#ifndef POINT_MOTION_HPP
#define POINT_MOTION_HPP 1

#include "../common/hydrodynamic_variables.hpp"
#include "../../tessellation/tessellation.hpp"

/*! \brief Abstract class for motion of mesh generating points
  \author Almog Yalinewich
 */
class PointMotion
{
public:

  /*! \brief Calculates the velocity of the point
    \param index Point index
    \param tessellation Positions of the points
    \param primitives Hydrodynamic variables
    \param time The simulation time	
    \return Velocity of the point
   */
  virtual Vector2D CalcVelocity(int index, 
				Tessellation const* tessellation,
				vector<Primitive> const& primitives,double time)= 0;

  /*! \brief Calculates the velocity of all mesh points
    \param tess The tessellation
    \param cells Hydrodynamics cells
    \param time The simulation time	
    \return Velocities of the points
   */
  virtual vector<Vector2D> calcAllVelocities(Tessellation const* tess,
					vector<Primitive> const& cells,
					double time);
  //! \brief Virtual destructor
  virtual ~PointMotion(void);
};

#endif