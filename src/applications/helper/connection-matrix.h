#ifndef CONNECTION_MATRIX_H
#define CONNECTION_MATRIX_H

#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include <queue>
#include <list>
#include <set>
#include <map>
#include <math.h>

using namespace std;
namespace ns3 {

struct connection
{
  int src, dst;
  bool large;
};

double
drand ()
{
  int r = rand ();
  int m = RAND_MAX;
  double d = (double) r / (double) m;
  return d;
}

int
pareto (int xm, int mean)
{
  double oneoveralpha = ((double) mean - xm) / mean;
  return (int) ((double) xm / pow (drand (), oneoveralpha));
}

double
exponential (double lambda)
{
  return -log (drand ()) / lambda;
}


class ConnectionMatrix
{
public:
  ConnectionMatrix(int);
  void setPermutation();
  void setRandomConnection(int conns);
  void setStride(int many);

  vector<connection*> *getAllNonConnections();
  vector<connection*> *getAllConnections();
  map<int,vector<int>*> connections;
  int N;
};

} // end of namespace
#endif
