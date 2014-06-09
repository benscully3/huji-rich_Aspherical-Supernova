#ifndef PLM1D_HPP
#define PLM1D_HPP 1

#include "spatial_reconstruction1d.hpp"

/*! \brief Piecewise linear spatial reconstruction
 */
class PLM1D: public SpatialReconstruction1D
{
public:

  /*! \brief Class constructor
    \param second_order_time Toggles using characteristic decomposition to achieve second order in time
    \param slope_limiter_flag Toggles slope limiters
   */
  PLM1D(bool second_order_time=false, bool slope_limiter_flag=true);

  Primitive InterpState(vector<double> const& vp,
			vector<Primitive> const& hv,
			int i, int dir, double dt) const;

private:

  bool second_order_time_;
  bool slope_limiter_flag_;
};

#endif // PLM1D_HPP