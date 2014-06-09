#include <limits>
#include <boost/foreach.hpp>
#include "linear_gauss_consistent.hpp"
#include "../../common/hydrodynamics.hpp"

ReducedPrimitiveGradient2D::ReducedPrimitiveGradient2D(void):
density(),pressure(),xvelocity(),yvelocity(),tracers(vector<Vector2D> ()) {}

ReducedPrimitiveGradient2D::ReducedPrimitiveGradient2D(Vector2D const& d,
	Vector2D const& p,Vector2D const& vx,Vector2D const& vy,vector<Vector2D>
	const& trace=vector<Vector2D>()):
density(d),pressure(p),xvelocity(vx),yvelocity(vy),tracers(trace) {}

ReducedPrimitiveGradient2D& ReducedPrimitiveGradient2D::operator+=
	(ReducedPrimitiveGradient2D const& source)
{
	density += source.density;
	pressure += source.pressure;
	xvelocity += source.xvelocity;
	yvelocity += source.yvelocity;

	if(source.tracers.empty()&&!tracers.empty())
	  {
	    throw "Something bad happened in ReducedPrimitiveGradient2D::operator+=";
	  }
	if(!tracers.empty())
	  {
	    if(tracers.size()!=source.tracers.size())
	      throw "Error in ReducedPrimitiveGradient2D::operator+ tracers have different size";
	  }
	if(tracers.empty()&&!source.tracers.empty()){
	  tracers.resize(source.tracers.size());
	}

	if(!tracers.empty())
	{
	  int n=(int)tracers.size();
	  for(int i=0;i<n;++i)
	    tracers[i]+=source.tracers[i];
	}
	return *this;
}

ReducedPrimitiveGradient2D& ReducedPrimitiveGradient2D::operator*=
	(double s)
{
	density *= s;
	pressure *= s;
	xvelocity *= s;
	yvelocity *= s;

	if(!tracers.empty())
	{
		int n=(int)tracers.size();
		for(int i=0;i<n;++i)
			tracers[i]*=s;
	}
	return *this;
}

class ReducedPrimitive
{
public:
	ReducedPrimitive(void):
	  density(0),pressure(0),xvelocity(0),yvelocity(0),tracers(vector<double> ()) {}

	  ReducedPrimitive(Primitive const& p,vector<double> const& trace
		  =vector<double>()):
	  density(p.Density),pressure(p.Pressure),xvelocity(p.Velocity.x),
		  yvelocity(p.Velocity.y),tracers(trace) {}

	  ReducedPrimitive(double d,double p,double vx,double vy,
		  vector<double> const& trace=vector<double>()):
	  density(d),pressure(p),xvelocity(vx), yvelocity(vy),tracers(trace) {}

	  double density;
	  double pressure;
	  double xvelocity;
	  double yvelocity;
	  vector<double> tracers;
};

namespace 
{
	ReducedPrimitive RpMultScalar(double s,ReducedPrimitive const& rp)
	{
		return ReducedPrimitive(s*rp.density,s*rp.pressure,s*rp.xvelocity,
			s*rp.yvelocity,s*rp.tracers);
	}

	ReducedPrimitive AddRp(ReducedPrimitive const& rp1,
		ReducedPrimitive const& rp2)
	{
		return ReducedPrimitive(rp1.density+rp2.density,rp1.pressure+rp2.pressure,
			rp1.xvelocity+rp2.xvelocity,rp1.yvelocity+rp2.yvelocity,
			rp1.tracers+rp2.tracers);
	}

	ReducedPrimitive MinusRp(ReducedPrimitive const& rp1,
		ReducedPrimitive const& rp2)
	{
		return ReducedPrimitive(rp1.density-rp2.density,	rp1.pressure-rp2.pressure,
			rp1.xvelocity-rp2.xvelocity,	rp1.yvelocity-rp2.yvelocity,
			rp1.tracers-rp2.tracers);
	}




	vector<double> ScalarProd(Vector2D const&v,vector<Vector2D> const& vec)
	{
		vector<double> res(vec.size());
		int n=(int)vec.size();
		for(int i=0;i<n;++i)
			res[i]=ScalarProd(v,vec[i]);
		return res;
	}

	ReducedPrimitive operator*(Vector2D const& v,
		ReducedPrimitiveGradient2D const& rpg)
	{
		return ReducedPrimitive(ScalarProd(v,rpg.density),
			ScalarProd(v,rpg.pressure),
			ScalarProd(v,rpg.xvelocity),
			ScalarProd(v,rpg.yvelocity),ScalarProd(v,rpg.tracers));
	}

	ReducedPrimitiveGradient2D operator*(double s,
		ReducedPrimitiveGradient2D const& rpg)
	{
		return ReducedPrimitiveGradient2D(s*rpg.density,s*rpg.pressure,
			s*rpg.xvelocity,s*rpg.yvelocity,s*rpg.tracers);
	}

	ReducedPrimitiveGradient2D operator-(ReducedPrimitiveGradient2D const& rpg1,
		ReducedPrimitiveGradient2D const& rpg2)
	{
		return ReducedPrimitiveGradient2D(rpg1.density-rpg2.density,	rpg1.pressure
			-rpg2.pressure,rpg1.xvelocity-rpg2.xvelocity,rpg1.yvelocity
			-rpg2.yvelocity,rpg1.tracers-rpg2.tracers);
	}

	vector<Vector2D> operator*(Vector2D const&v,vector<double> const& vec)
	{
		if(vec.empty())
			return vector<Vector2D> ();
		vector<Vector2D> res(vec.size());
		int n=(int)vec.size();
		for(int i=0;i<n;++i)
			res[i]=v*vec[i];
		return res;
	}

	ReducedPrimitiveGradient2D operator*(ReducedPrimitive const& rp,
		Vector2D const& v)
	{
		return ReducedPrimitiveGradient2D(v*rp.density,v*rp.pressure,v*rp.xvelocity,
			v*rp.yvelocity,v*rp.tracers);
	}
}

namespace 
{
	vector<int> GetOnlyRealNeighbors(Tessellation const* tess,int cell_index)
	{
		vector<int> Neigh=tess->GetNeighbors(cell_index);
		vector<int> res;
		res.reserve(Neigh.size());
		for(int i=0;i<(int)Neigh.size();++i)
			if(Neigh[i]>-1)
				res.push_back(Neigh[i]);
		return res;
	}

	vector<Primitive> GetRealNeighborPrimitive
		(Tessellation const* tess,
		vector<Primitive> const& cells,vector<int> const& Neigh)
	{
		vector<Primitive> res;
		for(int i=0;i<(int)Neigh.size();++i){
			if(Neigh[i]>-1)
				res.push_back(cells[tess->GetOriginalIndex(Neigh[i])]);
		}
		return res;
	}

	vector<Vector2D> GetRealNeighborMesh
		(Tessellation const* tess,
		vector<int> const& neigh)
	{
		vector<Vector2D> res;
		res.resize(neigh.size());
		for(int i=0;i<(int)neigh.size();++i){
			res[i]=tess->GetCellCM(neigh[i]);
		}
		return res;
	}

	Vector2D LinearGrad
		(vector<Vector2D> const& X,vector<double> const& val,
		Vector2D const& X0,double val0)
	{
		vector<double> dx,dy;
		dx.resize(X.size());
		dy.resize(X.size());
		for(int i=0;i<(int)X.size();++i){
			dx[i]=X[i].x-X0.x;
			dy[i]=X[i].y-X0.y;
		}
		double c1=0,c2=0,c3=0,c4=0,c5=0,r_inv;
		for(int i=0;i<(int)X.size();++i)
		{
			r_inv=1/(dx[i]*dx[i]+dy[i]*dy[i]);
			c1+=dy[i]*dy[i]*r_inv;
			c2+=dx[i]*dx[i]*r_inv;
			c3+=dx[i]*dy[i]*r_inv;
			c4+=dx[i]*(val[i]-val0)*r_inv;
			c5+=dy[i]*(val[i]-val0)*r_inv;
		}
		double b=(c5-c4*c3/c2)/(c1-c3*c3/c2);
		return Vector2D((c4-b*c3)/c2,b);
	}

	/*PrimitiveGradient2D LinearSlope
	(vector<Primitive> const& cells,
	Tessellation const* tess,int point)
	{
	vector<int> neigh=GetOnlyRealNeighbors(tess,point);
	vector<Primitive> neighbors=GetRealNeighborPrimitive(tess,cells,neigh);
	vector<Vector2D> neighbor_mesh=GetRealNeighborMesh(tess,neigh);
	Vector2D mesh_point=tess->GetCellCM(point);
	PrimitiveGradient2D res;
	vector<double> vals(neigh.size());
	Vector2D grad;
	for(int i=0;i<cells[0].GetVarNo();++i)
	{
	for(int j=0;j<(int)neigh.size();++j)
	vals[j]=neighbors[j][i];
	grad=LinearGrad(neighbor_mesh,vals,mesh_point,cells[point][i]);
	res.x[i]=grad.x;
	res.y[i]=grad.y;
	}
	return res;
	}
	*/
}


LinearGaussConsistent::LinearGaussConsistent
	(EquationOfState const& eos,
	OuterBoundary const& obc,
	HydroBoundaryConditions const* hbc,
	bool slf, bool soitf,double delta_v,double theta,
	double delta_P,bool rigidflag):
eos_(eos),rslopes_(),obc_(&obc),hbc_(hbc),slf_(slf),soitf_(soitf),
	shockratio_(delta_v),diffusecoeff_(theta),pressure_ratio_(delta_P),
	_rigidflag(rigidflag) {}

namespace 
{
	int GetNeighborIndex(int cell_index,Edge const& edge)
	{
		if(edge.GetNeighbor(0)==cell_index)
			return edge.GetNeighbor(1);
		else if(edge.GetNeighbor(1)==cell_index)
			return edge.GetNeighbor(0);
		else
			throw UniversalError("Error in GetNeighborIndex: edge does not bound cell");
	}

	ReducedPrimitive interp_all(Primitive const& cell,vector<double> const&
		cell_tracer,Vector2D const& cell_cm,ReducedPrimitiveGradient2D const& slope,
		Vector2D const& target)
	{
		return AddRp(ReducedPrimitive(cell,cell_tracer),(target-cell_cm)*slope);
	}

	Primitive interp_primitive(Primitive const& cell,Vector2D const& cell_cm,
		ReducedPrimitiveGradient2D const& slope,Vector2D const& target)
	{
		Primitive res(cell);
		res.Pressure+=ScalarProd(target-cell_cm,slope.pressure);
		res.Density+=ScalarProd(target-cell_cm,slope.density);
		res.Velocity.x+=ScalarProd(target-cell_cm,slope.xvelocity);
		res.Velocity.y+=ScalarProd(target-cell_cm,slope.yvelocity);
		return res;
	}

	vector<double> operator*(Vector2D const&v,vector<Vector2D> const&vec)
	{
		vector<double> res(vec.size());
		int n=(int)vec.size();
		for(int i=0;i<n;++i)
			res[i]=ScalarProd(v,vec[i]);
		return res;
	}

	vector<double> interp_tracer(vector<double> const& cell,
		Vector2D const& cell_cm,ReducedPrimitiveGradient2D const& slope,
		Vector2D const& target)
	{
		return cell+(target-cell_cm)*slope.tracers;
	}

	Vector2D GetReflectedPoint(Tessellation const* tess,int point,
		OuterBoundary const& /*obc*/,Edge const& edge)
	{
		Vector2D MeshPoint=tess->GetMeshPoint(point);
		Vector2D par=edge.GetVertex(1)-edge.GetVertex(0);
		if(abs(par.x)>abs(par.y))
		{
			// We are in the x direction
			if(MeshPoint.y>edge.get_y(0))
				MeshPoint.y-=MeshPoint.y-edge.get_y(0);
			else
				MeshPoint.y+=edge.get_y(0)-MeshPoint.y;
		}
		else
		{
			// We are in the y direction
			if(MeshPoint.x>edge.get_x(0))
				MeshPoint.x-=MeshPoint.x-edge.get_x(0);
			else
				MeshPoint.x+=edge.get_x(0)-MeshPoint.x;
		}
		return MeshPoint;
	}

	vector<Vector2D> GetNeighborMesh
		(Tessellation const* tess,vector<Edge> const&
		edges,int cell_index,OuterBoundary const& obc)
	{
		int n=(int)edges.size();
		int neigh0,neigh1;
		vector<Vector2D> res(n);
		for(int i=0;i<n;++i)
		{
			neigh0=edges[i].GetNeighbor(0);
			neigh1=edges[i].GetNeighbor(1);
			if(neigh0==cell_index)
				if(neigh1>-1) // we are not near rigid wall
					res[i]=tess->GetMeshPoint(neigh1);
				else
					res[i]=GetReflectedPoint(tess,neigh0,obc,edges[i]);
			else
				if(neigh0>-1)
					res[i]=tess->GetMeshPoint(neigh0);
				else
					res[i]=GetReflectedPoint(tess,neigh1,obc,edges[i]);
		}
		return res;
	}

	vector<Primitive> GetNeighborPrimitive
		(Tessellation const* tess,vector<Edge> const&
		edges,int cell_index,OuterBoundary const& /*obc*/,
		vector<Primitive> const& cells,HydroBoundaryConditions const* hbc,
		double time)
	{
		int n=(int)edges.size();
		int neigh0,neigh1;
		vector<Primitive> res(n);
		for(int i=0;i<n;++i)
		{
			if(hbc->IsBoundary(edges[i],tess))
			{
				res[i]=hbc->GetBoundaryPrimitive(edges[i],tess,cells,time);
			}
			else
			{
				neigh0=edges[i].GetNeighbor(0);
				neigh1=edges[i].GetNeighbor(1);
				if(neigh0==cell_index)
					res[i]=cells[neigh1];
				else
					res[i]=cells[neigh0];
			}
		}
		return res;
	}

	vector<vector<double> > GetNeighborTracers(Tessellation const* tess,
		vector<Edge> const&	edges,int cell_index,
		vector<vector<double> > const& tracers,
		HydroBoundaryConditions const* hbc,double time)
	{
		int n=(int)edges.size();
		int neigh0,neigh1;
		vector<vector<double> > res(n);
		for(int i=0;i<n;++i)
		{
			if(hbc->IsBoundary(edges[i],tess))
			{
				res[i]=hbc->GetBoundaryTracers(edges[i],tess,tracers,time);
			}
			else
			{
				neigh0=edges[i].GetNeighbor(0);
				neigh1=edges[i].GetNeighbor(1);
				if(neigh0==cell_index)
					res[i]=tracers[neigh1];
				else
					res[i]=tracers[neigh0];
			}
		}
		return res;
	}


	ReducedPrimitiveGradient2D calc_naive_slope(Primitive const& cell,
		Vector2D const& center,double cell_volume,vector<Primitive> const& neighbors,
		vector<Vector2D> const& neighbor_centers,vector<Edge> const& edge_list,
		vector<double> const& cell_tracer=vector<double>(),vector<vector<double> > const& neighbor_tracers
		=vector<vector<double> > ())
	{
		ReducedPrimitiveGradient2D res;
		const ReducedPrimitive phi_i(cell,cell_tracer);
		int n=(int)edge_list.size();
		for(int i=0;i<n;++i)
		{
			const Vector2D c_ij = CalcCentroid(edge_list[i])-
				0.5*(neighbor_centers[i]+center);
			const Vector2D r_ij = center - neighbor_centers[i];
			ReducedPrimitive phi_j;
			if(cell_tracer.empty())
				phi_j=ReducedPrimitive(neighbors[i]);
			else
				phi_j=ReducedPrimitive(neighbors[i],neighbor_tracers[i]);
			const double temp=edge_list[i].GetLength()/cell_volume;
			res += RpMultScalar(temp,MinusRp(phi_j,phi_i))*(c_ij/abs(r_ij))
				-RpMultScalar(0.5*temp,AddRp(phi_i,phi_j))*(r_ij/abs(r_ij));
		}
		return res;
	}

	class PropertyGetter
	{
	public:

		virtual double get_property(ReducedPrimitive const& rp) const = 0;

		virtual double get_property(Primitive const& p) const = 0;

		virtual Vector2D& get_property(ReducedPrimitiveGradient2D& rpg) const = 0;

		virtual ~PropertyGetter(void) {}
	};

	class DensityGetter: public PropertyGetter
	{
	public:
		double get_property(ReducedPrimitive const& rp) const
		{
			return rp.density;
		}

		double get_property(Primitive const& p) const
		{
			return p.Density;
		}

		Vector2D& get_property(ReducedPrimitiveGradient2D& rpg) const
		{
			return rpg.density;
		}
	};

	class PressureGetter: public PropertyGetter
	{
	public:
		double get_property(ReducedPrimitive const& rp) const
		{
			return rp.pressure;
		}

		double get_property(Primitive const& p) const
		{
			return p.Pressure;
		}

		Vector2D& get_property(ReducedPrimitiveGradient2D& rpg) const
		{
			return rpg.pressure;
		}
	};

	class XVelocityGetter: public PropertyGetter
	{
	public:

		double get_property(ReducedPrimitive const& rp) const
		{
			return rp.xvelocity;
		}

		double get_property(Primitive const& p) const
		{
			return p.Velocity.x;
		}

		Vector2D& get_property(ReducedPrimitiveGradient2D& rpg) const
		{
			return rpg.xvelocity;
		}
	};

	class YVelocityGetter: public PropertyGetter
	{
	public:

		double get_property(ReducedPrimitive const& rp) const
		{
			return rp.yvelocity;
		}

		double get_property(Primitive const& p) const
		{
			return p.Velocity.y;
		}

		Vector2D& get_property(ReducedPrimitiveGradient2D& rpg) const
		{
			return rpg.yvelocity;
		}
	};


	ReducedPrimitiveGradient2D slope_limit(Primitive const& cell,
		Vector2D const& center,vector<Primitive> const& neighbors,
		vector<Edge> const& edge_list,ReducedPrimitiveGradient2D const& slope,
		vector<vector<double> > const& neighbor_tracers=vector<vector<double> >(),
		vector<double> const& cell_trace=vector<double> ())
	{
		ReducedPrimitiveGradient2D res = slope;
		vector<ReducedPrimitive> centroid_vars(edge_list.size());
		int n=(int)edge_list.size();
		for(int i=0;i<n;++i)
		{
			centroid_vars[i] = interp_all(cell,cell_trace,center,slope,
				CalcCentroid(edge_list[i]));
		}
		DensityGetter density_getter;
		PressureGetter pressure_getter;
		XVelocityGetter xvelocity_getter;
		YVelocityGetter yvelocity_getter;
		boost::array<PropertyGetter const*,4> hydro_vars;
		hydro_vars[0]=&density_getter;
		hydro_vars[1]=&pressure_getter;
		hydro_vars[2]=&xvelocity_getter;
		hydro_vars[3]=&yvelocity_getter;
		BOOST_FOREACH(PropertyGetter const* rpg, hydro_vars)
		{
			PropertyGetter const& pg = *rpg;
			double psi = 1;
			boost::container::static_vector<double,17> neighbor_vals;
			BOOST_FOREACH(Primitive const& pr,neighbors)
			{
				neighbor_vals.push_back(pg.get_property(pr));
			}
			const double my_val = pg.get_property(cell);
			const double min_val = min(*std::min_element(neighbor_vals.begin(),
				neighbor_vals.end()),my_val);
			const double max_val = max(*std::max_element(neighbor_vals.begin(),
				neighbor_vals.end()),my_val);
			const double max_diff = max(max_val-my_val,
				my_val-min_val);
			for(int i=0;i<n;++i)
			{
				const double centroid_val = pg.get_property(centroid_vars[i]);
				const double dphi = centroid_val - my_val;
				if(abs(dphi)<0.1*max_diff&&centroid_val*my_val>0)
					continue;
				else if(centroid_val>my_val)
					psi = min(psi,(max_val-my_val)/(centroid_val-my_val));
				else if(centroid_val<my_val)
					psi = min(psi,(min_val-my_val)/(centroid_val-my_val));
				else if(centroid_val==my_val)
					psi = min(psi,1.0);
				else
					throw "Something bad happened in LinearGaussConsistent::Prepare";
			}
			pg.get_property(res) *= psi;
		}
		// Deal with tracers
		if(!res.tracers.empty())
		{
			int tracer_number=(int)res.tracers.size();
			for(int i=0;i<tracer_number;++i)
			{
				double psi = 1;
				boost::container::static_vector<double,17> neighbor_vals;
				for(int j=0;j<n;++j)
					neighbor_vals.push_back(neighbor_tracers[j][i]);
				const double my_val = cell_trace[i];
				const double min_val = min(*std::min_element(neighbor_vals.begin(),
					neighbor_vals.end()),my_val);
				const double max_val = max(*std::max_element(neighbor_vals.begin(),
					neighbor_vals.end()),my_val);
				const double max_diff = max(max_val-my_val,
					my_val-min_val);
				for(int k=0;k<n;++k)
				{
					const double centroid_val =centroid_vars[k].tracers[i];
					const double dphi = centroid_val - my_val;
					if(abs(dphi)<0.1*max_diff&&centroid_val*my_val>0)
						continue;
					else
						if(centroid_val>my_val)
							psi = min(psi,(max_val-my_val)/(centroid_val-my_val));
						else
							if(centroid_val<my_val)
								psi = min(psi,(min_val-my_val)/
								(centroid_val-my_val));
							else
								if(centroid_val==my_val)
									psi = min(psi,1.0);
								else
									throw "Something bad happened in LinearGaussConsistent::Prepare";
				}
				res.tracers[i] *= psi;
			}
		}
		return res;
	}


	ReducedPrimitiveGradient2D shocked_slope_limit(Primitive const& cell,
		Vector2D const& center,vector<Primitive> const& neighbors,
		vector<Edge> const& edge_list,ReducedPrimitiveGradient2D const& slope,
		double diffusecoeff,
		vector<vector<double> > const& neighbor_tracers=vector<vector<double> >(),
		vector<double> const& cell_trace=vector<double> ())
	{
		ReducedPrimitiveGradient2D res = slope;
		vector<ReducedPrimitive> centroid_vars(edge_list.size());
		int n=(int)edge_list.size();
		for(int i=0;i<n;++i)
		{
			centroid_vars[i] = interp_all(cell,cell_trace,center,slope,
				CalcCentroid(edge_list[i]));
		}
		DensityGetter density_getter;
		PressureGetter pressure_getter;
		XVelocityGetter xvelocity_getter;
		YVelocityGetter yvelocity_getter;
		boost::array<PropertyGetter const*,4> hydro_vars;
		hydro_vars[0]=&density_getter;
		hydro_vars[1]=&pressure_getter;
		hydro_vars[2]=&xvelocity_getter;
		hydro_vars[3]=&yvelocity_getter;
		BOOST_FOREACH(PropertyGetter const* rpg, hydro_vars)
		{
			PropertyGetter const& pg = *rpg;
			double psi = 1;
			boost::container::static_vector<double,17> neighbor_vals;
			BOOST_FOREACH(Primitive const& pr,neighbors)
			{
				neighbor_vals.push_back(pg.get_property(pr));
			}
			const double my_val = pg.get_property(cell);
			double maxdiff = 0;
			BOOST_FOREACH(double nv, neighbor_vals)
			{
				maxdiff = max(maxdiff,abs(nv-my_val));
			}
			for(int i=0;i<(int)edge_list.size();++i)
			{
				const double centroid_val = pg.get_property(centroid_vars[i]);
				const double dphi = centroid_val - my_val;
				if(abs(dphi)<0.1*maxdiff&&centroid_val*my_val>0)
					continue;
				else if(dphi>0)
					psi = min(psi,diffusecoeff*max((neighbor_vals[i]-my_val)/dphi,0.0));
				else if(dphi<0)
					psi = min(psi,diffusecoeff*max((neighbor_vals[i]-my_val)/dphi,0.0));
				else if(dphi==0)
					psi = min(psi,1.0);
				else
					throw "Something bad happened in LinearGaussConsistent::Prepare";
			}
			pg.get_property(res) *= psi;
		}

		// Deal with tracers
		if(!res.tracers.empty())
		{
			int tracer_number=(int)res.tracers.size();
			for(int i=0;i<tracer_number;++i)
			{
				double psi = 1;
				boost::container::static_vector<double,17> neighbor_vals;
				for(int j=0;j<n;++j)
					neighbor_vals.push_back(neighbor_tracers[j][i]); 
				//const double my_val = cell_trace[i];
				const double my_val2 = cell_trace[i];
				double maxdiff = 0;
				BOOST_FOREACH(double nv, neighbor_vals)
				{
					maxdiff = max(maxdiff,abs(nv-my_val2));
				}
				for(int j=0;j<n;++j)
				{
					const double centroid_val = centroid_vars[j].tracers[i];
					const double dphi = centroid_val - my_val2;
					if(abs(dphi)<0.1*maxdiff&&centroid_val*my_val2>0)
						continue;
					else
						if(dphi>0)
							psi = min(psi,diffusecoeff*
							max((neighbor_vals[i]-my_val2)/dphi,0.0));
						else
							if(dphi<0)
								psi = min(psi,diffusecoeff*
								max((neighbor_vals[i]-my_val2)/dphi,0.0));
							else
								if(dphi==0)
									psi = min(psi,1.0);
								else
									throw "Something bad happened in LinearGaussConsistent::Prepare";
				}
				res.tracers[i] *= psi;
			}
		}
		return res;
	}


	vector<int> GetNeighborIndices(int cell_index,
		vector<Edge> const& edge_list)
	{
		vector<int> res(edge_list.size(),0);
		for(int i=0;i<(int)edge_list.size();++i){
			res[i] = GetNeighborIndex(cell_index,edge_list[i]);
		}
		return res;
	}

	vector<Primitive> GetNeighborList(vector<Primitive> const& cells, 
		vector<int> const& cell_indices)
	{
		vector<Primitive> res(cell_indices.size());
		for(int i=0;i<(int)cell_indices.size();++i){
			res[i] = cells[cell_indices[i]];
		}
		return res;
	}

	vector<Vector2D> GetNeighborMeshList(Tessellation const* tess,
		vector<int> const& neighbor_indices)
	{
		vector<Vector2D> res(neighbor_indices.size());
		for(int i=0;i<(int)neighbor_indices.size();++i){
			res[i] = tess->GetMeshPoint(neighbor_indices[i]);
		}
		return res;
	}

	vector<Edge> GetEdgeList(Tessellation const* tess,
		vector<int> const& edge_indices)
	{
		vector<Edge> res(edge_indices.size());
		for(int i=0;i<(int)edge_indices.size();++i){
			res[i] = tess->GetEdge(edge_indices[i]);
		}
		return res;
	}

	double PressureRatio(Primitive cell,vector<Primitive> const& neigh)
	{
		int n=(int)neigh.size();
		double res=1;
		double p=cell.Pressure;
		for(int i=0;i<n;++i)
		{
			if(p>neigh[i].Pressure)
				res=min(res,neigh[i].Pressure/p);
			else
				res=min(res,p/neigh[i].Pressure);
		}
		return res;
	}

	bool is_shock(ReducedPrimitiveGradient2D const& naive_slope,
		double cell_width,
		double shock_ratio,
		Primitive const& cell,
		vector<Primitive> const& neighbor_list,
		double pressure_ratio)
	{
		const bool cond1 = 
			(naive_slope.xvelocity.x+naive_slope.yvelocity.y)*
			cell_width>(-shock_ratio)*cell.SoundSpeed;
		const bool cond2 = PressureRatio(cell,neighbor_list)>pressure_ratio;
		return cond1&&cond2;
	}

	ReducedPrimitiveGradient2D calc_slope(Tessellation const* tess,
		vector<Primitive> const& cells,vector<vector<double> >
		const& tracers,int cell_index,bool slf,
		OuterBoundary const& obc,HydroBoundaryConditions const* hbc,
		double shockratio,double diffusecoeff,double pressure_ratio,
		double time,bool /*rigidflag*/)
	{
		vector<int> edge_indices = tess->GetCellEdges(cell_index);
		vector<Edge> edge_list = GetEdgeList(tess,edge_indices);
		vector<Vector2D> neighbor_mesh_list = GetNeighborMesh(tess,edge_list,
			cell_index,obc);
		vector<Primitive> neighbor_list = GetNeighborPrimitive(tess, edge_list, cell_index,
			obc, cells, hbc, time);
		vector<vector<double> > neighbor_tracers;
		if(!tracers.empty())
			neighbor_tracers=GetNeighborTracers(tess,edge_list,cell_index,tracers,
			hbc,time);
		ReducedPrimitiveGradient2D naive_slope;
		if(!tracers.empty())
			naive_slope=calc_naive_slope
			(cells[cell_index],tess->GetMeshPoint(cell_index),
			tess->GetVolume(cell_index),	neighbor_list,
			neighbor_mesh_list,edge_list,tracers[cell_index],neighbor_tracers);
		else
			naive_slope=calc_naive_slope
			(cells[cell_index],tess->GetMeshPoint(cell_index),
			tess->GetVolume(cell_index),	neighbor_list,
			neighbor_mesh_list,edge_list);
		if(slf)
		{
			if(is_shock(naive_slope,tess->GetWidth(cell_index),shockratio,
				cells[cell_index],neighbor_list,pressure_ratio))
			{
				if(!tracers.empty())
					return slope_limit(cells[cell_index],tess->GetCellCM(cell_index),
					neighbor_list,edge_list,naive_slope,neighbor_tracers,
					tracers[cell_index]);
				else
					return slope_limit(cells[cell_index],tess->GetCellCM(cell_index),
					neighbor_list,edge_list,naive_slope);
			}
			else
			{
				if(!tracers.empty())
					return shocked_slope_limit(cells[cell_index],
					tess->GetCellCM(cell_index),	neighbor_list,edge_list,
					naive_slope,diffusecoeff,neighbor_tracers,tracers[cell_index]);
				else
					return shocked_slope_limit(cells[cell_index],
					tess->GetCellCM(cell_index),	neighbor_list,edge_list,
					naive_slope,diffusecoeff);
			}
		}
		else
		{
			return naive_slope;
		}
	}
}


void LinearGaussConsistent::Prepare(Tessellation const* tessellation,
	vector<Primitive> const& cells,vector<vector<double> > const& tracers,
	double /*dt*/,double time)
{
	if(tessellation->GetPointNo()!=(int)rslopes_.size())
	{
		rslopes_.resize(tessellation->GetPointNo());
	}
	for(int i=0;i<tessellation->GetPointNo();++i)
	{
		if(!hbc_->IsGhostCell(i,tessellation))
			rslopes_[i] = calc_slope(tessellation,cells,tracers,i,slf_,*obc_,hbc_,
			shockratio_,diffusecoeff_,pressure_ratio_,time,_rigidflag);
	}
}

namespace {

	Primitive CalcDtFlux(Primitive const&cell,
		ReducedPrimitiveGradient2D const& grad,double dt)
	{
		Primitive res;
		res.Density-=0.5*dt*(cell.Velocity.x*grad.density.x+
			cell.Velocity.y*grad.density.y+cell.Density*(grad.xvelocity.x+
			grad.yvelocity.y));
		res.Velocity.x-=0.5*dt*(cell.Velocity.x*
			grad.xvelocity.x+cell.Velocity.y*
			grad.yvelocity.x+grad.pressure.x/cell.Density);
		res.Velocity.y-=0.5*dt*(cell.Velocity.x*
			grad.xvelocity.y+cell.Velocity.y*
			grad.yvelocity.y+grad.pressure.y/cell.Density);
		res.Pressure-=0.5*dt*(cell.Pressure*(1+cell.Pressure/
			(cell.Energy*cell.Density))*(grad.xvelocity.x+
			grad.yvelocity.y)+grad.pressure.x*cell.Velocity.x
			+grad.pressure.y*cell.Velocity.y);
		return res;
	}

	vector<double> CalcDtFlux(vector<double> const& tracer,Primitive const& cell,
		ReducedPrimitiveGradient2D const& grad,double dt)
	{
		vector<double> res(tracer.size());
		int n=(int)tracer.size();
		for(int i=0;i<n;++i)
			res[i]=-0.5*dt*(grad.tracers[i].x*cell.Velocity.x+
			grad.tracers[i].y*cell.Velocity.y);
		return res;
		
	}
}

vector<double> LinearGaussConsistent::interpolateTracers
	(Tessellation const* tess,vector<Primitive> const& cells,
	vector<vector<double> > const& tracers,double dt,Edge const& edge,
	int side,InterpolationType interptype) const
{
	int cell_index = edge.GetNeighbor(side);
	Vector2D target = CalcCentroid(edge);
	if(interptype==InBulk)
	{
		vector<double> res=interp_tracer(tracers[cell_index],
			tess->GetCellCM(cell_index),rslopes_[cell_index],target);
		if(soitf_)
		{
			vector<double> temp=CalcDtFlux(tracers[cell_index],cells[cell_index],
				rslopes_[cell_index],dt);
			int n=(int)temp.size();
			for(int i=0;i<n;++i)
				res[i]+=temp[i];
		}
		return res;
	}
	else
		if(interptype==Boundary)
		{
			int other=edge.GetNeighbor((side+1)%2);
			vector<double> res=interp_tracer(tracers[tess->GetOriginalIndex(other)],
				tess->GetCellCM(other),rslopes_[tess->GetOriginalIndex(other)],
				target);
		if(soitf_)
		{
			vector<double> temp=CalcDtFlux(tracers[tess->GetOriginalIndex(other)],
				cells[tess->GetOriginalIndex(other)],
				rslopes_[tess->GetOriginalIndex(other)],dt);
			int n=(int)temp.size();
			for(int i=0;i<n;++i)
				res[i]+=temp[i];
		}
		return res;
		}
		else
			throw UniversalError("Wrong interpolation type in linear_gauss_scalar");
}

Primitive LinearGaussConsistent::Interpolate(Tessellation const* tess,
	vector<Primitive> const& cells,double dt,Edge const& edge,int side,
	InterpolationType interptype) const
{
	int cell_index = edge.GetNeighbor(side);
	Vector2D target = CalcCentroid(edge);
	if(interptype==InBulk)
	{
		const Primitive temp = interp_primitive(cells[cell_index],
			tess->GetCellCM(cell_index),rslopes_[cell_index],target);
		Primitive res = CalcPrimitive(temp.Density,temp.Pressure,
			temp.Velocity,eos_);
		if(soitf_)
		{
			res+=CalcDtFlux(cells[cell_index],rslopes_[cell_index],dt);
			res = CalcPrimitive(res.Density,res.Pressure,Vector2D(res.Velocity.x,
				res.Velocity.y),eos_);
		}
		return res;
	}
	else
		if(interptype==Boundary)
		{
			const int other=edge.GetNeighbor((side+1)%2);
			const Primitive temp = interp_primitive(cells[tess->GetOriginalIndex(other)],
				tess->GetMeshPoint(other),rslopes_[tess->GetOriginalIndex(other)],
				target);
			Primitive res = CalcPrimitive(temp.Density,temp.Pressure,
				temp.Velocity,eos_);
			if(soitf_)
			{
				res+=CalcDtFlux(cells[cell_index],rslopes_[cell_index],dt);
				res = CalcPrimitive(res.Density,res.Pressure,res.Velocity,eos_);
			}
			return res;
		}
		else
			throw UniversalError("Wrong interpolation type");
}

LinearGaussConsistent::~LinearGaussConsistent(void) {}