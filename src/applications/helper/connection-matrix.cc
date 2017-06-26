#include "connection-matrix.h"
#include <string.h>
#include <iostream>
#include "ns3/core-module.h"

using namespace std;
namespace ns3 {

ConnectionMatrix::ConnectionMatrix (int n)
{
  N = n;
}

void
ConnectionMatrix::setPermutation ()
{
  int is_dest[N], dest;
  // Initialise is_dest in order to know either dst is used or not
  for (int i = 0; i < N; i++)
    is_dest[i] = 0;
  // loop through all hosts
  for (int src = 0; src < N; src++)
    { // Initialised all dst vector
      vector<int>* destinations = new vector<int> ();

      int r = rand () % (N - src);

      for (dest = 0; dest < N; dest++)
        {
          if (r == 0 && !is_dest[dest])
            break;
          if (!is_dest[dest])
            r--;
        }

      if (r != 0 || is_dest[dest])
        {
          cout << "Wrong connections r " << r << "is_dest " << is_dest[dest]
              << endl;
          exit (1);
        }

      if (src == dest)
        { //find first other destination that is different!
          do
            {
              dest = (dest + 1) % N;
            }
          while (is_dest[dest]);

          if (src == dest)
            {
              printf ("Wrong connections 2!\n");
              exit (1);
            }
          cout << "When src == dst -> r:" << r << " src: " << src << " dest: "
              << dest << " is_dest[dest]: " << is_dest[dest] << endl;
        }
      is_dest[dest] = 1;
      destinations->push_back (dest);
      connections[src] = destinations;
    }
}

void
ConnectionMatrix::setRandomConnection (int cnx)
{
  for (int conn = 0; conn < cnx; conn++)
    {
      int dest = 0;
      int src = rand () % N;
      do
        {
          dest = rand () % N;
        }
      while (src == dest);

      if (connections.find (src) == connections.end ())
        {
          connections[src] = new vector<int> ();
        }
      connections[src]->push_back (dest);
    }
}

void
ConnectionMatrix::setStride (int S)
{
  for (int src = 0; src < N; src++)
    {
      int dest = (src + S) % N;

      connections[src] = new vector<int> ();
      connections[src]->push_back (dest);
    }
}

vector<connection*>*
ConnectionMatrix::getAllConnections ()
{
  vector<connection*>* ret = new vector<connection*> ();
  vector<int>* destinations;
  map<int, vector<int>*>::iterator it;

  for (it = connections.begin (); it != connections.end (); it++)
    {
      int src = (*it).first;
      destinations = (vector<int>*) (*it).second;

      vector<int> subflows_chosen;

      for (unsigned int dst_id = 0; dst_id < destinations->size (); dst_id++)
        {
          connection* tmp = new connection ();
          tmp->src = src;
          tmp->dst = destinations->at (dst_id);
          tmp->large = false;
          ret->push_back (tmp);
        }
    }
  return ret;
}

vector<connection*>*
ConnectionMatrix::getAllNonConnections ()
{
  vector<connection*>* ret = new vector<connection*> ();
  for (int i = 0; i < N; i++)
    {

      if (connections.find (i) != connections.end ())
        continue;
      else
        {
          connection* tmp = new connection ();
          tmp->src = i;
          ret->push_back (tmp);
        }
    }
  return ret;
}

} // end of namespace
