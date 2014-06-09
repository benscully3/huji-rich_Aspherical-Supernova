#ifndef TRIANGLE_STEP_HPP
#define TRIANGLE_STEP_HPP 1

#include "../two_dimensional/hdsim2d.hpp"
#include "../two_dimensional/geometric_outer_boundaries/SquareBox.hpp"
#include "../two_dimensional/hydro_boundary_conditions/RigidWallHydro.hpp"
#include "../../tessellation/VoronoiMesh.hpp"
#include "../two_dimensional/interpolations/pcm2d.hpp"
#include "../two_dimensional/spatial_distributions/uniform2d.hpp"
#include "../two_dimensional/spatial_distributions/triangle.hpp"
#include "../common/ideal_gas.hpp"
#include "../two_dimensional/point_motions/eulerian.hpp"
#include "../two_dimensional/point_motions/lagrangian.hpp"
#include "../common/hllc.hpp"
#include "../two_dimensional/source_terms/zero_force.hpp"
#include "square_grid.hpp"

/*! \brief Simulation data for a square grid with a trigangular step in the middle
 */
class TriangleStep
{
public:

  /*! \brief Class constructor
    \param point_motion Name of point motion scheme
   */
  TriangleStep(string const& point_motion);

  /*! \brief Returns reference to simulation
   */
  hdsim& getSim(void);

private:

  double width_;
  vector<Vector2D> init_points_;
  SquareBox outer_;
  VoronoiMesh tess_;
  PCM2D interp_method_;
  Uniform2D density_;
  vector<Vector2D> triangle_vertices_;
  Triangle pressure_;
  Triangle xvelocity_;
  Triangle yvelocity_;
  IdealGas eos_;
  Eulerian eulerian_;
  Lagrangian lagrangian_;
  Hllc rs_;
  RigidWallHydro hbc_;
  ZeroForce force_;
  hdsim sim_;
};

#endif // TRIANGLE_STEP_HPP