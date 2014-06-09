/*! \file Delaunay.hpp
  \brief Delaunay triangulation with mpi
  \author Elad Steinberg
*/

#ifndef DELAUNAY_HPP
#define DELAUNAY_HPP 1
#include "facet.hpp"
#include <algorithm>
#include <vector>
#include <stack>
#include <climits>
#include "geometry.hpp"
#include "../misc/universal_error.hpp"
#include "HilbertOrder.hpp"
#include "geotests.hpp"
#include "../misc/utils.hpp"
#include "delaunay_logger.hpp"

using namespace std;
/*! \brief The Delaunay data structure. Gets a set of points and constructs the Delaunay tessellation.
  \author Elad Steinberg
*/
class Delaunay
{
private:
  enum Sides{RIGHT,UP,LEFT,DOWN,LU,LD,RU,RD};
  int lastFacet; //last facet to be checked in Walk
  bool CalcRadius;

  class DataOnlyForBuild
  {
  public:		
    DataOnlyForBuild();
    DataOnlyForBuild(DataOnlyForBuild const& other);
    DataOnlyForBuild& operator=(DataOnlyForBuild const& other);
    vector<int> insert_order;
    vector<vector<char> > copied;
  };

  vector<double> radius;
  vector<Vector2D> cell_points;
  bool PointWasAdded;
  int last_facet_added;
  vector<facet> f;	
  vector<Vector2D> cor;
  int length,olength;
  int location_pointer;
  int last_loc;

  bool IsOuterFacet(int facet);
  void add_point(int index);
  void flip(int i,int j);
  void find_diff(facet *f1,facet *f2,int*) const;
  int Walk(int point);
  void CheckInput();
  double CalculateRadius(int facet);
  int FindPointInFacet(int facet,int point);
  double FindMaxRadius(int point);
  void FindContainingTetras(int StartTetra,int point,vector<int> &tetras);

  Delaunay& operator=(const Delaunay& origin);

public:

  /*! \brief Changes the cor olength
    \param n The new length;
  */
  void ChangeOlength(int n);

  /*! \brief Changes the cor length
    \param n The new length
  */
  void Changelength(int n);

   /*! \brief Allows to change the cor
    \return Refrence to the cor vector
  */
  vector<Vector2D>& ChangeCor(void);

  //! \brief Class constructor.
  Delaunay(void);

  /*!
    \brief Copy constructor
    \param other The Triangulation to copy
  */
  Delaunay(Delaunay const& other);

  //! \brief Default destructor.
  ~Delaunay(void);
	
  /*! \brief Builds the Delaunay tessellation.  
    \param vp A refrence to a vector of points to be added. 
    \param cpoints The edges of the processor cell.
  */
  void build_delaunay(vector<Vector2D>const& vp,vector<Vector2D> const& cpoints);
	
  //! \brief Dumps the Delaunay tessellation into a binary file.
  void output(void);

  /*! \brief Returns a facet.
    \param index The faet to return.
    \returns A pointer to the selected facet.
  */
  facet* get_facet(int index);

  /*! \brief Returns a coordinate of a vertice. 
    \param Facet The index of the facet to check. 
    \param vertice The index of the vertice in the facet. 
    \param dim If dim=0 returns the x-coordinate else returns the y-coordinate. 
    \returns The chosen coordinate.
  */
  double get_facet_coordinate(int Facet,int vertice, int dim);

  /*! \brief Returns a point.
    \param index The index of the point. 
    \returns The chosen point.
  */
  Vector2D get_point(int index) const;

  /*! \brief Returns a coordinate.
    \param index The index of the point. 
    \param dim If dim=0 returns the x-coordinate else returns the y-coordinate. 
    \returns The chosen coordinate.
  */
  double get_cor(int index,int dim) const;

  /*! \brief Returns the number of facets. 
    \returns The number of facets.
  */
  int get_num_facet(void);

  /*! \brief Returns the number of points
    \returns The number of points.
  */
  int get_length(void) const;

  /*! \brief Returns the last location, a number used to identify the fact that the neighbor of a facet is empty. 
    \returns The last location.
  */
  int get_last_loc(void) const;

  /*! \brief Change Mesh point. 
    \param index The index of the point to change. 
    \param p The new point to set.
  */
  void set_point(int index, Vector2D p);

  /*! \brief Returns the area of the triangle. Negative result means the triangle isn't right handed. 
    \param index The index to the facet 
    \return The area
  */
  double triangle_area(int index);

  /*!
    \brief Updates the triangulation
    \param points The new set of points
	\param cpoints The points of the processor cell
  */
  void update(const vector<Vector2D>& points,vector<Vector2D> const& cpoints);
	
  /*!
    \brief Returns the original index of the duplicated point in Periodic Boundary conditions
    \param NewPoint The index of the duplicated point
    \return The original index
  */
  int GetOriginalIndex(int NewPoint) const;

  /*!
    \brief Returns the original length of the points (without duplicated points)
    \return The original length
  */
  int GetOriginalLength(void) const;

  /*!
    \brief Returns a refrence to the points
    \return Refrence to the points
  */
  vector<Vector2D>& GetMeshPoints(void);

  /*! \brief Returns the length of all the points (included duplicated)
    \return The length of all of the points
  */
  int GetTotalLength(void);

  /*! \brief return the facet's radius
    \param facet The facet to check
    \return The facet's radius
  */
  double GetFacetRadius(int facet) const;
  
  /*!
  \brief Adds a point to the cor vector. Used in periodic boundaries with AMR.
  \param vec The point to add.
  */
  void AddAditionalPoint(Vector2D const& vec);
   /*!
  \brief Gets the size of the cor vector.
  \return The size of the cor vector.
  */
  int GetCorSize(void)const;
  /*!
  \brief Adds points to a rigid boundary
  \param points The points to add
  */
  void DoMPIRigid(vector<Vector2D> const& points);
  
  /*!
  \brief Adds points to a periodic boundary
  \param points The points to add
  \return The order of the added points
  */
  vector<int> DoMPIPeriodic(vector<Vector2D> const& points);

  //! \brief Diagnostics
  delaunay_loggers::DelaunayLogger* logger;
};
#endif //DELAUNAYMPI_HPP