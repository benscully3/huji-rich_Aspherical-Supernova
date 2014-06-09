#include "source/newtonian/test_2d/triangle_step.hpp"
#include "source/newtonian/two_dimensional/diagnostics.hpp"
#include "source/newtonian/test_2d/main_loop_2d.hpp"

using namespace std;
using namespace simulation2d;

namespace {

  class WriteConserved: public DiagnosticFunction
  {
  public:

    WriteConserved(string const& fname):
      file_(fname.c_str()) {}

    void diagnose(hdsim const& sim)
    {
      const Conserved temp = total_conserved(sim);
      file_ << temp.Mass << " "
	    << temp.Momentum.x << " "
	    << temp.Momentum.y << " "
	    << temp.Energy << endl;
    }

    ~WriteConserved(void)
    {
      file_.close();
    }

  private:
    ofstream file_;
  };

void write_output(hdsim const& sim)
{
  write_edges_and_neighbors(sim, "edges.txt");
  write_generating_points(sim,"pointpos.txt");
  write_cells_property(sim,"pressure",
		       "pressures.txt");
}
}

int main(void)
{
  TriangleStep sim_data("eulerian");
  hdsim& sim = sim_data.getSim();

  {
    SafeTimeTermination term_cond(0.05,1e6);
    WriteConserved diag("res.txt");
    main_loop(sim, 
	      term_cond, 
	      1,
	      &diag);
  }

  return 0;
}
