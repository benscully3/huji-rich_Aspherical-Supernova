#include "VoronoiMesh.hpp"
#include <cmath>

namespace {

  void sortvectors(vector<vector<int> > &input)
  {
    int n=(int)input.size();
    for(int i=0;i<n;++i)
      {
	if(!input[i].empty())
	  sort(input[i].begin(),input[i].end());
      }
  }

#ifdef RICH_MPI
  // Send/Recv points from other processors and moves periodic points.
  // Returns the new points as well as the self index of the points that where kept
  // and the indeces of the sent points
  vector<Vector2D> UpdateMPIPoints(Tessellation const& vproc,int rank,
				   vector<int> const& cornerproc,vector<int> const& proclist,vector<Vector2D>
				   const& points,OuterBoundary const* obc,vector<int> &selfindex,
				   vector<int> &sentproc,vector<vector<int> > &sentpoints)
  {
    vector<Vector2D> res;
    res.reserve(points.size());
    selfindex.clear();
    int npoints=(int)points.size();
    int nproc=vproc.GetPointNo();
    const double dx=obc->GetGridBoundary(Right)-obc->GetGridBoundary(Left);
    const double dy=obc->GetGridBoundary(Up)-obc->GetGridBoundary(Down);
    vector<Vector2D> cproc;
    ConvexHull(cproc,&vproc,rank);
    int ncorner=(int)cornerproc.size();
    vector<vector<Vector2D> > cornerpoints;
    vector<int> realcornerproc;
    for(int i=0;i<ncorner;++i)
      {
	if(cornerproc[i]>-1)
	  {
	    realcornerproc.push_back(vproc.GetOriginalIndex(cornerproc[i]));
	    cornerpoints.push_back(vector<Vector2D> ());
	    ConvexHull(cornerpoints.back(),&vproc,realcornerproc.back());
	  }
      }
    ncorner=(int)realcornerproc.size();
    vector<vector<Vector2D> > cornersend(cornerpoints.size());
    int nneigh=(int)proclist.size();
    vector<vector<Vector2D> > neighpoints;
    vector<int> realneighproc;
    for(int i=0;i<nneigh;++i)
      {
	if(proclist[i]>-1)
	  {
	    realneighproc.push_back(vproc.GetOriginalIndex(proclist[i]));
	    neighpoints.push_back(vector<Vector2D> ());
	    ConvexHull(neighpoints.back(),&vproc,realneighproc.back());
	  }
      }
    nneigh=(int)realneighproc.size();
    sentpoints.clear();
    sentproc=realneighproc;
    sentproc.insert(sentproc.end(),realcornerproc.begin(),realcornerproc.end());
    sentpoints.resize(sentproc.size());
    vector<vector<Vector2D> > neighsend(realneighproc.size());
    int pointoutside=0;
    for(int i=0;i<npoints;++i)
      {
	Vector2D temp(points[i]);
	if(obc->GetBoundaryType()==Periodic)
	  {
	    if(temp.x>obc->GetGridBoundary(Right))
	      temp.x-=dx;
	    if(temp.x<obc->GetGridBoundary(Left))
	      temp.x+=dx;
	    if(temp.y>obc->GetGridBoundary(Up))
	      temp.y-=dy;
	    if(temp.y<obc->GetGridBoundary(Down))
	      temp.y+=dy;
	  }
	if(obc->GetBoundaryType()==HalfPeriodic)
	  {
	    if(temp.x>obc->GetGridBoundary(Right))
	      temp.x-=dx;
	    if(temp.x<obc->GetGridBoundary(Left))
	      temp.x+=dx;
	  }
	if(PointInCell(cproc,temp))
	  {
	    res.push_back(temp);
	    selfindex.push_back(i);
	    continue;
	  }
	bool good=false;
	for(int j=0;j<nneigh;++j)
	  {
	    if(PointInCell(neighpoints[j],temp))
	      {
		neighsend[j].push_back(temp);
		sentpoints[j].push_back(i);
		good=true;
		break;
	      }
	  }
	if(good)
	  continue;
	for(int j=0;j<ncorner;++j)
	  {
	    if(PointInCell(cornerpoints[j],temp))
	      {
		cornersend[j].push_back(temp);
		sentpoints[j+nneigh].push_back(i);
		good=true;
		break;
	      }
	  }
	if(good)
	  continue;
	for(int j=0;j<nproc;++j)
	  {
	    vector<Vector2D> cellpoints;
	    ConvexHull(cellpoints,&vproc,j);
	    if(PointInCell(cellpoints,temp))
	      {
		realneighproc.insert(realneighproc.end(),j);
		vector<int> vtemp;
		vtemp.push_back(i);
		sentpoints.insert(sentpoints.end(),vtemp);
		vector<Vector2D> v2temp;
		v2temp.push_back(temp);
		neighsend.insert(neighsend.end(),v2temp);
		good=true;
		pointoutside=1;
		break;
	      }
	  }
	if(good)
	  continue;
	voronoi_loggers::BinLogger log("verror.bin");
	log.output(vproc);
	UniversalError eo("Point is not inside any processor");
	eo.AddEntry("CPU rank",rank);
	eo.AddEntry("Point number",i);
	eo.AddEntry("Point x cor",points[i].x);
	eo.AddEntry("Point y cor",points[i].y);
	for(int k=0;k<nneigh;++k)
	  eo.AddEntry("Neighbor "+int2str(k)+" is cpu "
		      ,(double)realneighproc[k]);
	for(int k=0;k<ncorner;++k)
	  eo.AddEntry("Corner "+int2str(k)+" is cpu "
		      ,(double)realcornerproc[k]);
	throw eo;
      }
    // Send/Recv the points
    vector<int> procorder=GetProcOrder(rank,nproc);
    // Combine the vectors
    vector<int> neightemp;
    vector<vector<int> > senttemp;
    int ntemp=(int)realneighproc.size();
    vector<vector<Vector2D> > tosend;
    int pointoutside2;
    MPI_Allreduce(&pointoutside,&pointoutside2,1,MPI_INT,MPI_MAX,MPI_COMM_WORLD);
    if(pointoutside2==1)
      {
	neightemp.reserve(nproc-1);
	senttemp.resize(nproc-1);
	tosend.resize(nproc-1);
	for(int i=0;i<nproc;++i)
	  if(i!=rank)
	    neightemp.push_back(i);
	int ntemp2=(int)realneighproc.size();
	int ntemp=(int)realcornerproc.size();
	for(int i=0;i<(ntemp2+ntemp);++i)
	  {
	    if(i<nneigh)
	      {
		if(realneighproc[i]>rank)
		  {
		    if(!sentpoints[i].empty())
		      {
			senttemp[realneighproc[i]-1].insert(senttemp[realneighproc[i]-1].end(),
							    sentpoints[i].begin(),sentpoints[i].end());
			tosend[realneighproc[i]-1].insert(tosend[realneighproc[i]-1].end(),
							  neighsend[i].begin(),neighsend[i].end());
		      }
		  }
		else
		  {
		    if(!sentpoints[i].empty())
		      {
			senttemp[realneighproc[i]].insert(senttemp[realneighproc[i]].end(),
							  sentpoints[i].begin(),sentpoints[i].end());
			tosend[realneighproc[i]].insert(tosend[realneighproc[i]].end(),
							neighsend[i].begin(),neighsend[i].end());
		      }
		  }
	      }
	    else
	      {
		if(i<(nneigh+ntemp)) // corners
		  {
		    if(realcornerproc[i-nneigh]>rank)
		      {
			if(!sentpoints[i].empty())
			  {
			    senttemp[realcornerproc[i-nneigh]-1].insert(	senttemp[
											 realcornerproc[i-nneigh]-1].end(),sentpoints[i].begin(),sentpoints[i].end());
			    tosend[realcornerproc[i-nneigh]-1].insert(tosend[realcornerproc[i-nneigh]-1].end(),
								      cornersend[i-nneigh].begin(),cornersend[i-nneigh].end());
			  }
		      }
		    else
		      {
			if(!sentpoints[i].empty())
			  {
			    senttemp[realcornerproc[i-nneigh]].insert(senttemp[
									       realcornerproc[i-nneigh]].end(),sentpoints[i].begin(),sentpoints[i].end());
			    tosend[realcornerproc[i-nneigh]].insert(tosend[realcornerproc[i-nneigh]].end(),
								    cornersend[i-nneigh].begin(),cornersend[i-nneigh].end());
			  }
		      }
		  }
		else // the extra points
		  {
		    if(realneighproc[i-ntemp]>rank)
		      {
			if(!sentpoints[i].empty())
			  {
			    senttemp[realneighproc[i-ntemp]-1].insert(senttemp[realneighproc[i-ntemp]-1].end(),
								      sentpoints[i].begin(),sentpoints[i].end());
			    tosend[realneighproc[i-ntemp]-1].insert(tosend[realneighproc[i-ntemp]-1].end(),
								    neighsend[i-ntemp].begin(),neighsend[i-ntemp].end());
			  }
		      }
		    else
		      {
			if(!sentpoints[i].empty())
			  {
			    senttemp[realneighproc[i-ntemp]].insert(senttemp[realneighproc[i-ntemp]].end(),
								    sentpoints[i].begin(),sentpoints[i].end());
			    tosend[realneighproc[i-ntemp]].insert(tosend[realneighproc[i-ntemp]].end(),
								  neighsend[i-ntemp].begin(),neighsend[i-ntemp].end());
			  }
		      }
		  }
	      }	
	  }
      }
    else
      {
	for(int i=0;i<ntemp;++i)
	  {
	    neightemp.push_back(realneighproc[i]);
	    senttemp.push_back(sentpoints[i]);
	    tosend.push_back(neighsend[i]);
	  }
	ntemp=(int)realcornerproc.size();
	for(int i=0;i<ntemp;++i)
	  {
	    int index=find(realneighproc.begin(),realneighproc.end(),realcornerproc[i])
	      -realneighproc.begin();
	    if(index<(int)realneighproc.size())
	      {
		senttemp[index].insert(senttemp[index].begin(),sentpoints[i+nneigh].begin(),
				       sentpoints[i+nneigh].end());
		tosend[index].insert(tosend[index].end(),cornersend[i].begin(),
				     cornersend[i].end());
	      }
	    else
	      {
		neightemp.push_back(realcornerproc[i]);
		senttemp.push_back(sentpoints[i+nneigh]);
		tosend.push_back(cornersend[i]);
	      }
	  }
      }
    sentpoints=senttemp;
    sentproc=neightemp;
    vector<Vector2D> toadd=MPI_MassSendRecvVectorVector2D(tosend,sentproc,procorder);
    res.insert(res.end(),toadd.begin(),toadd.end());
    return res;
  }
#endif

  vector<Vector2D> UpdatePoints(vector<Vector2D> const& points,
				OuterBoundary const* obc)
  {
    if(obc->GetBoundaryType()==Rectengular)
      return points;
    vector<Vector2D> res;
    res.reserve(points.size());
    int npoints=(int)points.size();
    const double dx=obc->GetGridBoundary(Right)-obc->GetGridBoundary(Left);
    const double dy=obc->GetGridBoundary(Up)-obc->GetGridBoundary(Down);
    for(int i=0;i<npoints;++i)
      {
	Vector2D temp(points[i]);
	if(obc->GetBoundaryType()==Periodic)
	  {
	    if(temp.x>obc->GetGridBoundary(Right))
	      temp.x-=dx;
	    if(temp.x<obc->GetGridBoundary(Left))
	      temp.x+=dx;
	    if(temp.y>obc->GetGridBoundary(Up))
	      temp.y-=dy;
	    if(temp.y<obc->GetGridBoundary(Down))
	      temp.y+=dy;
	  }
	if(obc->GetBoundaryType()==HalfPeriodic)
	  {
	    if(temp.x>obc->GetGridBoundary(Right))
	      temp.x-=dx;
	    if(temp.x<obc->GetGridBoundary(Left))
	      temp.x+=dx;
	  }
	res.push_back(temp);
      }
    return res;
  }


  void RemoveDuplicateCorners(vector<int> &cornerproc,vector<vector<int> > 
			      &corners,vector<int> const& proclist,vector<vector<int> > &toduplicate)
  {
    vector<int> newcornerproc;
    vector<vector<int> > newcorners;
    int ncorners=(int)cornerproc.size();
    int nproc=(int)proclist.size();
    for(int i=0;i<ncorners;++i)
      {
	if(cornerproc[i]==-1)
	  continue;
	int index=find(proclist.begin(),proclist.end(),cornerproc[i])-proclist.begin();
	if(index<nproc)
	  {
	    // Add unduplicated data
	    vector<int> toadd;
	    int npoints=(int)corners[i].size();
	    for(int j=0;j<npoints;++j)
	      {
		if(!binary_search(toduplicate[index].begin(),toduplicate[index].end(),
				  corners[i][j]))
		  toadd.push_back(corners[i][j]);
	      }
	    if(!toadd.empty())
	      {
		toduplicate[index].insert(toduplicate[index].end(),toadd.begin(),
					  toadd.end());
		sort(toduplicate[index].begin(),toduplicate[index].end());
	      }
	  }
	else
	  {
	    // copy the data
	    newcornerproc.push_back(cornerproc[i]);
	    newcorners.push_back(corners[i]);
	  }
      }
    // Remove same CPUs
    if(newcornerproc.size()>1)
      {
	int n=(int)newcornerproc.size();
	vector<int> index;
	sort_index(newcornerproc,index);
	sort(newcornerproc.begin(),newcornerproc.end());
	vector<int> temp=unique(newcornerproc);
	int nuinq=(int)temp.size();
	vector<vector<int> > cornerstemp(temp.size());
	for(int i=0;i<n;++i)
	  {
	    int place=find(temp.begin(),temp.end(),newcornerproc[i])-temp.begin();
	    cornerstemp[place].insert(cornerstemp[place].begin(),
				      newcorners[index[i]].begin(),newcorners[index[i]].end());
	  }
	for(int i=0;i<nuinq;++i)
	  {
	    sort(cornerstemp[i].begin(),cornerstemp[i].end());
	    cornerstemp[i]=unique(cornerstemp[i]);
	  }
	newcornerproc=temp;
	newcorners=cornerstemp;
      }
    cornerproc=newcornerproc;
    corners=newcorners;
  }

  Vector2D GetPeriodicDiff(Edge const& edge,OuterBoundary const* obc)
  {
    Vector2D diff;
    if(abs(edge.vertices.first.x-edge.vertices.second.x)
       <abs(edge.vertices.first.y-edge.vertices.second.y))
      {
	if(abs(edge.vertices.first.x-obc->GetGridBoundary(Left))
	   <abs(edge.vertices.first.x-obc->GetGridBoundary(Right)))
	  diff=Vector2D(obc->GetGridBoundary(Right)-obc->GetGridBoundary(Left),0);
	else
	  diff=Vector2D(-obc->GetGridBoundary(Right)+obc->GetGridBoundary(Left),0);
      }
    else
      {
	if(abs(edge.vertices.first.y-obc->GetGridBoundary(Down))
	   <abs(edge.vertices.first.y-obc->GetGridBoundary(Up)))
	  diff=Vector2D(0,obc->GetGridBoundary(Up)-obc->GetGridBoundary(Down));
	else
	  diff=Vector2D(0,-obc->GetGridBoundary(Up)+obc->GetGridBoundary(Down));
      }
    return diff;
  }

  vector<int> GetCornerNeighbors(Tessellation const& v,int rank)
  {
    vector<int> neighbors=v.GetNeighbors(rank);
    sort(neighbors.begin(),neighbors.end());
    int n=(int)neighbors.size();
    vector<int> minremove(1,-1);
    neighbors=RemoveList(neighbors,minremove);
    // Arrange the corners accordingly
    vector<int> edgeindex;
    ConvexEdges(edgeindex,&v,rank);
    vector<int> result(n);
    for(int i=0;i<n;++i)
      {
	int other=v.GetEdge(edgeindex[i]).neighbors.first;
	if(other==rank)
	  other=v.GetEdge(edgeindex[i]).neighbors.second;
	int nextneigh=v.GetEdge(edgeindex[(i+1)%n]).neighbors.first;
	if(nextneigh==rank)
	  nextneigh=v.GetEdge(edgeindex[(i+1)%n]).neighbors.second;
	if(other==-1&&nextneigh==-1)
	  {
	    result[i]=-1;
	    continue;
	  }
	if(other==-1||nextneigh==-1)
	  {
	    vector<int> nextedges;
	    (nextneigh==-1)?ConvexEdges(nextedges,&v,other):ConvexEdges(nextedges,&v,nextneigh);
	    int nedges=(int) nextedges.size();
	    int counter=0;
	    for(int k=0;k<nedges;++k)
	      {
		Edge e=v.GetEdge(nextedges[k]);
		if(e.neighbors.first==rank||e.neighbors.second==rank)
		  {
		    counter=k;
		    break;
		  }
	      }
	    Edge nextedge=(other==-1)?v.GetEdge(nextedges[(counter+2)%nedges])
	      :v.GetEdge(nextedges[(counter-2+nedges)%nedges]);
	    if(nextneigh==-1)
	      result[i]=(nextedge.neighbors.first==other)?
		nextedge.neighbors.second:nextedge.neighbors.first;
	    else
	      result[i]=(nextedge.neighbors.first==nextneigh)?
		nextedge.neighbors.second:nextedge.neighbors.first;
	  }
	else
	  {
	    vector<int> otherneigh=v.GetNeighbors(nextneigh);
	    vector<int> myneigh=v.GetNeighbors(other);
	    sort(otherneigh.begin(),otherneigh.end());
	    sort(myneigh.begin(),myneigh.end());
	    int N=(int)myneigh.size();
	    int j;
	    result[i]=-1;
	    vector<int> candidates;
	    vector<double> dist;
	    for(j=0;j<N;++j)
	      {
		if(myneigh[j]==rank||myneigh[j]==-1)
		  continue;
		if(binary_search(otherneigh.begin(),otherneigh.end(),myneigh[j]))
		  if(!binary_search(neighbors.begin(),neighbors.end(),myneigh[j]))
		    {
		      candidates.push_back(myneigh[j]);
		      dist.push_back(v.GetMeshPoint(myneigh[j]).distance(
									 v.GetMeshPoint(rank)));
		    }
	      }
	    if(!dist.empty())
	      {
		int place=min_element(dist.begin(),dist.end())-dist.begin();
		result[i]=candidates[place];
	      }
	  }
      }
    return result;
  }

  int SumV(vector<vector<int> > const& v)
  {
    int res=0;
    int n=(int)v.size();
    for(int i=0;i<n;++i)
      {
	res+=(int)v[i].size();
      }
    return res;
  }

  template <typename T>
  bool EmptyVectorVector(vector<vector<T> > const& v)
  {
    int n=(int)v.size();
    for(int i=0;i<n;++i)
      {
	if(!v[i].empty())
	  return false;
      }
    return true;
  }

  template <typename T>
  vector<vector<T> > CombineVectorVector(vector<vector<T> > const& v1,
					 vector<vector<T> > const& v2)
  {
    vector<vector<T> > res(v1);
    if(v1.size()!=v2.size())
      {
	cout<<"Error in CombineVectorVector, vectors not same length"<<endl;
	throw;
      }
    int n=(int)v1.size();
    for(int i=0;i<n;++i)
      {
	if(!v2[i].empty())
	  res[i].insert(res[i].end(),v2[i].begin(),v2[i].end());
      }
    return res;
  }

  UniversalError negative_volume_ratio(VoronoiMesh const& V,int index,
				       vector<int> const& ToRemove)
  {
    UniversalError eo("Negative volume ratio in cell refine");
    vector<int> bad_edgesindex=V.GetCellEdges(index);
    for(int jj=0;jj<(int)bad_edgesindex.size();++jj)
      {
	Edge e=V.GetEdge(bad_edgesindex[jj]);
	eo.AddEntry("Edge X cor",e.vertices.first.x);
	eo.AddEntry("Edge Y cor",e.vertices.first.y);
	eo.AddEntry("Edge X cor",e.vertices.second.x);
	eo.AddEntry("Edge Y cor",e.vertices.second.y);
	eo.AddEntry("Edge length",e.GetLength());
	eo.AddEntry("Edge neighbor 0",e.neighbors.first);
	eo.AddEntry("Edge neighbor 1",e.neighbors.second);
      }
    for(int i=0;i<(int)ToRemove.size();++i)
      {
	eo.AddEntry("ToRemove Index",ToRemove[i]);
	eo.AddEntry("ToRemove x cor",V.GetMeshPoint(ToRemove[i]).x);
	eo.AddEntry("ToRemove y cor",V.GetMeshPoint(ToRemove[i]).y);
      }
    return eo;
  }
}


VoronoiMesh::VoronoiMesh(vector<Vector2D> const& points,OuterBoundary const& bc):
  Nextra(0),
  logger(0),
  eps(1e-8),
  obc(0),
  cell_edges(vector<Edge> ()),
  edges(vector<Edge>()),
  CM(vector<Vector2D> ()),
  mesh_vertices(vector<vector<int> >()),
  GhostPoints(vector<vector<int> > ()),
  GhostProcs(vector<int> ()),
  SentPoints(vector<vector<int> > ()),
  SentProcs(vector<int> ()),
  selfindex(vector<int> ()),
  Tri(0)
{
  Initialise(points,&bc);
}

VoronoiMesh::VoronoiMesh(vector<Vector2D> const& points,Tessellation const& proctess,
			 OuterBoundary const& bc):
  Nextra(0),
  logger(0),
  eps(1e-8),
  obc(0),
  cell_edges(vector<Edge> ()),
  edges(vector<Edge>()),
  CM(vector<Vector2D> ()),
  mesh_vertices(vector<vector<int> >()),
  GhostPoints(vector<vector<int> > ()),
  GhostProcs(vector<int> ()),
  SentPoints(vector<vector<int> > ()),
  SentProcs(vector<int> ()),
  selfindex(vector<int> ()),
  Tri(0)
{
  Initialise(points,proctess,&bc);
}

vector<int> VoronoiMesh::AddPointsAlongEdge(int point,vector<vector<int> > const&copied,
					    int side)
{
  int ncopy=(int)copied[side].size();
  Vector2D vec=Tri->get_point(point);
  vector<double> dist(ncopy);
  for(int i=0;i<ncopy;++i)
    dist[i]=vec.distance(Tri->get_point(copied[side][i]));
  const int copylength=min(7,(int)copied[side].size()-1);
  vector<int> index,toadd(copylength);
  sort_index(dist,index);
  for(int i=0;i<copylength;++i)
    toadd[i]=copied[side][index[i+1]];
  return toadd;
}

Vector2D VoronoiMesh::CalcFaceVelocity(Vector2D wl, Vector2D wr,Vector2D rL, Vector2D rR,
				       Vector2D f)const
{
  Vector2D wprime = ScalarProd(wl-wr,f-(rR+rL)/2)*	(rR-rL)/pow(abs(rR-rL),2);
  return 0.5*(wl+wr) + wprime;
}

vector<Vector2D> VoronoiMesh::calc_edge_velocities(HydroBoundaryConditions const* hbc,
						   vector<Vector2D> const& point_velocities,double time)const
{
  vector<Vector2D> facevelocity;
  facevelocity.resize(edges.size());
  for(int i = 0; i < (int)edges.size(); ++i)
    {
      if(hbc->IsBoundary(edges[i],*this))
	{
	  // Boundary
	  facevelocity[i] = hbc->CalcEdgeVelocity(*this,point_velocities,edges[i],
						  time);
	}
      else
	{
	  // Bulk
	  facevelocity[i] = CalcFaceVelocity(
					     point_velocities[edges[i].neighbors.first],
					     point_velocities[edges[i].neighbors.second],
					     GetMeshPoint(edges[i].neighbors.first),
					     GetMeshPoint(edges[i].neighbors.second),
					     0.5*(edges[i].vertices.first+edges[i].vertices.second));
	}
    }
  return facevelocity;
}

bool VoronoiMesh::NearBoundary(int index) const
{
  int n=int(mesh_vertices[index].size());
  int n1,n0;
  int N=Tri->get_length();
  for(int i=0;i<n;++i)
    {
      n0=edges[mesh_vertices[index][i]].neighbors.first;
      n1=edges[mesh_vertices[index][i]].neighbors.second;
      if(n0<0||n1<0||n0>=N||n1>=N)
	return true;
    }
  return false;
}

VoronoiMesh::~VoronoiMesh(void)
{
  delete Tri;
  edges.clear();
  mesh_vertices.clear();
}

int VoronoiMesh::GetOriginalIndex(int point) const
{
  int npoints=GetPointNo();
  if(point<npoints)
    return point;
  else
    {
#ifdef RICH_MPI
      return point;
#endif
      int counter=0;
      if(point<Nextra)
	{
	  UniversalError eo("Tried to get original index of non exsistent cell");
	  eo.AddEntry("Tried accessing cell",point);
	  throw eo;
	}
      int maxcor=(int)Tri->ChangeCor().size();
      int cumulative=Nextra;
      while(cumulative<maxcor)
	{
	  int temp=(int)GhostPoints[counter].size();
	  if((cumulative+temp)<=point)
	    {
	      cumulative+=temp;
	      ++counter;
	    }
	  else
	    {
	      return GhostPoints[counter][point-cumulative];
	    }
	}
      UniversalError eo("Tried to get original index of non exsistent cell");
      eo.AddEntry("Tried accessing cell",point);
      throw eo;
    }
}

vector<int> VoronoiMesh::GetSelfPoint(void)const
{
  return selfindex;
}

VoronoiMesh::VoronoiMesh():
  Nextra(0),
  logger(0),
  eps(1e-8),
  obc(0),
  cell_edges(vector<Edge> ()),
  edges(vector<Edge>()),
  CM(vector<Vector2D> ()),
  mesh_vertices(vector<vector<int> >()),
  GhostPoints(vector<vector<int> > ()),
  GhostProcs(vector<int> ()),
  SentPoints(vector<vector<int> > ()),
  SentProcs(vector<int> ()),
  selfindex(vector<int> ()),
  Tri(0)  {}

VoronoiMesh::VoronoiMesh(VoronoiMesh const& other):
  GhostPoints(other.GhostPoints),GhostProcs(other.GhostProcs),
  SentPoints(other.SentPoints),SentProcs(other.SentProcs),
  selfindex(other.selfindex),
  Nextra(other.Nextra),logger(other.logger),	
  eps(other.eps),
  obc(other.obc),
  cell_edges(other.cell_edges),
  edges(other.edges),
  CM(other.CM),
  mesh_vertices(other.mesh_vertices),
  Tri(new Delaunay(*other.Tri)) {}

void VoronoiMesh::build_v()
{
  vector<int>::iterator it;
  Vector2D center,center_temp;
  int j;
  facet *to_check;
  Edge edge_temp;
  Vector2D p_temp;
  mesh_vertices.clear();
  mesh_vertices.resize(Tri->get_length());
  edges.reserve((int)(Tri->get_length()*3.5));
  int N=Tri->GetOriginalLength();
  for(int i=0;i<N;++i)
    mesh_vertices[i].reserve(7);
  int Nfacets=Tri->get_num_facet();
  vector<Vector2D> centers(Nfacets);
  for(int i=0;i<Nfacets;++i)
    centers[i]=get_center(i);
  for(int i=0;i<Nfacets;++i)
    {
      center=centers[i];
      to_check=Tri->get_facet(i);
      for(j=0;j<3;++j)
	{
	  if(to_check->get_friend(j)==Tri->get_last_loc())
	    continue;
	  if(to_check->get_friend(j)<i)
	    continue;
	  center_temp=centers[to_check->get_friend(j)];
	  {
	    edge_temp.vertices.first = center;
	    edge_temp.vertices.second = center_temp;
	    edge_temp.neighbors.first = to_check->get_vertice(j);
	    edge_temp.neighbors.second = to_check->get_vertice((j+1)%3);

	    int n0=edge_temp.neighbors.first;
	    int n1=edge_temp.neighbors.second;

	    if(legal_edge(&edge_temp))
	      {
		// I added a change here, if edge has zero length I don't add it.
		if(edge_temp.GetLength()>eps*sqrt(Tri->GetFacetRadius(i)*
						  Tri->GetFacetRadius(to_check->get_friend(j))))
		  {
		    {
		      if(edge_temp.neighbors.first<Tri->GetOriginalLength())
			mesh_vertices[edge_temp.neighbors.first].push_back((int)edges.size());
		      else
			if(obc->PointIsReflective(Tri->get_point(
								 edge_temp.neighbors.first)))
			  edge_temp.neighbors.first = -1;
		      if(edge_temp.neighbors.second<Tri->GetOriginalLength())
			mesh_vertices[edge_temp.neighbors.second].push_back((int)edges.size());
		      else
			if(obc->PointIsReflective(Tri->get_point(
								 edge_temp.neighbors.second)))
			  edge_temp.neighbors.second = -1;
		      edges.push_back(edge_temp);
		    }
		  }
	      }
	  }
	}
    }
}

void VoronoiMesh::Initialise(vector<Vector2D>const& pv,OuterBoundary const* _bc)
{
  obc=_bc;
  //Build the delaunay
  delete Tri;
  Tri=new Delaunay;
  vector<Vector2D> procpoints;
  procpoints.push_back(Vector2D(_bc->GetGridBoundary(Left),_bc->GetGridBoundary(Down)));
  procpoints.push_back(Vector2D(_bc->GetGridBoundary(Right),_bc->GetGridBoundary(Down)));
  procpoints.push_back(Vector2D(_bc->GetGridBoundary(Right),_bc->GetGridBoundary(Up)));
  procpoints.push_back(Vector2D(_bc->GetGridBoundary(Left),_bc->GetGridBoundary(Up)));
  Tri->build_delaunay(pv,procpoints);
  eps=1e-8;
  Nextra=0;
  edges.clear();
  GhostPoints.clear();
  GhostProcs.clear();
  build_v();

  if(logger)
    logger->output(*this);
  vector<vector<int> > toduplicate;
  vector<Edge> box_edges=GetBoxEdges();
  cell_edges=box_edges;
  FindIntersectingPoints(box_edges,toduplicate);
  vector<vector<int> > corners;
  vector<vector<int> > totest;
  if(_bc->GetBoundaryType()==Rectengular)
    {
      for(int i=0;i<4;++i)
	RigidBoundaryPoints(toduplicate[i],box_edges[i]);
      Nextra=(int)Tri->ChangeCor().size();
    }
  else
    {
      if(_bc->GetBoundaryType()==HalfPeriodic)
	{
	  for(int i=0;i<2;++i)
	    RigidBoundaryPoints(toduplicate[i],box_edges[i]);
	  Nextra=(int)Tri->ChangeCor().size();
	  vector<vector<int> > tempduplicate;
	  tempduplicate.push_back(toduplicate[2]);
	  tempduplicate.push_back(toduplicate[3]);
	  for(int i=0;i<2;++i)
	    {
	      PeriodicBoundaryPoints(tempduplicate[i],i);
	      GhostPoints.push_back(tempduplicate[i]);
	      GhostProcs.push_back(-1);
	    }
	  toduplicate[2]=tempduplicate[0];
	  toduplicate[3]=tempduplicate[1];
	  sortvectors(toduplicate);
	  GetToTest(toduplicate,totest);
	}
      else
	{
	  GetToTest(toduplicate,totest);
	  Nextra=(int)Tri->ChangeCor().size();
	  for(int i=0;i<4;++i)
	    PeriodicBoundaryPoints(toduplicate[i],i);
	  for(int i=0;i<4;++i)
	    {
	      GhostPoints.push_back(toduplicate[i]);
	      GhostProcs.push_back(-1);
	    }
	  sortvectors(toduplicate);
	}
    }

  if(_bc->GetBoundaryType()==Periodic)
    {
      GetCorners(toduplicate,corners);
      for(int i=0;i<4;++i)
	CornerBoundaryPoints(corners[i],i);
      for(int i=0;i<4;++i)
	{
	  GhostPoints.push_back(corners[i]);
	  GhostProcs.push_back(-1);
	}
    }
  edges.clear();
  build_v();
  vector<vector<int> > moreduplicate;
  if(_bc->GetBoundaryType()!=Rectengular)
    {
      // toduplicate is what I've copied
      GetAdditionalBoundary(toduplicate,moreduplicate,totest);
      GetToTest(moreduplicate,totest);
      for(int i=0;i<(int)totest.size();++i)
	PeriodicBoundaryPoints(moreduplicate[i],i);
      for(int i=0;i<(int)totest.size();++i)
	{
	  GhostPoints.push_back(moreduplicate[i]);
	  GhostProcs.push_back(-1);
	}
      sortvectors(moreduplicate);
      edges.clear();
      build_v();
      if(!EmptyVectorVector(moreduplicate))
	{
	  vector<vector<int> > temp(CombineVectorVector(toduplicate,moreduplicate));
	  GetAdditionalBoundary(temp,moreduplicate,totest);
	  for(int i=0;i<(int)totest.size();++i)
	    PeriodicBoundaryPoints(moreduplicate[i],i);
	  for(int i=0;i<(int)totest.size();++i)
	    {
	      GhostPoints.push_back(moreduplicate[i]);
	      GhostProcs.push_back(-1);
	    }
	  edges.clear();
	  build_v();
	}
    }
  int n=GetPointNo();
  CM.resize(n);
  for(int i=0;i<n;++i)
    CM[i]=CalcCellCM(i);
}


void VoronoiMesh::Initialise(vector<Vector2D>const& pv,Tessellation const& vproc,
			     OuterBoundary const* outer)
{
#ifdef RICH_MPI
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  obc=outer;
  vector<int> cedges;
  ConvexEdges(cedges,&vproc,rank);
  cell_edges.clear();
  for(int i=0;i<(int)cedges.size();++i)
    cell_edges.push_back(vproc.GetEdge(cedges[i]));

  // Get the convex hull of the cell
  vector<Vector2D> cpoints;
  ConvexHull(cpoints,&vproc,rank);
  //Build the delaunay
  delete Tri;
  Tri=new Delaunay;
  Tri->build_delaunay(pv,cpoints);
  eps=1e-8;
  edges.clear();
  GhostPoints.clear();
  GhostProcs.clear();
  selfindex.resize(pv.size());
  int npv=(int)pv.size();
  for(int i=0;i<npv;++i)
    selfindex[i]=i;
  build_v();

  if(logger)
    logger->output(*this);
  vector<vector<int> > toduplicate;
  FindIntersectingPoints(cell_edges,toduplicate);
  vector<vector<int> > corners;
  vector<vector<int> > totest;
  vector<int> cornerproc=GetCornerNeighbors(vproc,rank);

  GetCorners(toduplicate,corners);
  GetToTest(toduplicate,totest);

  int worldsize;
  MPI_Comm_size(MPI_COMM_WORLD,&worldsize);
  vector<int> procorder=GetProcOrder(rank,worldsize);

  vector<Edge> bedge=GetBoxEdges();
  vector<vector<int> > boxduplicate;
  FindIntersectingPoints(bedge,boxduplicate);
  vector<int> proclist(vproc.GetCellEdges(rank).size());
  for(int i=0;i<(int)proclist.size();++i)
    proclist[i]=(vproc.GetOriginalIndex(cell_edges[i].neighbors.first)==rank)? vproc.GetOriginalIndex(
												     cell_edges[i].neighbors.second):vproc.GetOriginalIndex(cell_edges[i].neighbors.first);
  // Remove box boundaries from to duplicate
  int ndup=(int)toduplicate.size();
  int counter=0;
  vector<vector<int> > todup2,totest2;
  vector<int> proclist2;
  for(int i=0;i<ndup;++i)
    {
      if(proclist[i]!=-1)
	{
	  todup2.push_back(toduplicate[i]);
	  totest2.push_back(totest[i]);
	  proclist2.push_back(proclist[i]);
	}
    }
  toduplicate=todup2;
  totest=totest2;
  proclist=proclist2;
  for(int i=0;i<4;++i)
    RigidBoundaryPoints(boxduplicate[i],bedge[i]);

  NonSendBoundary(proclist,toduplicate,vproc,totest,bedge);
  NonSendCorners(cornerproc,corners,vproc);
  RemoveDuplicateCorners(cornerproc,corners,proclist,toduplicate);
  Nextra=Tri->GetCorSize()-Tri->GetOriginalLength();

  // Send/Recv the boundary points
  vector<int> proclisttemp(proclist);
  vector<vector<int> > toduptemp(toduplicate);
  proclisttemp.insert(proclisttemp.end(),cornerproc.begin(),cornerproc.end());
  toduptemp.insert(toduptemp.end(),corners.begin(),corners.end());
  //SendRecv(procorder,proclist,toduplicate,vproc);
  SendRecv(procorder,proclisttemp,toduptemp,vproc);
  ndup=(int)toduplicate.size();
  for(int i=0;i<(int)toduptemp.size();++i)
    {
      GhostPoints.push_back(toduptemp[i]);
      GhostProcs.push_back(proclisttemp[i]);
    }
  sortvectors(toduplicate);

  edges.clear();
  build_v();
  vector<vector<int> > moreduplicate;

  GetAdditionalBoundary(toduplicate,moreduplicate,totest);
  GetToTest(moreduplicate,totest);
  SendRecv(procorder,proclist,moreduplicate,vproc);
  for(int i=0;i<ndup;++i)
    {
      GhostPoints.push_back(moreduplicate[i]);
      GhostProcs.push_back(proclist[i]);
    }
  sortvectors(toduplicate);
  edges.clear();
  build_v();

  vector<vector<int> > temp(CombineVectorVector(toduplicate,moreduplicate));
  GetAdditionalBoundary(temp,moreduplicate,totest);
  SendRecv(procorder,proclist,moreduplicate,vproc);
  for(int i=0;i<ndup;++i)
    {
      GhostPoints.push_back(moreduplicate[i]);
      GhostProcs.push_back(proclist[i]);
    }

  /*	// Send/Recv the cornerpoints
	SendRecv(procorder,cornerproc,corners,vproc);

	for(int i=0;i<(int)cornerproc.size();++i)
	{
	GhostPoints.push_back(corners[i]);
	GhostProcs.push_back(cornerproc[i]);
	}
  */

  edges.clear();
  build_v();

  int n=GetPointNo();
  CM.resize(n);
  for(int i=0;i<n;++i)
    CM[i]=CalcCellCM(i);
#endif
}


Vector2D VoronoiMesh::get_center(int facet)
// This function calculates the center of the circle surrounding the Facet
{
  Vector2D center;
  double x1=Tri->get_facet_coordinate(facet,0,0);
  double y1=Tri->get_facet_coordinate(facet,0,1);
  double x2=Tri->get_facet_coordinate(facet,1,0);
  double y2=Tri->get_facet_coordinate(facet,1,1);
  double x3=Tri->get_facet_coordinate(facet,2,0);
  double y3=Tri->get_facet_coordinate(facet,2,1);
  // Do we have a case where two point are very close compared to the third?
  double d12=(x1-x2)*(x1-x2)+(y1-y2)*(y1-y2);
  double d23=(x3-x2)*(x3-x2)+(y3-y2)*(y3-y2);
  double d13=(x1-x3)*(x1-x3)+(y1-y3)*(y1-y3);
  int scenario=0;
  if(d12<0.1*(d23+d13))
    scenario=1;
  else
    if(d23<0.1*(d13+d12))
      scenario=3;
    else
      if(d13<0.1*(d23+d12))
	scenario=2;
  switch(scenario)
    {
    case(0):
    case(1):
    case(2):
      {
	x2-=x1;
	x3-=x1;
	y2-=y1;
	y3-=y1;
	double d_inv=1/(2*(x2*y3-y2*x3));
	center.Set((y3*(x2*x2+y2*y2)-y2*(x3*x3+y3*y3))*d_inv+x1,
		   (-x3*(x2*x2+y2*y2)+x2*(x3*x3+y3*y3))*d_inv+y1);
	break;
      }
    case(3):
      {
	x1-=x2;
	x3-=x2;
	y1-=y2;
	y3-=y2;
	double d_inv=1/(2*(x3*y1-y3*x1));
	center.Set((y1*(x3*x3+y3*y3)-y3*(x1*x1+y1*y1))*d_inv+x2,
		   (x3*(x1*x1+y1*y1)-x1*(x3*x3+y3*y3))*d_inv+y2);
	break;
      }
    default:
      throw UniversalError("Unhandled case in switch statement VoronoiMesh::get_center");
    }
  return center;
}

bool VoronoiMesh::legal_edge(Edge *e) //checks if both ends of the edge are outside the grid and that the edge doesn't cross the grid
{
  if((e->neighbors.first<Tri->get_length())||
     (e->neighbors.second<Tri->get_length()))
    return true;
  else
    return false;
}

double VoronoiMesh::GetWidth(int index)const
{
  return sqrt(GetVolume(index)/M_PI);
}

vector<int> const& VoronoiMesh::GetCellEdges(int index) const
{
  return mesh_vertices[index];
}

double VoronoiMesh::GetVolume(int index) const
{
  double x1,x2;
  double y1,y2;
  Vector2D center=Tri->get_point(index);
  double area=0;
  for (size_t i=0;i<mesh_vertices[index].size();++i)
    {
      x1=edges[mesh_vertices[index][i]].vertices.first.x-center.x;
      x2=edges[mesh_vertices[index][i]].vertices.second.x-center.x;
      y1=edges[mesh_vertices[index][i]].vertices.first.y-center.y;
      y2=edges[mesh_vertices[index][i]].vertices.second.y-center.y;
      area+=abs(x2*y1-y2*x1)*0.5;
    }
  return area;
}

Vector2D VoronoiMesh::CalcCellCM(int index) const
{
  double x1,x2;
  double y1,y2;
  double xc=0,yc=0;
  Vector2D center=Tri->get_point(index);
  double area=0,area_temp;
  for (int i=0;i<(int)mesh_vertices[index].size();i++)
    {
      int temp=mesh_vertices[index][i];
      x1=edges[temp].vertices.first.x-center.x;
      x2=edges[mesh_vertices[index][i]].vertices.second.x-center.x;
      y1=edges[mesh_vertices[index][i]].vertices.first.y-center.y;
      y2=edges[mesh_vertices[index][i]].vertices.second.y-center.y;
      area_temp=abs(x2*y1-y2*x1)*0.5;
      area+=area_temp;
      xc+=area_temp*(center.x+edges[temp].vertices.first.x+edges[temp].vertices.second.x)/3;
      yc+=area_temp*(center.y+edges[temp].vertices.first.y+edges[temp].vertices.second.y)/3;
    }
  area=1/area;
  Vector2D p(xc*area,yc*area);
  return p;
}

vector<Vector2D>& VoronoiMesh::GetMeshPoints(void)
{
  return Tri->GetMeshPoints();
}

void VoronoiMesh::Update(vector<Vector2D> const& p)
{
  // Clean_up last step
  edges.clear();
  GhostPoints.clear();
  GhostProcs.clear();
  vector<Vector2D> procpoints;
  procpoints.push_back(Vector2D(obc->GetGridBoundary(Left),obc->GetGridBoundary(Down)));
  procpoints.push_back(Vector2D(obc->GetGridBoundary(Right),obc->GetGridBoundary(Down)));
  procpoints.push_back(Vector2D(obc->GetGridBoundary(Right),obc->GetGridBoundary(Up)));
  procpoints.push_back(Vector2D(obc->GetGridBoundary(Left),obc->GetGridBoundary(Up)));
  vector<Vector2D> points=UpdatePoints(p,obc);
  Tri->update(points,procpoints);
  build_v();
  vector<vector<int> > toduplicate;
  vector<Edge> box_edges=GetBoxEdges();
  FindIntersectingPoints(box_edges,toduplicate);
  vector<vector<int> > corners;
  vector<vector<int> > totest;
  if(obc->GetBoundaryType()==Rectengular)
    {
      for(int i=0;i<4;++i)
	RigidBoundaryPoints(toduplicate[i],box_edges[i]);
      sortvectors(toduplicate);
      Nextra=(int)Tri->ChangeCor().size();
    }
  else
    {
      if(obc->GetBoundaryType()==HalfPeriodic)
	{
	  for(int i=0;i<2;++i)
	    RigidBoundaryPoints(toduplicate[i],box_edges[i]);
	  Nextra=(int)Tri->ChangeCor().size();
	  vector<vector<int> > tempduplicate;
	  tempduplicate.push_back(toduplicate[2]);
	  tempduplicate.push_back(toduplicate[3]);
	  for(int i=0;i<2;++i)
	    {
	      PeriodicBoundaryPoints(tempduplicate[i],i);
	      GhostPoints.push_back(tempduplicate[i]);
	      GhostProcs.push_back(-1);
	    }
	  toduplicate[2]=tempduplicate[0];
	  toduplicate[3]=tempduplicate[1];
	  sortvectors(toduplicate);
	  GetToTest(toduplicate,totest);
	}
      else
	{
	  GetToTest(toduplicate,totest);
	  Nextra=(int)Tri->ChangeCor().size();

	  GetCorners(toduplicate,corners);
	  for(int i=0;i<4;++i)
	    CornerBoundaryPoints(corners[i],i);
	  for(int i=0;i<4;++i)
	    {
	      GhostPoints.push_back(corners[i]);
	      GhostProcs.push_back(-1);
	    }
	  sortvectors(toduplicate);
	  for(int i=0;i<4;++i)
	    PeriodicBoundaryPoints(toduplicate[i],i);
	  for(int i=0;i<4;++i)
	    {
	      GhostPoints.push_back(toduplicate[i]);
	      GhostProcs.push_back(-1);
	    }
	  sortvectors(toduplicate);
	}
    }

  edges.clear();
  build_v();
  vector<vector<int> > moreduplicate;
  if(obc->GetBoundaryType()!=Rectengular)
    {
      // toduplicate is what I've copied
      GetAdditionalBoundary(toduplicate,moreduplicate,totest);
      GetToTest(moreduplicate,totest);
      for(int i=0;i<(int)totest.size();++i)
	PeriodicBoundaryPoints(moreduplicate[i],i);
      for(int i=0;i<(int)totest.size();++i)
	{
	  GhostPoints.push_back(moreduplicate[i]);
	  GhostProcs.push_back(-1);
	}
      sortvectors(moreduplicate);
      edges.clear();
      build_v();
      if(!EmptyVectorVector(moreduplicate))
	{
	  vector<vector<int> > temp(CombineVectorVector(toduplicate,moreduplicate));
	  GetAdditionalBoundary(temp,moreduplicate,totest);
	  for(int i=0;i<(int)totest.size();++i)
	    PeriodicBoundaryPoints(moreduplicate[i],i);
	  for(int i=0;i<(int)totest.size();++i)
	    {
	      GhostPoints.push_back(moreduplicate[i]);
	      GhostProcs.push_back(-1);
	    }

	  edges.clear();
	  build_v();
	}
    }
  int n=GetPointNo();
  CM.resize(n);
  for(int i=0;i<n;++i)
    CM[i]=CalcCellCM(i);
}


void VoronoiMesh::Update(vector<Vector2D> const& p,Tessellation const &vproc)
{
#ifdef RICH_MPI
  // Clean_up last step
  edges.clear();
  GhostPoints.clear();
  GhostProcs.clear();
  selfindex.clear();
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  vector<int> cedges;
  ConvexEdges(cedges,&vproc,rank);
  cell_edges.clear();
  for(int i=0;i<(int)cedges.size();++i)
    cell_edges.push_back(vproc.GetEdge(cedges[i]));

  vector<Vector2D> cproc;
  ConvexHull(cproc,&vproc,rank);
  // Send/Recv points that entered and left domain
  vector<int> cornerproc=GetCornerNeighbors(vproc,rank);
  vector<int> proclist(vproc.GetCellEdges(rank).size());
  for(int i=0;i<(int)proclist.size();++i)
    proclist[i]=(cell_edges[i].neighbors.first==rank)? vproc.GetOriginalIndex(
									     cell_edges[i].neighbors.second):vproc.GetOriginalIndex(cell_edges[i].neighbors.first);
  vector<Vector2D> points=UpdateMPIPoints(vproc,rank,cornerproc,proclist,p,obc,
					  selfindex,SentProcs,SentPoints);

  Tri->update(points,cproc);
  build_v();

  if(logger)
    logger->output(*this);
  vector<vector<int> > toduplicate;
  FindIntersectingPoints(cell_edges,toduplicate);
  vector<vector<int> > corners;
  vector<vector<int> > totest;


  GetCorners(toduplicate,corners);
  GetToTest(toduplicate,totest);

  int worldsize;
  MPI_Comm_size(MPI_COMM_WORLD,&worldsize);
  vector<int> procorder=GetProcOrder(rank,worldsize);
  vector<Edge> bedge=GetBoxEdges();
  vector<vector<int> > boxduplicate;
  FindIntersectingPoints(bedge,boxduplicate);

  // Remove box boundaries from to duplicate
  int ndup=(int)toduplicate.size();
  int counter=0;
  vector<vector<int> > todup2,totest2;
  vector<int> proclist2;
  for(int i=0;i<ndup;++i)
    {
      if(proclist[i]!=-1)
	{
	  todup2.push_back(toduplicate[i]);
	  totest2.push_back(totest[i]);
	  proclist2.push_back(proclist[i]);
	}
    }
  toduplicate=todup2;
  totest=totest2;
  proclist=proclist2;
  for(int i=0;i<4;++i)
    RigidBoundaryPoints(boxduplicate[i],bedge[i]);

  NonSendBoundary(proclist,toduplicate,vproc,totest,bedge);
  NonSendCorners(cornerproc,corners,vproc);
  RemoveDuplicateCorners(cornerproc,corners,proclist,toduplicate);
  Nextra=Tri->GetCorSize()-Tri->GetOriginalLength();

  // Send/Recv boundaries
  vector<int> proclisttemp(proclist);
  vector<vector<int> > toduptemp(toduplicate);
  proclisttemp.insert(proclisttemp.end(),cornerproc.begin(),cornerproc.end());
  toduptemp.insert(toduptemp.end(),corners.begin(),corners.end());
  //SendRecv(procorder,proclist,toduplicate,vproc);
  SendRecv(procorder,proclisttemp,toduptemp,vproc);
  ndup=(int)toduplicate.size();
  for(int i=0;i<(int)toduptemp.size();++i)
    {
      GhostPoints.push_back(toduptemp[i]);
      GhostProcs.push_back(proclisttemp[i]);
    }
  sortvectors(toduplicate);
  edges.clear();
  build_v();
  vector<vector<int> > moreduplicate;
  GetAdditionalBoundary(toduplicate,moreduplicate,totest);
  GetToTest(moreduplicate,totest);
  SendRecv(procorder,proclist,moreduplicate,vproc);
  for(int i=0;i<ndup;++i)
    {
      GhostPoints.push_back(moreduplicate[i]);
      GhostProcs.push_back(proclist[i]);
    }
  sortvectors(toduplicate);
  edges.clear();
  build_v();

  vector<vector<int> > temp(CombineVectorVector(toduplicate,moreduplicate));
  GetAdditionalBoundary(temp,moreduplicate,totest);
  SendRecv(procorder,proclist,moreduplicate,vproc);
  for(int i=0;i<ndup;++i)
    {
      GhostPoints.push_back(moreduplicate[i]);
      GhostProcs.push_back(proclist[i]);
    }

  edges.clear();
  build_v();

  int n=GetPointNo();
  CM.resize(n);
  for(int i=0;i<n;++i)
    CM[i]=CalcCellCM(i);
#endif
}

vector<int> VoronoiMesh::GetNeighbors(int index)const
{
  int n=(int)mesh_vertices[index].size();
  int other;
  vector<int> res;
  res.reserve(n);
  for(int i=0;i<n;++i)
    {
      if((other=edges[mesh_vertices[index][i]].neighbors.first)!=index)
	{
	  //if(other!=-1)
	  res.push_back(other);
	}
      else
	{
	  other=edges[mesh_vertices[index][i]].neighbors.second;
	  //if(other!=-1)
	  res.push_back(other);
	}	
    }
  return res;
}


vector<int> VoronoiMesh::GetLiteralNeighbors(int index)const
{
  int n=(int)mesh_vertices[index].size();
  int other;
  vector<int> res;
  res.reserve(n);
  for(int i=0;i<n;++i)
    {
      if((other=edges[mesh_vertices[index][i]].neighbors.first)!=index)
	{
	  if(other>-1)
	    res.push_back(other);
	}
      else
	{
	  if(other>-1)
	    other=edges[mesh_vertices[index][i]].neighbors.second;
	  res.push_back(other);
	}	
    }
  return res;
}




Tessellation* VoronoiMesh::clone(void)const
{
  return new VoronoiMesh(*this);
}

void VoronoiMesh::RemoveCells(vector<int> &ToRemove,vector<vector<int> > &VolIndex,
			      vector<vector<double> > &Volratio)
{
  Remove_Cells(*this,ToRemove,VolIndex,Volratio);
}

void Remove_Cells(VoronoiMesh &V,vector<int> &ToRemove,
		  vector<vector<int> > &VolIndex,vector<vector<double> > &Volratio)
{
  VolIndex.clear();
  Volratio.clear();
  size_t n=ToRemove.size();
  VolIndex.resize(n);
  Volratio.resize(n);
  vector<int> RemovedEdges;
  for(size_t i=0;i<n;++i)
    {
      double vol_inverse=1/V.GetVolume(ToRemove[i]);
      // Get neighboring points
      vector<int> neigh=V.GetLiteralNeighbors(ToRemove[i]);
      vector<Vector2D> neighcor;
      //Remove The boundary points
      vector<int> real_neigh; //Keeps track of the real points I copied
      vector<int> period_points;// Keeps track of the points which were reflected
      real_neigh.reserve(10);
      period_points.reserve(10);
      for(size_t j=0;j<neigh.size();++j)
	{
	  if(neigh[j]>-1)
	    {
	      real_neigh.push_back(V.GetOriginalIndex(
						      neigh[j]));	
	      period_points.push_back(neigh[j]);
	    }
	}
      vector<int> AllPoints=real_neigh;
      for(size_t j=0;j<real_neigh.size();++j)
	{
	  vector<int> temp_neigh=V.GetNeighbors(real_neigh[j]);
	  for(size_t jj=0;jj<temp_neigh.size();++jj)
	    if(temp_neigh[jj]>-1&&temp_neigh[jj]!=ToRemove[i])
	      AllPoints.push_back(temp_neigh[jj]);
	}
      // save old volume
      vector<double> old_vol(real_neigh.size());
      vector<int> indeces;
      vector<int> temp_period(period_points);
      sort_index(real_neigh,indeces);
      sort(real_neigh.begin(),real_neigh.end());
      for(size_t j=0;j<real_neigh.size();++j)
	{
	  old_vol[j]=V.GetVolume(real_neigh[j]);
	  period_points[j]=temp_period[indeces[j]];
	}
      sort(AllPoints.begin(),AllPoints.end());
      AllPoints=unique(AllPoints);
      AllPoints=RemoveList(AllPoints,real_neigh);
      AllPoints.insert(AllPoints.begin(),real_neigh.begin(),real_neigh.end());
      neighcor.reserve(AllPoints.size());
      for(size_t j=0;j<AllPoints.size();++j)
	neighcor.push_back(V.Tri->get_point(AllPoints[j]));
      // build new local tessellation
      VoronoiMesh Vlocal;
      Vlocal.Initialise(neighcor,V.obc);
      // save the volume change
      for(size_t j=0;j<real_neigh.size();++j)
	{
	  old_vol[(int)j]=(Vlocal.GetVolume((int)j)-old_vol[(int)j])*vol_inverse;
	  if(old_vol[j]<(-1e-5))
	    {
	      cout<<"Negative volume ratio is "<<old_vol[j]<<endl;
	      throw negative_volume_ratio(V,real_neigh[j],ToRemove);
	    }
	}
      // Check we did ok
      double vol_sum=0;
      for(size_t j=0;j<real_neigh.size();++j)
	{
	  vol_sum+=old_vol[j];
	}
      if(abs(1-vol_sum)>1e-4)
	{
	  UniversalError eo("Bad total volume in Voronoi Remove_Cells");
	  eo.AddEntry("Total Volume",vol_sum);
	  eo.AddEntry("Removed cell",ToRemove[i]);
	  throw eo;
	}
      Volratio[i]=old_vol;
      VolIndex[i]=real_neigh;
      // Fix the old Voronoi////////////////
      int Nedges=(int)Vlocal.edges.size();
      // Keep only the relevant edges
      vector<Edge> NewEdges;
      int N=(int)real_neigh.size();
      for(int j=0;j<Nedges;++j)
	{
	  int temp0=Vlocal.edges[j].neighbors.first;
	  int temp1=Vlocal.edges[j].neighbors.second;
	  if(temp0<N)
	    {
	      if(temp1<N||temp1<0||Vlocal.GetOriginalIndex(temp1)<N)
		{
		  NewEdges.push_back(Vlocal.edges[j]);
		  continue;
		}
	    }
	  if(temp1<N)
	    if(temp0<0||Vlocal.GetOriginalIndex(temp0)<N)
	      NewEdges.push_back(Vlocal.edges[j]);
	}
      // Fix the new edges to correspond to the correct points in cor, use real_neigh
      for(size_t j=0;j<NewEdges.size();++j)
	{
	  int temp=NewEdges[j].neighbors.first;
	  if(temp>N)
	    {
	      // New point add to tessellation
	      V.Tri->AddAditionalPoint(Vlocal.GetMeshPoint(temp));
	      NewEdges[j].neighbors.first = V.Tri->GetCorSize()-1;
	    }
	  else
	    {
	      if(temp>-1)
		NewEdges[j].neighbors.first = real_neigh[temp];
	    }
	  temp=NewEdges[j].neighbors.second;
	  if(temp>N)
	    {
	      // New point add to tessellation
	      V.Tri->AddAditionalPoint(Vlocal.GetMeshPoint(temp));
	      NewEdges[j].neighbors.second = V.Tri->GetCorSize()-1;
	    }
	  else
	    {
	      if(temp>-1)
		NewEdges[j].neighbors.second = real_neigh[temp];
	    }
	}
      const int Npoints=V.GetPointNo();
      //Remove bad edge reference from mesh vertices and outer edges
      vector<int> oldedges=V.mesh_vertices[ToRemove[i]];
      for(int j=0;j<N;++j)
	{
	  int temp1=real_neigh[j];
	  int NN=(int)V.mesh_vertices[temp1].size();
	  for(int jj=0;jj<NN;++jj)
	    {
	      Edge etemp=V.edges[V.mesh_vertices[temp1][jj]];
	      if(etemp.neighbors.first==-1||V.GetOriginalIndex(etemp.neighbors.first)
		 ==ToRemove[i])
		{
		  if(etemp.neighbors.first<0||etemp.neighbors.first>Npoints)
		    oldedges.push_back(V.mesh_vertices[temp1][jj]);
		  V.mesh_vertices[temp1].erase(V.mesh_vertices[temp1].begin()
					       +jj);
		  --NN;
		  --jj;
		  continue;
		}
	      if(etemp.neighbors.second==-1||V.GetOriginalIndex(etemp.neighbors.second)
		 ==ToRemove[i])
		{
		  if(etemp.neighbors.second<0||etemp.neighbors.second>Npoints)
		    oldedges.push_back(V.mesh_vertices[temp1][jj]);
		  V.mesh_vertices[temp1].erase(V.mesh_vertices[temp1].begin()
					       +jj);
		  --NN;
		  --jj;
		  continue;
		}
	    }
	}

      // copy the new edges	
      Nedges=(int)NewEdges.size();	
      // Fix the mesh vertices to point to the correct edges
      int temp,other;
      for(int j=0;j<Nedges;++j)
	{
	  const double R=NewEdges[j].GetLength();
	  const double eps=1e-8;
	  for(int k=0;k<2;++k)
	    {
	      temp=pair_member(NewEdges[j].neighbors,k);
	      other=pair_member(NewEdges[j].neighbors,(k+1)%2);
	      if(temp==-1||temp>Npoints)
		continue;				
	      size_t jj=0;			
	      if(other!=-1)
		{
		  for(jj=0;jj<V.mesh_vertices[temp].size();++jj)
		    {
		      if(V.edges[V.mesh_vertices[temp][jj]].neighbors.first>-1)
			if(V.GetMeshPoint(V.edges[V.mesh_vertices[temp][jj]].
					  neighbors.first).distance(V.GetMeshPoint(other))<eps*R)
			  {
			    V.edges[V.mesh_vertices[temp][jj]]=NewEdges[j];
			    break;
			  }
		      if(V.edges[V.mesh_vertices[temp][jj]].neighbors.second>-1)
			if(V.GetMeshPoint(V.edges[V.mesh_vertices[temp][jj]].
					  neighbors.second).distance(V.GetMeshPoint(other))<eps*R)
			  {
			    V.edges[V.mesh_vertices[temp][jj]]=NewEdges[j];
			    break;
			  }
		    }
		}
	      else
		{
		  V.mesh_vertices[temp].push_back((int)V.edges.size());
		  V.edges.push_back(NewEdges[j]);
		}
	      // We made New neighbors
	      if(jj==V.mesh_vertices[temp].size())
		{
		  if(other<Npoints)
		    {
		      V.mesh_vertices[temp].push_back((int)V.edges.size()-k);
		      if(k==0)
			V.edges.push_back(NewEdges[j]);
		    }
		  else
		    {
		      V.mesh_vertices[temp].push_back((int)V.edges.size());
		      V.edges.push_back(NewEdges[j]);
		    }
		}
	    }
	}
      RemovedEdges.insert(RemovedEdges.end(),oldedges.begin(),oldedges.end());
    }
  // Done with all of the points to remove now start fixing
  sort(RemovedEdges.begin(),RemovedEdges.end());
  RemovedEdges=unique(RemovedEdges);
  sort(ToRemove.begin(),ToRemove.end());

  //Fix edges
  size_t Nedges=V.edges.size();
  int toReduce;
  int temp;
  for(size_t i=0;i<Nedges;++i)
    {
      temp=V.edges[i].neighbors.first;
      if(temp>-1)
	{
	  toReduce=int(lower_bound(ToRemove.begin(),ToRemove.end(),temp)-
		       ToRemove.begin());
	  V.edges[i].neighbors.first = temp - toReduce;
	}
      temp = V.edges[i].neighbors.second;
      if(temp>-1)
	{
	  toReduce=int(lower_bound(ToRemove.begin(),ToRemove.end(),temp)-
		       ToRemove.begin());
	  V.edges[i].neighbors.second = temp - toReduce;
	}
    }
  // Remove bad edges
  RemoveVector(V.edges,RemovedEdges);
  // Fix mesh_vertices
  RemoveVector(V.mesh_vertices,ToRemove);
  size_t N=V.mesh_vertices.size();
  // Fix edge number in mesh_vertices
  for(size_t i=0;i<N;++i)
    {
      for(size_t j=0;j<V.mesh_vertices[i].size();++j)
	{
	  toReduce=int(lower_bound(RemovedEdges.begin(),RemovedEdges.end(),
				   V.mesh_vertices[i][j])-RemovedEdges.begin());
	  V.mesh_vertices[i][j]-=toReduce;
	}
    }
  // Fix cor in Tri
  RemoveVector(V.Tri->ChangeCor(),ToRemove);
  // Fix CM
  V.CM.resize(V.CM.size()-ToRemove.size());
  int NN=(int)V.CM.size();
  for(int i=0;i<NN;++i)
    V.CM[i]=V.CalcCellCM(i);
  // Fix point numer in Tri
  V.Tri->ChangeOlength(V.Tri->get_length()-(int)n);
  V.Tri->Changelength(V.Tri->get_length()-(int)n);
  // Fix GhostPoints
  int nsent=(int)V.GhostPoints.size();
  for(int i=0;i<nsent;++i)
    {
      if(V.GhostPoints[i].empty())
	continue;
      int nsent2=(int)V.GhostPoints[i].size();
      for(int j=0;j<nsent2;++j)
	{
	  int toReduce2=int(lower_bound(ToRemove.begin(),ToRemove.end(),V.GhostPoints[i][j])-
			    ToRemove.begin());
	  if((V.GhostPoints[i][j]-toReduce2)==V.GetPointNo())
	    ++toReduce2;
	  V.GhostPoints[i][j]-=toReduce2;
	}
    }
#ifdef RICH_MPI
  // Fix the self index
  RemoveVector(V.selfindex,ToRemove);
#endif
  // Fix the sent cells, do i need this??
  V.Nextra-=(int)ToRemove.size();
  return;
  // Reset Tree if self gravity is needed
}

namespace
{
  int FindEdge(VoronoiMesh const& V,int tofind,int celltolook)
  {
    vector<int> edges=V.GetCellEdges(celltolook);
    int n=(int)edges.size();
    for(int i=0;i<n;++i)
      {
	Edge edge=V.GetEdge(edges[i]);
	if(V.GetOriginalIndex(edge.neighbors.first)==tofind||
	   V.GetOriginalIndex(edge.neighbors.second)==tofind)
	  return edges[i];
      }
    throw("Couldn't find neighbor in Voronoi::FindEdge");
  }
}

int FixPeriodNeighbor(VoronoiMesh &V,int other,int ToRefine,int NewIndex,
		      Vector2D const& NewPoint)
{
  int loc=FindEdge(V,ToRefine,other);
  vector<Vector2D>& cor=V.Tri->ChangeCor();
  cor.push_back(NewPoint);
  int index= V.edges[loc].neighbors.second==other ? 0 : 1;
  int& temp = V.edges[loc].neighbors.second==other ? 
    V.edges[loc].neighbors.first :
    V.edges[loc].neighbors.second;
  temp = (int)cor.size();
  return (int)cor.size();
}


void VoronoiMesh::RefineCells(vector<int> const& ToRefine,
			      vector<Vector2D> const& directions,double alpha)
{
  Refine_Cells(*this,ToRefine,alpha,directions,obc->GetBoundaryType()==Periodic);
  return;
}

void Refine_Cells(VoronoiMesh &V,vector<int> const& ToRefine,double alpha,
		  vector<Vector2D> const& directions,bool PeriodicBoundary)
{
  int n=(int)ToRefine.size();
  if(n==0)
    return;
  int Npoints=V.GetPointNo();
  vector<Vector2D>& cor=V.Tri->ChangeCor();
  vector<Vector2D> cortemp(cor.size()-Npoints);
  // copy the ghost points into cortemp
  copy(cor.begin()+Npoints,cor.end(),cortemp.begin());
  int N=(int)cor.size();
  // Expand the cor vector to include the new points
  cor.resize(N+n);
  copy(cortemp.begin(),cortemp.end(),cor.begin()+Npoints+n);
  cortemp.clear();
  // Fix the boundary point refrences if needed
  bool mpion=false;
#ifdef RICH_MPI
  mpion=true;
#endif
  if(PeriodicBoundary||mpion)
    {
      for(int i=0;i<(int)V.edges.size();++i)
	{
	  int temp_neigh=V.edges[i].neighbors.first;
	  if(temp_neigh>Npoints)
	    {
	      V.edges[i].neighbors.first = temp_neigh+n;
	      continue;
	    }
	  temp_neigh=V.edges[i].neighbors.second;
	  if(temp_neigh>Npoints)
	    V.edges[i].neighbors.second = temp_neigh+n;
	}
    }
  // Update the lengths
  V.Tri->Changelength(Npoints+n);
  V.Tri->ChangeOlength(Npoints+n);
  V.Nextra+=(int)ToRefine.size();
  // reserve space for mesh_vertices
  V.mesh_vertices.resize(Npoints+n);
  // Refine the points
  for(int i=0;i<n;++i)
    {
      // Get the edges
      vector<int> edge_index=V.GetCellEdges(ToRefine[i]);
      int nedges=(int)edge_index.size();
      vector<Edge> edges(nedges);
      for(int j=0;j<nedges;++j)
	edges[j]=V.GetEdge(edge_index[j]);
      // Make the new point by finding the closest edge
      Vector2D NewPoint=V.GetMeshPoint(ToRefine[i]);
      double R=V.GetWidth(ToRefine[i]);
      Vector2D normal;
      Vector2D slope;
      // Do we have a specified direction or do we do line conneting the CM or nearest edge
      if(!directions.empty())
	{
	  slope=directions[i]/abs(directions[i]);
	  normal.Set(slope.y,-slope.x);
	}
      else
	slope=FindBestSplit(&V,ToRefine[i],edges,R,normal);
      NewPoint+=alpha*R*slope;
      cor[Npoints+i]=NewPoint;
      Vector2D v(V.GetMeshPoint(ToRefine[i]));
      // Split edges and update neighbors
      vector<int> old_ref;
      vector<int> new_ref;
      // Calculate the splitting edge
      Edge splitedge(0.5*(NewPoint+V.GetMeshPoint(ToRefine[i])) - 
		     normal*R*20,
		     0.5*(NewPoint+V.GetMeshPoint(ToRefine[i])) +
		     normal*R*20,
		     Npoints+i,ToRefine[i]);
      Vector2D intersect;
      // Find other intersecting segment
      int counter_edges=0;
      int coincide;
      Vector2D vs1,vs2;
      for(int j=0;j<nedges;++j)
	{
	  // Does the splitting edge cross the edge?
	  if(SegmentIntersection(splitedge,edges[j],intersect,1e-7))
	    {
	      if(counter_edges==0)
		vs1=intersect;
	      else
		if(vs1.distance(intersect)<R*1e-8)
		  continue;
		else
		  vs2=intersect;			
	      ++counter_edges;
	      if(counter_edges==2)
		{
		  splitedge.SetVertex(vs1,0);
		  splitedge.SetVertex(vs2,1);
		  break;
		}
	    }
	}
      if(counter_edges!=2)
	{
	  UniversalError eo("Error in splitting the cell");
	  eo.AddEntry("Cell index",ToRefine[i]);
	  throw eo;
	}
      // add the splitting edge
      old_ref.push_back((int)V.edges.size());
      new_ref.push_back((int)V.edges.size());
      V.edges.push_back(splitedge);
      // make the new edges
      for(int j=0;j<nedges;++j)
	{
	  // Does the edge have a coinciding point with the splitting edge?
	  if(splitedge.vertices.first.distance(edges[j].vertices.first)<
	     1e-8*R||splitedge.vertices.first.distance(edges[j].
						     vertices.second)<1e-8*R)
	    coincide=0;
	  else
	    if(splitedge.vertices.second.distance(edges[j].vertices.first)<
	       1e-8*R||splitedge.vertices.second.distance(edges[j].
						       vertices.second)<1e-8*R)
	      coincide=1;
	    else
	      coincide=2;
	  if(coincide<2)
	    {
	      // No need to change vertices only neighbors
	      if(DistanceToEdge(v,edges[j])>DistanceToEdge(NewPoint,edges[j]))
		{
		  Edge NewEdge(edges[j]);
		  int rindex=1;
		  if(NewEdge.neighbors.first==ToRefine[i])
		    rindex=0;
		  NewEdge.set_friend(rindex,Npoints+i);
		  V.edges[edge_index[j]]=NewEdge;
		  new_ref.push_back(edge_index[j]);
		  const Vector2D diff(V.GetMeshPoint(pair_member(NewEdge.neighbors,(rindex+1)%2))-V.GetMeshPoint(V.GetOriginalIndex(pair_member(NewEdge.neighbors,(rindex+1)%2))));
		  if(pair_member(NewEdge.neighbors,(rindex+1)%2)>(n+Npoints))
		    FixPeriodNeighbor(V,V.GetOriginalIndex(pair_member(NewEdge.neighbors,(rindex+1)%2)),
				      ToRefine[i],Npoints+i,NewPoint-diff);
		}
	      else
		old_ref.push_back(edge_index[j]);
	      continue;
	    }
	  // Does the splitting edge cross the edge?
	  SegmentIntersection(splitedge,edges[j],intersect);
	  if(DistanceToEdge(intersect,edges[j])<1e-8*R)
	    {
	      Edge NewEdge;
	      NewEdge.set_friend(0,Npoints+i);
	      if(edges[j].neighbors.first==ToRefine[i])
		NewEdge.set_friend(1,edges[j].neighbors.second);
	      else
		NewEdge.set_friend(1,edges[j].neighbors.first);
	      int index;
	      if(NewPoint.distance(edges[j].vertices.first)<
		 v.distance(edges[j].vertices.first))
		index=0;
	      else
		index=1;
	      NewEdge.vertices.first = pair_member(edges[j].vertices,index);
	      NewEdge.vertices.second = intersect;
	      Vector2D diff(0,0);
	      if(NewEdge.neighbors.second>(n+Npoints))
		{
		  diff=V.GetMeshPoint(NewEdge.neighbors.second)-
		    V.GetMeshPoint(V.GetOriginalIndex(NewEdge.neighbors.second));
		  Edge temp(NewEdge.vertices.first-diff,
			    NewEdge.vertices.second-diff,
			    Npoints+i, 
			    V.GetOriginalIndex(NewEdge.neighbors.second));
		  V.mesh_vertices[temp.neighbors.second].push_back((int)V.edges.size());
		  V.edges.push_back(temp);
		  int loc=FindEdge(V,ToRefine[i],temp.neighbors.second);
		  int index2;
		  if(temp.vertices.first.distance(V.edges[loc].vertices.first)<
		     temp.vertices.first.distance(V.edges[loc].vertices.second))
		    index2=0;
		  else
		    index2=1;
		  set_pair_member(V.edges[loc].vertices,
				  index2, temp.vertices.second);
		  FixPeriodNeighbor(V,V.GetOriginalIndex(NewEdge.neighbors.second),
				    Npoints+i,Npoints+i,NewPoint-diff);
		}
	      else
		if(NewEdge.neighbors.second>-1)
		  V.mesh_vertices[NewEdge.neighbors.second].push_back((int)V.edges.size());
	      new_ref.push_back((int)V.edges.size());
	      V.edges.push_back(NewEdge);
	      // Do the other split
	      NewEdge.vertices.first = pair_member(edges[j].vertices,(index+1)%2);
	      NewEdge.set_friend(0,ToRefine[i]);
	      V.edges[edge_index[j]]=NewEdge;
	      old_ref.push_back(edge_index[j]);
	      continue;
	    }
	  // No need to split the edge
	  // check if update is needed
	  if(NewPoint.distance(edges[j].vertices.first)<
	     v.distance(edges[j].vertices.first))
	    {
	      // Change edge neighbors
	      int index=1;
	      if(edges[j].neighbors.first==ToRefine[i])
		index=0;
	      V.edges[edge_index[j]].set_friend(index,Npoints+i);
	      // add new reference
	      new_ref.push_back(edge_index[j]);
	      if(pair_member(V.edges[edge_index[j]].neighbors,(index+1)%2)>(n+Npoints))
		{
		  int other=pair_member(edges[j].neighbors,(index+1)%2);
		  const Vector2D diff=V.GetMeshPoint(other)-V.GetMeshPoint(
									   V.GetOriginalIndex(other));
		  other=V.GetOriginalIndex(other);
		  FixPeriodNeighbor(V,other,ToRefine[i],Npoints+i,
				    NewPoint-diff);
		}
	    }
	  else
	    old_ref.push_back(edge_index[j]);
	}
      V.mesh_vertices[Npoints+i]=new_ref;
      V.mesh_vertices[ToRefine[i]]=old_ref;
    }

  // Calculate the new CM
  for(int i=0;i<(int)ToRefine.size();++i)
    V.CM.push_back(V.CalcCellCM(Npoints+i));
  for(int i=0;i<(int)ToRefine.size();++i)
    V.CM[ToRefine[i]]=V.CalcCellCM(ToRefine[i]);
  return;
  // Reset Tree if self gravity is needed
}

int VoronoiMesh::GetPointNo(void) const
{
  return Tri->get_length();
}

Vector2D VoronoiMesh::GetMeshPoint(int index) const
{
  return Tri->get_point(index);
}

int VoronoiMesh::GetTotalSidesNumber(void) const 
{
  return (int)edges.size();
}

Edge const& VoronoiMesh::GetEdge(int index) const 
{
  return edges[index];
}

Vector2D const& VoronoiMesh::GetCellCM(int index) const
{
  return CM[index];
}

vector<Vector2D>& VoronoiMesh::GetAllCM(void)
{
  return CM;
}

void VoronoiMesh::FindIntersectingPoints(vector<Edge> const &box_edges,
					 vector<vector<int> > &toduplicate)
{
  int n=(int)box_edges.size();
  toduplicate.resize(n);
  int N=(int)mesh_vertices.size();
  if(N<20)
    {
      for(int i=0;i<n;++i)
	for(int j=0;j<N;++j)
	  toduplicate[i].push_back(j);
      return;
    }

  for(int i=0;i<N;++i)
    {
      vector<int> temp=CellIntersectBoundary(box_edges,i);
      int j=(int)temp.size();
      if(j>0)
	{
	  for(int k=0;k<j;++k)
	    toduplicate[temp[k]].push_back(i);
	}
    }
  for(int i=0;i<n;++i)
    {
      if(!toduplicate[i].empty())
	{
	  sort(toduplicate[i].begin(),toduplicate[i].end());
	  toduplicate[i]=unique(toduplicate[i]);
	}
    }
}

vector<int> VoronoiMesh::CellIntersectBoundary(vector<Edge> const&box_edges,int cell)
{
  int ncell=(int)mesh_vertices[cell].size();
  int nbox=(int) box_edges.size();
  vector<int> res;
  vector<Vector2D> intersections;
  Vector2D intersect;
  for(int i=0;i<ncell;++i)
    {
      for(int j=0;j<nbox;++j)
	{
	  if(SegmentIntersection(box_edges[j],edges[mesh_vertices[cell][i]],
				 intersect))
	    {
	      res.push_back(j);
	      intersections.push_back(intersect);
	    }
	}
    }
  vector<int> indeces;
  sort_index(res,indeces);
  sort(res.begin(),res.end());
  res=unique(res);
  int nintersect=(int)res.size();
  if(nintersect>1)
    {
      if(nintersect>3)
	{
	  UniversalError eo("Too many intersections of boundary cell with box edges");
	  eo.AddEntry("Cell number",(double)cell);
	  Vector2D pp=Tri->get_point(cell);
	  eo.AddEntry("x cor",pp.x);
	  eo.AddEntry("y cor",pp.y);
	  for(int jj=0;jj<(int)intersections.size();++jj)
	    {
	      eo.AddEntry("Intersection x",intersections[jj].x);
	      eo.AddEntry("Intersection y",intersections[jj].y);
	    }
	  throw;
	}
      vector<Vector2D> cpoints;
      ConvexHull(cpoints,this,cell);
      for(int i=0;i<nbox;++i)
	if(PointInCell(cpoints,box_edges[i].vertices.first)||
	   PointInCell(cpoints,box_edges[i].vertices.second))
	  res.push_back(i);
      sort(res.begin(),res.end());
      res=unique(res);
      /*		if(cell_edges_intersect[0]!=cell_edges_intersect[1])
			{
			boost::array<Vector2D,3> test;
			SegmentIntersection(edges[mesh_vertices[cell][cell_edges_intersect[0]]],
			edges[mesh_vertices[cell][cell_edges_intersect[1]]],intersect);
			test[0]=intersect;
			test[1]=intersections[indeces[0]];
			test[2]=intersections[indeces[nintersect-1]];
			double temp=orient2d(test);
			for(int k=0;k<nbox;++k)
			{
			if(binary_search(res.begin(),res.end(),k))
			continue;
			test[0]=0.5*(box_edges[k].vertices.first+box_edges[k].vertices.second);
			if(orient2d(test)*temp<0)
			toadd.push_back(k);
			}
			}
			else
			{
			boost::array<Vector2D,3> test;
			test[0]=intersections[indeces[0]];
			test[1]=intersections[indeces[1]];
			test[2]=Tri->get_point(cell);
			double temp=orient2d(test);
			for(int k=0;k<nbox;++k)
			{
			if(binary_search(res.begin(),res.end(),k))
			continue;
			test[2]=box_edges[k].vertices.second;
			if(orient2d(test)*temp>0)
			{
			toadd.push_back(k);
			continue;
			}
			test[2]=box_edges[k].vertices.first;
			if(orient2d(test)*temp>0)
			toadd.push_back(k);
			}
			}*/
    }
  return res;
}

vector<Edge> VoronoiMesh::GetBoxEdges(void)
{
  vector<Edge> res(4);
  res[1]=Edge(Vector2D(obc->GetGridBoundary(Left),
		       obc->GetGridBoundary(Up)),
	      Vector2D(obc->GetGridBoundary(Right),
		       obc->GetGridBoundary(Up)),
	      0,0);
  res[3]=Edge(Vector2D(obc->GetGridBoundary(Left),
		       obc->GetGridBoundary(Down)),
	      Vector2D(obc->GetGridBoundary(Right),
		       obc->GetGridBoundary(Down)),
	      0,0);
  res[0]=Edge(Vector2D(obc->GetGridBoundary(Right),
		       obc->GetGridBoundary(Up)),
	      Vector2D(obc->GetGridBoundary(Right),
		       obc->GetGridBoundary(Down)),
	      0,0);
  res[2]=Edge(Vector2D(obc->GetGridBoundary(Left),
		       obc->GetGridBoundary(Up)),
	      Vector2D(obc->GetGridBoundary(Left),
		       obc->GetGridBoundary(Down)),
	      0,0);
  return res;
}

bool VoronoiMesh::CloseToBorder(int point,int &border)
{
  int olength=Tri->GetOriginalLength();
  int n=(int)mesh_vertices[point].size();
  for(int i=0;i<n;++i)
    {
      if(edges[mesh_vertices[point][i]].neighbors.second==point)
	border=edges[mesh_vertices[point][i]].neighbors.first;
      else
	border=edges[mesh_vertices[point][i]].neighbors.second;
      if(border>olength)
	return true;
    }
  return false;
}

vector<int> VoronoiMesh::GetBorderingCells(vector<int> const& copied,
					   vector<int> const& totest,int tocheck,vector<int> tempresult,int outer)
{
  int border,test;
  int olength=Tri->GetOriginalLength();
  tempresult.push_back(tocheck);
  sort(tempresult.begin(),tempresult.end());
  int n=(int)mesh_vertices[tocheck].size();
  for(int i=0;i<n;++i)
    {
      if(edges[mesh_vertices[tocheck][i]].neighbors.second==tocheck)
	test=edges[mesh_vertices[tocheck][i]].neighbors.first;
      else
	test=edges[mesh_vertices[tocheck][i]].neighbors.second;
      if(test>=olength)
	continue;
      if(test<0)
	continue;
      if(CloseToBorder(test,border))
	if(border==outer)
	  if(!binary_search(copied.begin(),copied.end(),test)&&
	     !binary_search(totest.begin(),totest.end(),test)&&
	     !binary_search(tempresult.begin(),tempresult.end(),test))
	    tempresult=GetBorderingCells(copied,totest,test,tempresult,outer);
    }
  return tempresult;
}

void VoronoiMesh::GetAdditionalBoundary(vector<vector<int> > &copied,
					vector<vector<int> > &neighbors,vector<vector<int> > &totest)
{
  int nsides=(int) copied.size();
  // Get all the neighbors
  neighbors.clear();
  neighbors.resize(nsides);
  for(int i=0;i<nsides;++i)
    {
      sort(copied[i].begin(),copied[i].end());
      // look if there are boundary points neighbors
      int n=(int)totest[i].size();
      for(int j=0;j<n;++j)
	{
	  if(totest[i][j]==-1)
	    continue;
	  vector<int> toadd;
	  int outer=0;
	  if(CloseToBorder(totest[i][j],outer))
	    toadd=GetBorderingCells(copied[i],totest[i],totest[i][j],toadd,outer);
	  int nn=(int)toadd.size();
	  for(int k=0;k<nn;++k)
	    neighbors[i].push_back(toadd[k]);
	}
      sort(neighbors[i].begin(),neighbors[i].end());
      neighbors[i]=unique(neighbors[i]);
      neighbors[i]=RemoveList(neighbors[i],copied[i]);
    }
}

void VoronoiMesh::GetRealNeighbor(vector<int> &result,int point)
{
  result.reserve(7);
  int n=(int)mesh_vertices[point].size();
  int olength=Tri->GetOriginalLength();
  for(int i=0;i<n;++i)
    {
      if(edges[mesh_vertices[point][i]].neighbors.first==point)
	{
	  if(edges[mesh_vertices[point][i]].neighbors.second>-1&&
	     edges[mesh_vertices[point][i]].neighbors.second<olength)
	    result.push_back(edges[mesh_vertices[point][i]].neighbors.second);
	}
      else
	{
	  if(edges[mesh_vertices[point][i]].neighbors.first>-1&&
	     edges[mesh_vertices[point][i]].neighbors.first<olength)
	    result.push_back(edges[mesh_vertices[point][i]].neighbors.first);
	}
    }
  sort(result.begin(),result.end());
  unique(result);
}

void VoronoiMesh::GetNeighborNeighbors(vector<int> &result,int point)
{
  vector<int> neigh;
  result.clear();
  GetRealNeighbor(neigh,point);
  int n=(int)neigh.size();
  for(int i=0;i<n;++i)
    {
      vector<int> temp;
      GetRealNeighbor(temp,neigh[i]);
      int N=(int)temp.size();
      for(int j=0;j<N;++j)
	result.push_back(temp[j]);
    }
  sort(result.begin(),result.end());
  unique(result);
}

void VoronoiMesh::GetCorners(vector<vector<int> > &copied,
			     vector<vector<int> > &result)
{
  // copied should be sorted already
  int nsides=(int)copied.size();
  result.clear();
  result.resize(nsides);
  vector<vector<int> > toadd(nsides);
  for(int i=0;i<nsides;++i)
    {
      int n=(int)copied[i].size();
      for(int j=0;j<n;++j)
	{
	  if(binary_search(copied[(i+1)%nsides].begin(),copied[(i+1)%nsides].end(),
			   copied[i][j]))
	    {
	      vector<int> temp;
	      GetNeighborNeighbors(temp,copied[i][j]);
	      result[i].insert(result[i].end(),temp.begin(),temp.end());
	      temp=AddPointsAlongEdge(copied[i][j],copied,i);
	      toadd[(i+1)%nsides].insert(toadd[(i+1)%nsides].end(),temp.begin(),
					 temp.end());
	    }
	  if(binary_search(copied[(i-1+nsides)%nsides].begin(),copied[(i-1+nsides)%nsides].end(),
			   copied[i][j]))
	    {
	      vector<int> temp;
	      GetNeighborNeighbors(temp,copied[i][j]);
	      result[(i-1+nsides)%nsides].insert(result[(i-1+nsides)%nsides].end()
						 ,temp.begin(),temp.end());
	      temp=AddPointsAlongEdge(copied[i][j],copied,i);
	      toadd[(i-1+nsides)%nsides].insert(toadd[(i-1+nsides)%nsides].end(),
						temp.begin(),temp.end());
	    }
	}
    }
  for(int i=0;i<nsides;++i)
    {
      copied[i].insert(copied[i].end(),toadd[i].begin(),toadd[i].end());
      sort(copied[i].begin(),copied[i].end());
      copied[i]=unique(copied[i]);
      sort(result[i].begin(),result[i].end());
      result[i]=unique(result[i]);
    }
}

void VoronoiMesh::GetToTest(vector<vector<int> > &copied,vector<vector<int> > &totest)
{
  int nsides=(int) copied.size();
  int olength=Tri->GetOriginalLength();
  // sort the vectors
  for(int i=0;i<nsides;++i)
    sort(copied[i].begin(),copied[i].end());
  totest.resize(nsides);
  int test=0;
  for(int i=0;i<nsides;++i)
    {
      vector<int> totest2;
      int ncopy=(int)copied[i].size();
      for(int j=0;j<ncopy;++j)
	{
	  int n=(int)mesh_vertices[copied[i][j]].size();
	  for(int k=0;k<n;++k)
	    {
	      if(edges[mesh_vertices[copied[i][j]][k]].neighbors.first==
		 copied[i][j])
		test=edges[mesh_vertices[copied[i][j]][k]].neighbors.second;
	      else
		test=edges[mesh_vertices[copied[i][j]][k]].neighbors.first;
	      if(test<olength)
		totest2.push_back(test);
	    }
	}
      sort(totest2.begin(),totest2.end());
      totest2=unique(totest2);
      totest[i]=totest2;
    }
}

vector<int> VoronoiMesh::FindEdgeStartConvex(int point)
{
  int n=(int)mesh_vertices[point].size();
  Vector2D min_point;
  int min_index=0,p_index;
  if(edges[mesh_vertices[point][0]].vertices.first.x<
     edges[mesh_vertices[point][0]].vertices.second.x)
    {
      min_point=edges[mesh_vertices[point][0]].vertices.first;
      p_index=0;
    }
  else
    {
      min_point=edges[mesh_vertices[point][0]].vertices.second;
      p_index=1;
    }
  for(int i=1;i<n;++i)
    {
      double R=edges[mesh_vertices[point][i]].GetLength();
      if(edges[mesh_vertices[point][i]].vertices.first.x<(min_point.x-R*eps))
	{
	  min_point=edges[mesh_vertices[point][i]].vertices.first;
	  min_index=i;
	  p_index=0;
	}
      if(edges[mesh_vertices[point][i]].vertices.second.x<(min_point.x-R*eps))
	{
	  min_point=edges[mesh_vertices[point][i]].vertices.second;
	  min_index=i;
	  p_index=1;
	}
      if(edges[mesh_vertices[point][i]].vertices.first.x<(min_point.x+R*eps)&&
	 edges[mesh_vertices[point][i]].vertices.first.y<min_point.y)
	{	
	  min_point=edges[mesh_vertices[point][i]].vertices.first;
	  min_index=i;
	  p_index=0;
	}

      if(edges[mesh_vertices[point][i]].vertices.second.x<(min_point.x+R*eps)&&
	 edges[mesh_vertices[point][i]].vertices.second.y<min_point.y)
	{	
	  min_point=edges[mesh_vertices[point][i]].vertices.second;
	  min_index=i;
	  p_index=1;
	}
    }
  vector<int> res(2);
  res[0]=min_index;
  res[1]=p_index;
  return res;
}

void VoronoiMesh::ConvexEdgeOrder(void)
{
  int n=(int)mesh_vertices.size();
  for(int i=0;i<n;++i)
    {
      double R=GetWidth(i);
      vector<int> min_index=FindEdgeStartConvex(i);
      int p_loc=min_index[1];
      int edge_loc=mesh_vertices[i][min_index[0]];
      int nedges=(int)mesh_vertices[i].size();
      list<int> elist;
      for(int j=0;j<nedges;++j)
	{
	  if(j!=min_index[0])
	    elist.push_back(mesh_vertices[i][j]);
	}
      vector<int> new_order;
      new_order.reserve(nedges);
      new_order.push_back(edge_loc);
      for(int j=0;j<nedges;++j)
	{
	  if(j==min_index[0])
	    continue;
	  int nlist=(int)elist.size();
	  list<int>::iterator it=elist.begin();
	  for(int k=0;k<nlist;++k)
	    {
	      double temp0=pair_member(edges[edge_loc].vertices,(p_loc+1)%2).distance(edges[*it].vertices.first);
	      if(temp0<eps*R)
		{
		  p_loc=0;
		  edge_loc=*it;
		  elist.erase(it);
		  new_order.push_back(edge_loc);
		  break;
		}
	      double temp1=pair_member(edges[edge_loc].vertices,(p_loc+1)%2).distance(
									   edges[*it].vertices.second);
	      if(temp1<eps*R)
		{
		  p_loc=1;
		  edge_loc=*it;
		  new_order.push_back(edge_loc);
		  elist.erase(it);
		  break;
		}
	      ++it;
	    }
	}
      mesh_vertices[i]=new_order;
    }
}


vector<Edge>& VoronoiMesh::GetAllEdges(void)
{
  return edges;
}


void VoronoiMesh::NonSendBoundary(vector<int> &
				  proclist,vector<vector<int> > & data,Tessellation const& v,
				  vector<vector<int> > &totest,vector<Edge> const& boxedges)
{
#ifdef RICH_MPI
  vector<int> newproclist;
  vector<vector<int> > newdata;
  vector<vector<int> > newtotest;
  int n=(int)proclist.size();
  int nproc=v.GetPointNo();
  int rank=0;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  for(int i=0;i<n;++i)
    {
      if(proclist[i]==-1)
	{
	  RigidBoundaryPoints(data[i],boxedges[i]);
	  GhostPoints.push_back(data[i]);
	  GhostProcs.push_back(-1);
	  continue;
	}
      if(proclist[i]<nproc)
	{
	  newproclist.push_back(proclist[i]);
	  newdata.push_back(data[i]);
	  newtotest.push_back(totest[i]);
	  continue;
	}
      if(v.GetOriginalIndex(proclist[i])==rank)
	{
	  PeriodicBoundaryPoints(data[i],i);
	  newproclist.push_back(proclist[i]);
	  newdata.push_back(data[i]);
	  newtotest.push_back(totest[i]);
	  continue;
	}
      // The other proc is periodic
      newproclist.push_back(proclist[i]);
      newdata.push_back(data[i]);
      newtotest.push_back(totest[i]);
    }
  proclist=newproclist;
  data=newdata;
  totest=newtotest;
#endif
}

void VoronoiMesh::SendRecv(vector<int> const& procorder,vector<int> const&
			   proclist,vector<vector<int> > &data,Tessellation const& v)
{
#ifdef MPI_VERSION
  int n=(int)procorder.size();
  int nlist=(int)proclist.size();
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  int index;
  vector<int> realproclist(nlist);
  for(int i=0;i<nlist;++i)
    realproclist[i]=v.GetOriginalIndex(proclist[i]);
  for(int i=0;i<n;++i)
    {
      index=find(proclist.begin(),proclist.end(),procorder[i])-proclist.begin();
      // Do we talk with this processor?
      if(index<nlist)
	{
	  // Create send data
	  int nsend=(int)data[index].size();
	  vector<double> send(2*nsend);
	  // Arrange the points to send in Hilbert order
	  vector<Vector2D> cortemp=VectorValues(Tri->ChangeCor(),data[index]);
	  vector<int> order=HilbertOrder(cortemp,(int)cortemp.size());
	  ReArrangeVector(data[index],order);
	  for(int j=0;j<nsend;++j)
	    {
	      //send[2*j]=Tri->get_point(data[index][j]).x;
	      //send[2*j+1]=Tri->get_point(data[index][j]).y;
	      send[2*j]=cortemp[order[j]].x;
	      send[2*j+1]=cortemp[order[j]].y;
	    }
	  // Recv data
	  MPI_Status status;
	  vector<double> recv;
	  int nrecv;
	  if(rank<procorder[i])
	    {
	      if(nsend>0)
		MPI_Send(&send[0],2*nsend,MPI_DOUBLE,procorder[i],0,MPI_COMM_WORLD);
	      else
		{
		  double temp=0;
		  MPI_Send(&temp,1,MPI_DOUBLE,procorder[i],1,MPI_COMM_WORLD);
		}
	      MPI_Probe(procorder[i],MPI_ANY_TAG,MPI_COMM_WORLD,&status);
	      MPI_Get_count(&status,MPI_DOUBLE,&nrecv);
	      recv.resize(nrecv);
	      int rtag=status.MPI_TAG;
	      if(rtag==0)
		MPI_Recv(&recv[0],nrecv,MPI_DOUBLE,procorder[i],0,MPI_COMM_WORLD,&status);
	      else
		{
		  double temp=0;
		  MPI_Recv(&temp,1,MPI_DOUBLE,procorder[i],1,MPI_COMM_WORLD,&status);
		}
	    }
	  else
	    {
	      MPI_Probe(procorder[i],MPI_ANY_TAG,MPI_COMM_WORLD,&status);
	      MPI_Get_count(&status,MPI_DOUBLE,&nrecv);
	      recv.resize(nrecv);
	      if(status.MPI_TAG==0)
		MPI_Recv(&recv[0],nrecv,MPI_DOUBLE,procorder[i],0,MPI_COMM_WORLD,&status);
	      else
		{
		  double temp=0;
		  MPI_Recv(&temp,1,MPI_DOUBLE,procorder[i],1,MPI_COMM_WORLD,&status);
		}
	      if(nsend>0)
		MPI_Send(&send[0],2*nsend,MPI_DOUBLE,procorder[i],0,MPI_COMM_WORLD);
	      else
		{
		  double temp=0;
		  MPI_Send(&temp,1,MPI_DOUBLE,procorder[i],1,MPI_COMM_WORLD);
		}
	    }
	  nrecv/=2;
	  vector<Vector2D> toadd(nrecv);
	  for(int j=0;j<nrecv;++j)
	    toadd[j]=Vector2D(recv[2*j],recv[2*j+1]);
	  // Add the points
	  if(!toadd.empty())
	    {
	      Tri->DoMPIRigid(toadd);
	    }
	}
    }
#endif
}

void VoronoiMesh::RigidBoundaryPoints(vector<int> &points,Edge const& edge)
{
  int npoints=(int)points.size();
  vector<Vector2D> toadd;
  vector<int> pointstemp;
  pointstemp.reserve(npoints);
  toadd.reserve(npoints);
  Vector2D par(Parallel(edge));
  par=par/abs(par);
  Vector2D edge0=edge.vertices.first;
  boost::array<double,4> maxedges=FindMaxCellEdges();
  double dx=maxedges[1]-maxedges[0];
  double dy=maxedges[3]-maxedges[2];
  for(int i=0;i<npoints;++i)
    {
      Vector2D point=Tri->get_point(points[i]);
      Vector2D temp=point-edge0;
      temp=2*par*ScalarProd(par,temp)-temp+edge0;
      if((abs(point.x-temp.x)<2*dx)&&(abs(point.y-temp.y)<2*dy))
	{
	  toadd.push_back(temp);
	  pointstemp.push_back(i);
	}
    }
  if(!toadd.empty())
    {
      vector<int> order=HilbertOrder(toadd,(int)toadd.size());
      ReArrangeVector(toadd,order);
      Tri->DoMPIRigid(toadd);
      ReArrangeVector(pointstemp,order);
      points=pointstemp;
    }
}

void VoronoiMesh::PeriodicBoundaryPoints(vector<int> &points,int edge_number)
{
  int npoints=(int)points.size();
  vector<Vector2D> toadd(npoints);
  Vector2D diff=GetPeriodicDiff(cell_edges[edge_number],obc);
  for(int i=0;i<npoints;++i)
    toadd[i]=Tri->get_point(points[i])+diff;
  if(!toadd.empty())
    {
      vector<int> order=Tri->DoMPIPeriodic(toadd);
      ReArrangeVector(points,order);
    }
}

void VoronoiMesh::NonSendCorners(vector<int> &
				 proclist,vector<vector<int> > & data,Tessellation const& v)
{
#ifdef RICH_MPI
  vector<int> newproclist;
  vector<vector<int> > newdata;
  int n=(int)proclist.size();
  int nproc=v.GetPointNo();
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  for(int i=0;i<n;++i)
    {
      if(proclist[i]<nproc)
	{
	  newproclist.push_back(proclist[i]);
	  newdata.push_back(data[i]);
	  continue;
	}
      if(v.GetOriginalIndex(proclist[i])==rank)
	{
	  CornerBoundaryPoints(data[i],i);
	  continue;
	}
      // The other proc is periodic
      newproclist.push_back(proclist[i]);
      newdata.push_back(data[i]);
    }
  proclist=newproclist;
  data=newdata;
#endif
}

void VoronoiMesh::CornerBoundaryPoints(vector<int> &points,int edge_number)
{
  int n=(int)cell_edges.size();
  Vector2D diff1=GetPeriodicDiff(cell_edges[edge_number],obc);
  Vector2D diff2=GetPeriodicDiff(cell_edges[(edge_number+1)%n],obc);
  int npoints=(int)points.size();
  vector<Vector2D> toadd(npoints);
  for(int i=0;i<npoints;++i)
    toadd[i]=Tri->get_point(points[i])+diff1+diff2;
  if(!toadd.empty())
    {
      vector<int> order=Tri->DoMPIPeriodic(toadd);
      ReArrangeVector(points,order);
    }
}

vector<vector<int> > VoronoiMesh::GetDuplicatedPoints(void)const
{
  return GhostPoints;
}

int VoronoiMesh::GetTotalPointNumber(void)const
{
  return (int)Tri->ChangeCor().size();
}

vector<int> VoronoiMesh::GetDuplicatedProcs(void)const
{
  return GhostProcs;
}

vector<int> VoronoiMesh::GetSentProcs(void)const
{
  return SentProcs;
}

vector<vector<int> > VoronoiMesh::GetSentPoints(void)const
{
  return SentPoints;
}


// cpoints must be convex hull, checks if vec is inside cpoints
bool PointInCell(vector<Vector2D> const& cpoints,Vector2D const& vec)
{
  int n=(int)cpoints.size();
  boost::array<Vector2D,3> tocheck;
  tocheck[2]=vec;
  for(int i=0;i<n;++i)
    {
      tocheck[0]=cpoints[i];
      tocheck[1]=cpoints[(i+1)%n];
      if(orient2d(tocheck)<0)
	return false;
    }
  return true;
}

// result is : minx, maxx, miny, maxy
boost::array<double,4> VoronoiMesh::FindMaxCellEdges(void)
{
  int n=(int)cell_edges.size();
  boost::array<double,4> res;
  res[0]=min(cell_edges[0].vertices.first.x,cell_edges[0].vertices.second.x);
  res[1]=max(cell_edges[0].vertices.first.x,cell_edges[0].vertices.second.x);
  res[2]=min(cell_edges[0].vertices.first.y,cell_edges[0].vertices.second.y);
  res[3]=max(cell_edges[0].vertices.first.y,cell_edges[0].vertices.second.y);
  for(int i=1;i<n;++i)
    {
      res[0]=min(min(cell_edges[i].vertices.first.x,cell_edges[i].vertices.second.x),res[0]);
      res[1]=max(max(cell_edges[i].vertices.first.x,cell_edges[i].vertices.second.x),res[1]);		
      res[2]=min(min(cell_edges[i].vertices.first.y,cell_edges[i].vertices.second.y),res[2]);
      res[3]=max(max(cell_edges[i].vertices.first.y,cell_edges[i].vertices.second.y),res[3]);
    }
  return res;
}