#include "utils.hpp"

bool is_nan(double x)
{
  int b1 = (x>=0);
  int b2 = (x<0);
  return (1 != b1+b2);
}

vector<double> linspace(double xl, double xh, int n)
{
  vector<double> res(n,0);
  for(size_t i=0;i<size_t(n);++i)
    res[i] = xl + (xh-xl)*(double)i/(double)(n-1);
  return res;
}

double min(vector<double> const& v)
{
  double res = v[0];
  for(int i=1;i<(int)v.size();++i)
    res = std::min(res,v[size_t(i)]);
  return res;
}

double max(vector<double> const& v)
{
  double res = v[0];
  for(int i=1;i<(int)v.size();++i)
    res = std::max(res,v[size_t(i)]);
  return res;
}