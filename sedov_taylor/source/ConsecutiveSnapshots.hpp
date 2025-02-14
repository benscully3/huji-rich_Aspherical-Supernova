#ifndef CONSECUTIVE_SNAPSHOTS_HPP
#define CONSECUTIVE_SNAPSHOTS_HPP 1

#include "source/newtonian/test_1d/main_loop_1d.hpp"

using namespace simulation1d;

class ConsecutiveSnapshots: public DiagnosticsFunction
{
public:
	
  ConsecutiveSnapshots(double dt);
		
  void operator()(const hdsim1D& sim);

  void diagnose(const hdsim1D& sim);
	
private:
  const double dt_;
  mutable double t_next_;
  mutable int index_;
};

#endif // CONSECUTIVE_SNAPSHOTS_HPP
