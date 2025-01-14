#ifdef RICH_MPI
#include "source/mpi/MeshPointsMPI.hpp"
#endif
#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>
#include <string>
#include <tuple>
#include <random>
#include "source/tessellation/geometry.hpp"
#include "source/newtonian/two_dimensional/hdsim2d.hpp"
#include "source/tessellation/tessellation.hpp"
#include "source/newtonian/common/hllc.hpp"
#include "source/newtonian/common/ideal_gas.hpp"
#include "source/tessellation/VoronoiMesh.hpp"
#include "source/newtonian/two_dimensional/spatial_distributions/uniform2d.hpp"
#include "source/newtonian/two_dimensional/point_motions/eulerian.hpp"
#include "source/newtonian/two_dimensional/point_motions/lagrangian.hpp"
#include "source/newtonian/two_dimensional/point_motions/round_cells.hpp"
#include "source/newtonian/two_dimensional/source_terms/cylindrical_complementary.hpp"
#include "source/newtonian/two_dimensional/source_terms/zero_force.hpp"
#include "source/newtonian/two_dimensional/geometric_outer_boundaries/SquareBox.hpp"
#include "source/newtonian/test_2d/random_pert.hpp"
#include "source/newtonian/two_dimensional/diagnostics.hpp"
#include "source/misc/simple_io.hpp"
#include "source/misc/mesh_generator.hpp"
#include "source/newtonian/test_2d/main_loop_2d.hpp"
#include "source/newtonian/two_dimensional/hdf5_diagnostics.hpp"
#include "source/tessellation/shape_2d.hpp"
#include "source/newtonian/test_2d/piecewise.hpp"
#include "source/newtonian/two_dimensional/simple_flux_calculator.hpp"
#include "source/newtonian/two_dimensional/simple_cell_updater.hpp"
#include "source/newtonian/two_dimensional/simple_extensive_updater.hpp"
#include "source/newtonian/two_dimensional/stationary_box.hpp"
#include "source/newtonian/two_dimensional/amr.hpp"
#include "source/newtonian/test_2d/multiple_diagnostics.hpp"
#include "source/newtonian/test_2d/consecutive_snapshots.hpp"
#include "source/misc/utils.hpp"
#include "source/tessellation/right_rectangle.hpp"
#include "source/newtonian/test_2d/clip_grid.hpp"
#include "quadrupole_breakout/source/make_aspherical.hpp"
#include "quadrupole_breakout/source/pressure_floor.hpp"
#include "quadrupole_breakout/source/get_timestep.hpp"
#include "quadrupole_breakout/source/improved_stationary_box.hpp"
#include "quadrupole_breakout/source/remove_edge_cells.hpp"
#include "quadrupole_breakout/source/refine_nothing.hpp"
#include "quadrupole_breakout/source/combined_manip.hpp"

using namespace std;
using namespace simulation2d;

namespace {

#ifdef RICH_MPI

  vector<Vector2D> process_positions(const SquareBox& boundary)
  {
    const Vector2D lower_left = boundary.getBoundary().first;
    const Vector2D upper_right = boundary.getBoundary().second;
	int ws=0;
	MPI_Comm_size(MPI_COMM_WORLD,&ws);
    return RandSquare(ws,lower_left.x,upper_right.x,lower_left.y,upper_right.y);
  }

#endif
  // interpolates values from a polytrope density profile 
   double interp_polytrope(const vector<double>& rho_poly,const double& R,
  			  const double& R_s, const double& N_poly){
    double rho;
    int i;
    i = int((N_poly*(R/R_s)) + 0.5);
    if (rho_poly[i]==0){
      rho = 1e-12;
    }
    else{
      rho = rho_poly[i];
    }
    return rho;
   }

  // determines the nearest cell to a position r from tessellation
  size_t nearest_cell_index_tess(const Tessellation& tess,
				 const Vector2D& r)
  {
    double inv_dist = 0;
    size_t res = 0;
    const size_t n = static_cast<size_t>
      (tess.GetPointNo());
    for(size_t i=0;i<n;++i){
      const Vector2D& s = tess.GetMeshPoint
	(static_cast<int>(i));
      const double dist = abs(s-r);
      if(1.0/dist > inv_dist){
	inv_dist = 1.0/dist;
	res = i;
      }
    }
    return res;
  }

  // sets up the initial conditions for the simulation
  vector<ComputationalCell> calc_init_cond(const Tessellation& tess)
  {
    vector<ComputationalCell> res(static_cast<size_t>(tess.GetPointNo()));
    const double R_s = 1.;
    const vector<double> rho_poly = read_vector("polytrope_rho.txt");
        
    for(size_t i=0;i<res.size();++i){
      const Vector2D& r = tess.GetMeshPoint(static_cast<int>(i));
      const double R = abs(r);
      
      res[i].density = 1e-12;
      // initalize explosion by setting high pressure within 0.05*R
      res[i].pressure = abs(tess.GetMeshPoint(static_cast<int>(i)))<0.05 ? 1e5 : 1e-13;     
      /*
       THIS INTIALIZES AN OFFSET EXPLOSION
       COMMENT OUT ABOVE 'res[i].pressure' LINE IF USING

      const double a = 0.01;

      if ((r.x<sqrt(0.0025-pow(r.y-a,2)))&&
	  (r.x>-sqrt(0.0025-pow(r.y-a,2)))&&
	  (r.y<sqrt(0.0025-pow(r.x,2))+a)&&
	  (r.y>-sqrt(0.0025-pow(r.x,2))+a))
	res[i].pressure = 1e5;
      else
	res[i].pressure = 1e-13;
      */

      /*
       THIS SETS UP A DECAYING DENSITY AND PRESSURE FOR OUTSIDE THE STAR
       INCREASES REALISM BUT INCREASES RUNTIME

      if (R>=R_s){
	res[i].density = 1e-12*pow(R,-3);
	res[i].pressure = 1e-13*pow(R,-3);
      }
      */

      res[i].velocity = Vector2D(0,0);
      // intialize tracers which diffuse in simulation
      res[i].tracers[0] = 0.;
      res[i].tracers[1] = 0.;
      res[i].tracers[2] = 0.;
      res[i].tracers[3] = 0.;
      // intialize stickers which stick to a cell during simulation
      res[i].stickers[0] = false;
      res[i].stickers[1] = false;
      res[i].stickers[2] = false;
      res[i].stickers[3] = false;
      // interpolate a polytrope density profile
      if(R<R_s){
        res[i].density = interp_polytrope(rho_poly, R, R_s, 10000.);
	
      }  
    }

    // place a sticker and tracer  at chosen coordinates
    // NOTE: huji-rich only allows four of each
    size_t idx0 = nearest_cell_index_tess(tess, 0.97*Vector2D(cos(0.698),
							      sin(0.698)));
    res[idx0].tracers[0] = 1.;
    res[idx0].stickers[0] = true;

    size_t idx1 = nearest_cell_index_tess(tess, 0.97*Vector2D(cos(0.7156),
							      sin(0.7156)));
    res[idx1].tracers[1] = 1.;
    res[idx1].stickers[1] = true;

    size_t idx2 = nearest_cell_index_tess(tess, 0.97*Vector2D(cos(0.733),
							      sin(0.733)));
    res[idx2].tracers[2] = 1.;
    res[idx2].stickers[2] = true;
    
    size_t idx3 = nearest_cell_index_tess(tess, 0.97*Vector2D(cos(0.75),
							      sin(0.75)));
    res[idx3].tracers[3] = 1.;
    res[idx3].stickers[3] = true;
    
    return res;
  }

  // method to place a point based on the resolution of the mesh-grid
  double res_func(const double r,
		  const double r_star, // stellar radius
		  const double dr_c, // central resolution
		  const double dr_s, // stellar surface resolution
		  const double alpha, // resoution modifier outside of surface
		  const double b){
    double res;
    if (r<r_star){ // resolution inside of star
      res = dr_s + (dr_c - dr_s)*pow((1 - r/r_star), b);
    }
    else{ // resolution outside of star
      const vector<double> vec =
	{dr_s + (2*alpha*r_star - dr_s)*(r/r_star - 1),
	 alpha*r/r_star};
      res = min(vec);
    }
    return res;
  }

  /*
  method to build the mesh-grid as a spiral that is a function
  of r and theta based on the resolution function in method res_func(ln 185)
  */
  vector<Vector2D> make_grid(const double r_star, // stellar radius
			     const double r_dom, // computational domain radius
			     const double dr_c, //central resolution
			     const double dr_s, //surface resolution
			     const double alpha, // resoution modifier outside of surface
			     const double b){
    //const size_t N = static_cast<size_t>
    //  (M_PI*pow(r_star, 2.)/pow(alpha, 2.)*(2.*log(r_dom/r_star) + r_star));

    // initial r and theta
    const double r_0 = res_func(0, r_star, dr_c, dr_s, alpha, b)/8.;
    const double t_0 = M_PI;
    vector<double> r;
    vector<double> theta;
    vector<Vector2D> res;
    vector<Vector2D> t_res;
    r.push_back(r_0);
    theta.push_back(t_0);
    t_res.push_back(Vector2D(r[0]*cos(theta[0]),
			     r[0]*sin(theta[0])));
    int i = 0;
    while (r[i]<r_dom){
      double r_i = r[i];
      double t_i = theta[i];
      double f_i = res_func(r_i, r_star, dr_c, dr_s, alpha, b);
      double dt = f_i/r_i;
      if (dt>1){ // prevents big spread in initial values when r_i << 1
	dt = 1.;
      }
      double dr = f_i/(2*M_PI)*dt;
      r.push_back(r_i + dr);
      theta.push_back(t_i + dt);
      t_res.push_back(r[i+1]*Vector2D(cos(theta.at(i)),
				      sin(theta.at(i))));
      i++;
    }
    for(size_t j=0;j<t_res.size();j++){
      // this mirrors the mesh-grid on the x-axis so the explosion is not offcentred
      if (t_res[j].y>0){
    	res.push_back(t_res[j]);
    	res.push_back(Vector2D(t_res[j].x,
    			       -t_res[j].y));
      }     
    }
    return res;    
  }

  // sets up the simulation data
  class SimData
  {
  public:

    SimData(Vector2D lower_left = Vector2D(0.0002,0)+Vector2D(0,-1000),
	    Vector2D upper_right = Vector2D(0.0002,0)+Vector2D(1000,1000)):
      pg_(Vector2D(0,0), Vector2D(0,1)),
      //width_(1),
      outer_(lower_left, upper_right),
#ifdef RICH_MPI
	  vproc_(process_positions(outer_),outer_),
		init_points_(SquareMeshM(100,100,vproc_,outer_.getBoundary().first,outer_.getBoundary().second)),
		tess_(vproc_,init_points_,outer_),
#else
      //init_points_(cartesian_mesh(100,100,outer_.getBoundary().first,
      //	       		  outer_.getBoundary().second)),
      //		tess_(init_points_, outer_),

      // constructs the mesh-grid
      init_points_(clip_grid(RightRectangle(lower_left, upper_right),
      			     make_grid(1., 1000., 0.05, 0.004, 0.02, 1.))),
      tess_(init_points_, outer_),
      
#endif
      eos_(4./3.),
      //l_motion_(),
      l_motion_(),
      point_motion_(l_motion_, eos_, outer_, 0.3),
      //point_motion_(),
      sb_(),
      rs_(),
      force_(pg_.getAxis()),
      tsf_(0.3),
      fc_(rs_),
      eu_(),
      cu_(),
      sim_(
#ifdef RICH_MPI
		  vproc_,
#endif
		  tess_,
	   outer_,
	   pg_,
	   calc_init_cond(tess_),
	   eos_,
	   point_motion_,
	   sb_,
	   force_,
	   tsf_,
	   fc_,
	   eu_,
	   cu_,
      {vector<string>{"40", "41", "42", "43"}, // names for tracers and stickers
       vector<string>{"40", "41", "42", "43"}})
    {}

    

    hdsim& getSim(void)
    {      
      return sim_;
    }

  private:
    const CylindricalSymmetry pg_;
    //const double width_;
    const SquareBox outer_;
#ifdef RICH_MPI
	VoronoiMesh vproc_;
#endif
    const vector<Vector2D> init_points_;
    VoronoiMesh tess_;
    const IdealGas eos_;
#ifdef RICH_MPI
	//Eulerian point_motion_;
	Lagrangian point_motion_;
#else
    //Lagrangian point_motion_;
    //Eulerian point_motion_;
    Lagrangian l_motion_;
    RoundCells point_motion_;
#endif
    //const StationaryBox sb_;
    const ImprovedStationaryBox sb_;
    const Hllc rs_;
    CylindricalComplementary force_;
    const SimpleCFL tsf_;
    const SimpleFluxCalculator fc_;
    const SimpleExtensiveUpdater eu_;
    //const SimpleCellUpdater cu_;
    const PressureFloor cu_;
    hdsim sim_;
  };

  // write the current cycle count
  class WriteCycle: public DiagnosticFunction
  {
  public:
    
    WriteCycle(const string& fname):
      fname_(fname) {}      

    void operator()(const hdsim& sim)
    {
      write_number(sim.getCycle(), fname_);
    }

  private:
    const string fname_;
  };
  

  void write_tuple(const string fname,
		   const tuple<int, double, Vector2D, ComputationalCell> tup,
		   bool& new_f)
  {
    ofstream file_out;
    if (new_f){
      file_out.open(fname, ios_base::out);
      const string sep = ", ";
      file_out << get<0>(tup) << sep
	       << get<1>(tup) << sep
	       << get<2>(tup).x << sep
	       << get<2>(tup).y << sep
	       << get<3>(tup).velocity.x << sep
	       << get<3>(tup).velocity.y << sep
	       << get<3>(tup).density << sep
	       << get<3>(tup).pressure << "\n";
      file_out.close();
      new_f = false;
    }
    else{
      file_out.open(fname, ios_base::app);
      const string sep = ", ";
      file_out << get<0>(tup) << sep
	       << get<1>(tup) << sep
	       << get<2>(tup).x << sep
	       << get<2>(tup).y << sep
	       << get<3>(tup).velocity.x << sep
	       << get<3>(tup).velocity.y << sep
	       << get<3>(tup).density << sep
	       << get<3>(tup).pressure << "\n";
      file_out.close();
    }
  }

  // get the cell index for a given sticker
  size_t get_sticker_idx(const vector<ComputationalCell> cells,
			 const int sticker_num)
  {
    size_t res = 0;
    for (size_t i=0; i<cells.size(); ++i){
	if (cells[i].stickers[sticker_num]){
	    res=i;
	    return res;
	}
    }
    return res;
  }

  // write the data from a cell witha  sticker
  class TrackCell: public DiagnosticFunction
  {
  public:
    
    TrackCell(const int& sticker_num,
	      const string& fname,
	      bool& new_f):
      sticker_num_(sticker_num),
      fname_(fname),
      new_f_(new_f){}

    void operator()(const hdsim& sim)
    {
      size_t idx = get_sticker_idx(sim.getAllCells(),
				    sticker_num_);
      const double t = sim.getTime();
      const Vector2D& r = sim.getTessellation().GetMeshPoint
	(static_cast<int>(idx));
      const ComputationalCell& c = sim.getAllCells()[idx];
      tuple<int, double, Vector2D, ComputationalCell> tup = make_tuple(static_cast<int>(idx),t,r,c);
      write_tuple(fname_, tup, new_f_);
    }
  
  private:
    const int sticker_num_;
    const string fname_;
    bool& new_f_;
  };
}

int main(void)
{
#ifdef RICH_MPI
	MPI_Init(NULL,NULL);
	int rank=0;
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);
#endif
  SimData sim_data;
  hdsim& sim = sim_data.getSim();
  const Tessellation& tess = sim.getTessellation();
  const PhysicalGeometry& pg = sim.getPhysicalGeometry();

  //const double tf = 2e-1;
  const double tf = 10.;
  SafeTimeTermination term_cond(tf, 1e6);
  bool new_f0 = true;
  bool new_f1 = true;
  bool new_f2 = true;
  bool new_f3 = true;
  
  MultipleDiagnostics diag
    ({new ConsecutiveSnapshots
      (new ConstantTimeInterval(tf/150),
       new Rubric("output/snapshot_",".h5")),
      new getTimeStep("timestep.txt"),
      new WriteTime("time.txt"),
      new TrackCell(0, "tracked_cell_40_97.txt", new_f0),
      new TrackCell(1, "tracked_cell_41_97.txt", new_f1),
      new TrackCell(2, "tracked_cell_42_97.txt", new_f2),
      new TrackCell(3, "tracked_cell_43_97.txt", new_f3),
      new WriteCycle("cycle.txt")});

  
  bool cont = true; // condition to make aspherical
  MakeAspherical make_aspherical(1., 0.12, cont,
		       tess, pg, "energy.txt");

  const OuterBoundary& outer = sim.getOuterBoundary();
  const vector<Edge> edges = outer.GetBoxEdges();
  const double right = edges[0].vertices.first.x;
  const double top = edges[1].vertices.first.y;
  const double left = edges[2].vertices.first.x;
  const double bottom = edges[3].vertices.first.y;

  RemoveEdgeCells remove(1e-6, left,
			 right, top, bottom);
  RefineNothing refine;
  ConservativeAMR amr(refine,remove);

  CombinedManip manip(make_aspherical, amr);

  main_loop(sim,
	    term_cond,
	    &hdsim::TimeAdvance,
	    &diag,
	    &manip);

#ifdef RICH_MPI
  write_snapshot_to_hdf5(sim, "process_"+int2str(rank)+"_final"+".h5");
  MPI_Finalize();
#else
  write_snapshot_to_hdf5(sim, "final.h5");
#endif


  return 0;
}

