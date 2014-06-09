#include "Edge.hpp"
#include <cmath>
#include "../misc/universal_error.hpp"

Edge::Edge(void): 
  vertices(Vector2D(), Vector2D()),
  neighbors(0,0) {}

Edge::~Edge(void){}

Edge::Edge(Edge const& other):
  vertices(other.vertices),
  neighbors(other.neighbors) {}

Edge::Edge(Vector2D const& p1, Vector2D const& p2,
	   int neighbor1, int neighbor2):
  vertices(p1,p2), neighbors(neighbor1, neighbor2) {}

int Edge::GetNeighbor(int index) const
{
  if(index==0)
    return neighbors.first;
  else if(index==1)
    return neighbors.second;
  else{
    UniversalError eo("Invalid index in Edge::GetNeighbor");
    eo.AddEntry("index",index);
    throw eo;
  }
}

double Edge::GetLength(void) const
{
  return abs(vertices.second-vertices.first);
}

void Edge::set_friend(int dim,int data)
{
  if(dim==0)
    neighbors.first = data;
  else
    neighbors.second = data;
}

double DistanceToEdge(Vector2D const& point,Edge const& edge)
{
  Vector2D v=edge.vertices.second-edge.vertices.first;
  Vector2D w=point-edge.vertices.first;
  double c1,c2;
  c1=ScalarProd(v,w);
  if(c1<=0)
    return point.distance(edge.vertices.first);
  c2=ScalarProd(v,v);
  if(c2<=c1)
    return point.distance(edge.vertices.second);
  return point.distance(edge.vertices.first+(c1/c2)*v);
}

bool SegmentIntersection(Edge const&edge1,Edge const&edge2,
			 Vector2D &Intersection,double eps)
{
  bool res=true;
  if(min(edge1.vertices.second.x,edge1.vertices.first.x)>max(edge2.vertices.second.x,edge2.vertices.first.x)||
     min(edge2.vertices.second.x,edge2.vertices.first.x)>max(edge1.vertices.second.x,edge1.vertices.first.x)||
     min(edge1.vertices.second.y,edge1.vertices.first.y)>max(edge2.vertices.second.y,edge2.vertices.first.y)||
     min(edge2.vertices.second.y,edge2.vertices.first.y)>max(edge1.vertices.second.y,edge1.vertices.first.y))
    res=false;
  double d=(edge1.vertices.first.x-edge1.vertices.second.x)*(edge2.vertices.first.y-edge2.vertices.second.y)
    -(edge2.vertices.first.x-edge2.vertices.second.x)*(edge1.vertices.first.y-edge1.vertices.second.y);
  if(d==0)
    return false;
  double xi=((edge2.vertices.first.x-edge2.vertices.second.x)*(edge1.vertices.first.x*edge1.vertices.second.y-
					      edge1.vertices.second.x*edge1.vertices.first.y)-(edge1.vertices.first.x-edge1.vertices.second.x)*
	     (edge2.vertices.first.x*edge2.vertices.second.y-edge2.vertices.second.x*edge2.vertices.first.y))/d;
  double yi=((edge2.vertices.first.y-edge2.vertices.second.y)*(edge1.vertices.first.x*edge1.vertices.second.y-
					      edge1.vertices.second.x*edge1.vertices.first.y)-(edge1.vertices.first.y-edge1.vertices.second.y)*
	     (edge2.vertices.first.x*edge2.vertices.second.y-edge2.vertices.second.x*edge2.vertices.first.y))/d;
  Intersection.Set(xi,yi);
  eps=eps*min(edge1.GetLength(),edge2.GetLength());
  if((xi+eps)<min(edge1.vertices.first.x,edge1.vertices.second.x)||(xi-eps)>max(edge1.vertices.first.x,edge1.vertices.second.x))
    return false;
  if((xi+eps)<min(edge2.vertices.first.x,edge2.vertices.second.x)||(xi-eps)>max(edge2.vertices.first.x,edge2.vertices.second.x))
    return false;
  if((yi+eps)<min(edge1.vertices.first.y,edge1.vertices.second.y)||(yi-eps)>max(edge1.vertices.first.y,edge1.vertices.second.y))
    return false;
  if((yi+eps)<min(edge2.vertices.first.y,edge2.vertices.second.y)||(yi-eps)>max(edge2.vertices.first.y,edge2.vertices.second.y))
    return false;
  return res;
}

Vector2D Parallel(Edge const& edge)
{
  return (edge.vertices.second - edge.vertices.first);
}

void Edge::SetVertex(Vector2D const& vec,int index)
{
  if(index==0)
    vertices.first = vec;
  else
    vertices.second = vec;
}