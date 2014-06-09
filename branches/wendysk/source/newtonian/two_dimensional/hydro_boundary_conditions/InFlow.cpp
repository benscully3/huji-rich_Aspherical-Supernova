#include "InFlow.hpp"
#include "../../../misc/universal_error.hpp"

Primitive InFlow::GetBoundaryPrimitive(Edge const& /*edge*/,
	Tessellation const* /*Data*/,vector<Primitive> const& /*cells*/,
	double /*time*/)const
{
	return outer_;
}

vector<double> InFlow::GetBoundaryTracers(Edge const& /*edge*/,Tessellation const* /*Data*/,
	vector<vector<double> > const& /*tracers*/,double /*time*/)const
{
	return outer_tracer_;
}

InFlow::InFlow(Primitive const& InFlux,
	RiemannSolver const& rs,vector<double> outer_tracer):
outer_(InFlux), rs_(rs),outer_tracer_(outer_tracer) {}

InFlow::~InFlow() {}

Conserved InFlow::CalcFlux
	(Tessellation const* tessellation,
	vector<Primitive> const& cells,Vector2D const& edge_velocity,
	Edge const& edge,
	SpatialReconstruction const* interp,double dt,
	double /*time*/)const
{
	const bool cond1 = (edge.GetNeighbor(0)==-1)&&(edge.GetNeighbor(1)!=-1);
	const bool cond2 = (edge.GetNeighbor(1)==-1)&&(edge.GetNeighbor(0)!=-1);
	if(!(cond1||cond2))
	  throw UniversalError("Boundary condition called for bulk cell");

	Vector2D p = Parallel(edge);
	Vector2D n = Normal(edge, tessellation);
	vector<Primitive> states(2);
	for(int i=0;i<2;i++){
		if(IsGhostCell(edge.GetNeighbor(i),tessellation))
			states[i] = outer_;
		else
			states[i] = interp->Interpolate(tessellation,cells,dt,edge,i,InBulk);
		states[i].Velocity.Set
			(Projection(states[i].Velocity, n),
			Projection(states[i].Velocity, p));
	}
	Conserved res = rs_.Solve(states[0], states[1],Projection(edge_velocity,n));
	res.Momentum = res.Momentum.x*n/abs(n) +
		res.Momentum.y*p/abs(p);
	return res;
}

Vector2D InFlow::CalcEdgeVelocity(Tessellation const* /*tessellation*/,
	vector<Vector2D> const& /*point_velocities*/,
	Edge const& /*edge*/, double /*time*/) const
{
	Vector2D res(0,0);
	return res;
}

bool InFlow::IsBoundary(Edge const& edge,Tessellation const* tessellation)const
{
	if((edge.GetNeighbor(0)<0)||(edge.GetNeighbor(0)>=tessellation->GetPointNo()))
		return true;
	if((edge.GetNeighbor(1)<0)||(edge.GetNeighbor(1)>=tessellation->GetPointNo()))
		return true;
	return false;
}

bool InFlow::IsGhostCell(int i,Tessellation const* Data) const
{
	if(i>Data->GetPointNo()||i<0)
		return true;
	else
		return false;
}


vector<double> InFlow::CalcTracerFlux(Tessellation const* tessellation,
	vector<vector<double> > const& tracers,double dm,
	Edge const& edge,int /*index*/,double dt,
	double /*time*/,ScalarInterpolation const* interp) const
{
	vector<double> res(tracers[0].size());
	if(IsGhostCell(edge.GetNeighbor(0),tessellation))
	{
		if(dm>0)
			transform(outer_tracer_.begin(),outer_tracer_.end(),
			res.begin(),bind1st(multiplies<double>(),dm*dt*edge.GetLength()));
		else
		{
			res=interp->Interpolate(tessellation,tracers,dt,edge,1,SInBulk);
			transform(res.begin(),res.end(),res.begin(),
				bind1st(multiplies<double>(),dm*dt*edge.GetLength()));
		}
	}
	else
	{
		if(dm<0)
			transform(outer_tracer_.begin(),outer_tracer_.end(),
			res.begin(),bind1st(multiplies<double>(),dm*dt*edge.GetLength()));
		else
		{
			res=interp->Interpolate(tessellation,tracers,dt,edge,0,SInBulk);
			transform(res.begin(),res.end(),res.begin(),
				bind1st(multiplies<double>(),dm*dt*edge.GetLength()));
		}
	}
	return res;
}