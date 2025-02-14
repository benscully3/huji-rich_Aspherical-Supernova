#ifndef MAKEASPHERICAL_HPP
#define MAKEASPHERICAL_HPP 1


#include "source/newtonian/test_2d/main_loop_2d.hpp"
#include "source/newtonian/two_dimensional/cache_data.hpp"
#include "source/misc/simple_io.hpp"

/*
If shock front is at a certain fraction(frac) of the radius
x-velocity is removed to make the explosion aspherical
 */

using namespace simulation2d;

class MakeAspherical: public Manipulate
{
public:
  MakeAspherical(const double radius, const double frac,
		 bool check,
		 const Tessellation&,
		 const PhysicalGeometry&,
		 const string);

  void operator()(hdsim& sim);

private:
  const double radius_;
  const double frac_;
  bool check_;
  const Tessellation& tess_;
  const PhysicalGeometry& pg_;
  const string fname_;
  
};

#endif // MAKEASPHERICAL_HPP
