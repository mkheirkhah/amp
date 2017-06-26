/*
 * Author: Morteza Kheirkhah <m.kheirkhah@ed.ac.uk>
 */

#include <ctime>
#include <ctype.h>
#include <sys/time.h>
#include <stdint.h>
#include <fstream>
#include <string>
#include <cassert>
#include <iostream>  // std::cout; std::fixed
#include <iomanip>   // std::setprecision
#include <algorithm> // std::min|max
#include "ns3/log.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/netanim-module.h"
#include "ns3/mobility-module.h"
#include "ns3/callback.h"
#include "ns3/string.h"

#define MAXDATASIZE 1400
#define MAXPACKETSIZE 1500

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("FatTree");

NodeContainer host[8][4];  // NodeContainer for hosts

typedef enum
{
  Core, Aggr, Tor, Host, Core_Aggr, Core_Aggr_Tor, Core_Aggr_Tor_Host
} Layers_t;

typedef enum
{
  NONE, PERMUTATION, STRIDE, RANDOM, SHORT_FLOW, INCAST_STRIDE, INCAST_DIST, RANDOM_DIST, PERMUTATION_DIST, P_FABRIC, P_PERMUTATION
} TrafficMatrix_t;

typedef enum
{
  TCP, MPTCP
} SocketType_t;

typedef enum
{
  SF_NONE, SF_TCP, SF_MPTCP, SF_DCTCP, SF_ECN, SF_XMP
} ShortFlowType_t;

typedef enum
{
  SHORT, LARGE, BACKGROUND
} FlowType_t;

//-------------------------------- XMP --------------------------------//
uint32_t g_XmpGamma = 1;
uint32_t g_XmpBeta = 4;
//---------------------------- INCAST_DIST ----------------------------//
double   readArrivalInterval = 50.0;   // ms, doc: 10us ~ 1ms
uint32_t maxTotalLargeFlows = 2000;
uint32_t readRequestFlowSize = 2048;   // 2KB
uint32_t readResponseFlowSize = 65536; // 64KB
uint32_t jobId = 0;
uint32_t readFlows = 8; // Number of parallel flows;
uint32_t totalJobs = 0;
uint32_t activeJobs = 0;
uint32_t readJobs = 8;  // Number of parallel jobs;
uint32_t activeLargeFlows = 0;
uint32_t peakActiveLarges = 0;
uint32_t peakActiveJobs = 0;
uint64_t totalLargeFlows = 0;
uint32_t largeFlowCap = 4; // each node receives no more than 4 of long flows
std::vector<Ptr<Node> > hostArray;
//---------------------------- INCAST_DIST ----------------------------//
std::map<string, TrafficMatrix_t> stringToTrafficMatrix;
std::map<string, ShortFlowType_t> stringToShortFlowType;
std::map<string, SocketType_t> stringToSocketType;
std::map<string, Layers_t> stringToHotSpotLayers;
vector<ApplicationContainer> sourceShortFlowApps;
vector<ApplicationContainer> sourceLargeFlowApps;
vector<ApplicationContainer> sinkApps;
vector<uint32_t> dstList;
vector<pair<double, double> > totalArrival;
vector<double> poissonArrival;
uint32_t g_totalArrivals = 0;

// Setup general parameters
string   g_topology = "FT";
string   g_simName  = "S0";
string   g_linkCapacity = "1000Mbps"; // 100Mbps
string   g_linkDelay = "20us";        // link delay of 20us; Max RTT: 20 * 12 = 240us
uint32_t g_flowSize = 0;              // 0 i.e., Unlimited used for large flows
uint32_t g_shortFlowSize = 70000;     // 70Kb
uint32_t g_simTime = 1;               // default is 20 Seconds
uint32_t g_seed = 0;                  // RNG seed
double   g_lamda = 256;               // 256 flows/sec
uint32_t g_FlowSizeThresh = 1 * 1024 * 1024; // 1MB;
SocketType_t g_socketType = MPTCP;
TrafficMatrix_t g_trafficMatrix = SHORT_FLOW;
TrafficMatrix_t g_shortFlowTM = PERMUTATION;
TrafficMatrix_t g_backgroundFlowTM = NONE;

uint32_t g_connxLimit = 33; // 33% large flows if it being used
string g_simInstance = "0"; // Dynamically adjust at cmd
string g_simStartTime = "NULL";
bool g_enableLfPlotting = false;
bool g_enableSfPlotting = false;
bool g_largeFlowDCTCP = false;
double g_arrivalUpperBound = 0.2;
double g_shortFlowStartTime = 0.0;
ShortFlowType_t g_shortFlowType = SF_NONE;
bool g_enableRED = false;
bool g_enableDCTCP= false;
bool g_enableDTQmark= false;
double g_REDmaxTh = 10;
double g_REDminTh = 10;
double g_REDWeight = 1.0;
uint32_t g_queueLimit = 100;
uint32_t g_queueCounter = 0;
double   g_DCTCPWeight = 1.0 / 16.0;
double   g_queueSampInterval = 0.001; // 1ms is default
uint32_t g_queueMaxCounter = static_cast<uint32_t>(round(g_simTime/g_queueSampInterval));
bool g_dctcpFastReTxRecord = false;
bool g_hasHostQueueSetup = false;
bool g_simpleQueueTrace = false;
uint32_t g_DTQmarkTh = 10;        // Marking threshold with a droptail queue
bool g_enabledHostMarking = true; // If true, the queues in the host layer would also mark packets.
bool g_queueModeBytes = false;    // If true, the queues perform per bytes rather than packets.
bool g_shortFlowSizeUniRandom = true;
bool g_shortFlowSizeDist = false;
bool g_enableRatePlotting = false;
bool g_isSimActive = true;
bool g_slowDownXmpLike = false;
bool g_slowDownEcnLike = false;
bool g_dynamicSubflow = false;
double   g_rateBeat = 0.1;
uint32_t g_incastThreshold = 3;
uint32_t g_incastExitThreshold = 8;

// Cwnd/Rwnd
uint32_t g_cwndMin = 1;
uint32_t g_rwndScale = 1;

uint32_t g_rGap = 50;

uint16_t g_torLayerConnx = 0;
uint16_t g_aggrLayerConnx = 0;
uint16_t g_coreLayerConnx = 0;

uint32_t AllFlow[3];
uint32_t LargeFlow[3];
uint32_t ShortFlow[3];

// Setup MPTCP parameters
string   g_cc = "RTT_Compensator"; // Uncoupled_TCPs, Linked_Increases, Fully_Coupled
uint32_t g_subflows = 8;
uint32_t g_shortFlowSubflows = 1;

// Setup topology parameters
uint32_t g_ratio = 1;
uint32_t g_K = 8;                         // No. of switch's ports : 8
uint32_t g_numPod = g_K;                  // No. of pods           : 8
uint32_t g_numHost = (g_K / 2) * g_ratio; // Hosts per ToR switch  : 4
uint32_t g_numToR = g_K / 2;              // ToR switches per pod  : 4
uint32_t g_numAggr = g_K / 2;             // Aggr switches per pod : 4
uint32_t g_numCore = g_K / 2;             // Core switches in group: 2
uint32_t g_numGroup = g_K / 2;            // Core switches in group: 2
uint32_t g_totalHost = ((g_K * g_K * g_K) / 4) * g_ratio;
uint32_t g_totalToR = g_numToR * g_numPod;
uint32_t g_totalAggr = g_numAggr * g_numPod;
uint32_t g_totalCore = g_numCore * g_numGroup;

// [Second][Metrics][Node][Dev]
double core_data[25][2][1024][64];
double aggr_data[25][2][1024][64];
double tor_data [25][2][1024][64];
double host_data[25][2][1024][2];

double totalCoreUtil, totalCoreLoss, totalAggrUtil, totalAggrLoss, totalTorUtil, totalTorLoss, totalHostUtil, totalHostLoss = 0.0;
double meanCoreUtil,  meanCoreLoss,  meanAggrUtil,  meanAggrLoss,  meanTorUtil,  meanTorLoss,  meanHostUtil,  meanHostLoss  = 0.0;

double sumCoreUtil, sumAggrUtil, sumTorUtil, sumHostUtil = 0.0;
double sumCoreLoss, sumAggrLoss, sumTorLoss, sumHostLoss = 0.0;
uint32_t countCore, countAggr,   countTor,   countHost   = 0.0;

// NodeContainer to use for link utilisation and loss rate
NodeContainer core_c;
NodeContainer Aggr_c;
NodeContainer Tor_c;
NodeContainer Host_c;

void FlowExit(Ptr<Node> node, uint32_t index, uint32_t size, double elapse, double rate);
void SetupShortFlow(MpTcpBulkSendHelper &source, string &socketModelTmp);
uint32_t TransferData(Ptr<Node> src, Ptr<Node> des, uint32_t size);
string SetupSimFileName(string input);
string GetFlowLayer(Ipv4Address ipv4Src, Ipv4Address ipv4Dst);
string GetSocketModel();
void cmAnalisys(FlowType_t ft, Ipv4Address ipv4Src, Ipv4Address ipv4Dst);
uint32_t GetReTxThresh(Ipv4Address ipv4Src, Ipv4Address ipv4Dst);
uint64_t GetLinkRate(string linkRate);
string GetKeyFromValueTM(TrafficMatrix_t tm);
string GetIpv4AddressFromNodeInStr(Ptr<Node> node);
Ipv4Address GetIpv4AddressFromNode(Ptr<Node> node);
void SimHeaderWritter(Ptr<OutputStreamWrapper> stream);
void SimFooterWritter(Ptr<OutputStreamWrapper> stream);
//---------------------------- pFabric  ----------------------------//
// variables for pFabric traffic generation
string   pTraffic = "WEB";  // DM: data mining; WEB: web services
double   pload = 0.0;       // load to apply to the network, from 0.0 to 1.0 (0%, 100%)
double   linkSpeed;         // link speed (Mbps), used for pFabric traffic, g_linkCapacity
int      pFabSeed;          // seed to use for pFabric flow generation
uint16_t startPort = 50000; // assign increasing port values to flows
uint64_t g_totalShortFlows = 0;
uint64_t g_totalLargeFlows = 0;

struct cdfStruct
{ // contains points of the CDF
  double yCDF; // value corresponding to the CDF, e.g. flow size (packets)
  int xFlowSize;
};

vector<cdfStruct> cdfData; // flow size CDF
const int meanFlowSize[] = { 5117 * MAXDATASIZE, 1138 * MAXDATASIZE, 134 * MAXDATASIZE };  // data mining, web services, imc

struct pFlow
{
  uint32_t iSrc, iDst;  // source and destination IP
  uint64_t time;        // time at which the flow should start (ms)
  int size;             // size of the flow (packets)
  string srcIP, dstIP;  // source and destination IP for the flow
  int srcPort, dstPort; // source and destination TCP port (from the sender's perspective)
};

vector<pFlow> flowList; // flows to generate
int flowNum = 16256;    // number of flows to generate in the simulation, set it from cli

// Functions for pFabric traffic generation
bool
GetCDFData (string traf)
{
    // populate the traffic array with CDF of flow sizes
    // perhaps have these read from a file in the future
    // traf == "DM" or "WEB"
    cdfStruct temp = {0.0, 0};

    if (traf == "DM") {
        // use Data Mining traffic, there are 9 elements
        cdfData.assign(9, temp);   // initialise the list
        cdfData.at(0).xFlowSize = 1; cdfData.at(0).yCDF = 0.0;
        cdfData.at(1).xFlowSize = 1; cdfData.at(1).yCDF = 0.5;
        cdfData.at(2).xFlowSize = 2; cdfData.at(2).yCDF = 0.6;
        cdfData.at(3).xFlowSize = 3; cdfData.at(3).yCDF = 0.7;
        cdfData.at(4).xFlowSize = 7; cdfData.at(4).yCDF = 0.8;
        cdfData.at(5).xFlowSize = 267; cdfData.at(5).yCDF = 0.9;
        cdfData.at(6).xFlowSize = 2107; cdfData.at(6).yCDF = 0.95;
        cdfData.at(7).xFlowSize = 66667; cdfData.at(7).yCDF = 0.99;
        cdfData.at(8).xFlowSize = 666667; cdfData.at(8).yCDF = 1.0;
    } else if (traf == "WEB") {
        // use WEB traffic, there are 12 elements
        cdfData.assign(12, temp);   // initialise the list
        cdfData.at(0).xFlowSize = 6; cdfData.at(0).yCDF = 0.0;
        cdfData.at(1).xFlowSize = 6; cdfData.at(1).yCDF = 0.15;
        cdfData.at(2).xFlowSize = 13; cdfData.at(2).yCDF = 0.2;
        cdfData.at(3).xFlowSize = 19; cdfData.at(3).yCDF = 0.3;
        cdfData.at(4).xFlowSize = 33; cdfData.at(4).yCDF = 0.4;
        cdfData.at(5).xFlowSize = 53; cdfData.at(5).yCDF = 0.53;
        cdfData.at(6).xFlowSize = 133; cdfData.at(6).yCDF = 0.6;
        cdfData.at(7).xFlowSize = 667; cdfData.at(7).yCDF = 0.7;
        cdfData.at(8).xFlowSize = 1333; cdfData.at(8).yCDF = 0.8;
        cdfData.at(9).xFlowSize = 3333; cdfData.at(9).yCDF = 0.9;
        cdfData.at(10).xFlowSize = 6667; cdfData.at(10).yCDF = 0.97;
        cdfData.at(11).xFlowSize = 20000; cdfData.at(11).yCDF = 1.0;
    } else if (traf == "IMC") {
    // use IMC traffic, there are 12 elements
        cdfData.assign(12, temp);   // initialise the list
        cdfData.at(0).xFlowSize  = 1; cdfData.at(0).yCDF  = 0.0;
        cdfData.at(1).xFlowSize  = 1; cdfData.at(1).yCDF  = 0.5;
        cdfData.at(2).xFlowSize  = 2; cdfData.at(2).yCDF  = 0.6;
        cdfData.at(3).xFlowSize  = 3; cdfData.at(3).yCDF  = 0.7;
        cdfData.at(4).xFlowSize  = 5; cdfData.at(4).yCDF  = 0.75;
        cdfData.at(5).xFlowSize  = 7; cdfData.at(5).yCDF  = 0.8;
        cdfData.at(6).xFlowSize  = 40;  cdfData.at(6).yCDF  = 0.8125;
        cdfData.at(7).xFlowSize  = 72;  cdfData.at(7).yCDF  = 0.8250;
        cdfData.at(8).xFlowSize  = 137; cdfData.at(8).yCDF  = 0.85;
        cdfData.at(9).xFlowSize  = 267; cdfData.at(9).yCDF  = 0.9;
        cdfData.at(10).xFlowSize = 1187;  cdfData.at(10).yCDF = 0.95;
        cdfData.at(11).xFlowSize = 2107;  cdfData.at(11).yCDF = 1.0;
    } else {
        // bad option
        cout << "Bad option in GetCDFData(...)" << endl;
        exit(EXIT_FAILURE);
    }
    cout << "Got " << traf << " flow size CDF data" << endl;
    return true;
}

bool
PrintPFlowsToFile(string file)
{ // print flows generated with the pFabric model to file
  ofstream write(file.c_str());
  if (write.is_open())
    { // put a header in the file
      write << "#time\tsrcNode\tdstNode\tsize\tsrcIP\t\tdstIP\t\tsrcPort\tdstPort";
      write << "\n";
      for (int i = 0; i < flowNum; i++)
        {
          write << flowList.at(i).time;
          write << "\t" << flowList.at(i).iSrc;
          write << "\t" << flowList.at(i).iDst;
          write << "\t" << flowList.at(i).size;
          write << "\t" << flowList.at(i).srcIP;
          write << "\t" << flowList.at(i).dstIP;
          write << "\t" << flowList.at(i).srcPort;
          write << "\t" << flowList.at(i).dstPort;
          write << "\n";
        }
      write.close();
    }
  else
    {
      cout << "Could not open file to save pFlows" << endl;
      return false;
    }
  return true;
}

double
GetITTime (double r, double l, int mFS, double load = 1.0)
{ // get the inter-arrival time between every flow (time to start each flow)
  // uses mean flow size to figure out parameters for poisson distribution
  // also take into account a load value representing how much bandwidth of the channel is to be used in average
  // if load is not specified uses 1.0
  // use exponential distribution function to figure out flow inter-arrival time
  // flows are in order of generation, inter-arrival time dictates the arrival time between flows
  // x = log(1-u)/(-lambda)
  double temp;  // milliseconds of inter-flow arrival
  temp = log (1 - r) / (-l);
  return temp;
}

uint64_t
ExtrapolateFlowSize (double c)
{ // finds the correct flow size to attribute to a particular value of CDF
  // returns the size of the flow to be generated, in packets
  double diff = 1.0;  // maximum difference for any two CDF value is 1
  int index = 0;
  int size = 0;       // size of the CDF array
  if (pTraffic == "DM")
    {
      size = 9;
    }
  else if (pTraffic == "WEB")
    {
      size = 12;
    }
  else if (pTraffic == "IMC")
    {
      size = 12;
    }
  // find the closest value of CDF to c
  for (int i = 0; i < size; i++)
    {
      if (abs (cdfData.at (i).yCDF - c) < diff)
        {
          // new smallest found
          diff = abs (cdfData.at (i).yCDF - c);
          index = i;
        }
      else
        {
          break;
        }
    }
  // use linear extrapolation to find a value between the points
  // use this equation X = (Y - Yk-1)/(Yk - Yk-1) * (Xk - Xk-1) + Xk-1
  // X is the desired flow size and Y is the actual CDF from random number
  // need to assign only correct values for X, Y, Xk, Yk, Xk-1, Yk-1
  double x = 0, y = 0, xk = 0, yk = 0, xk1 = 0, yk1 = 0;
  y = c;
  diff = -(cdfData.at (index).yCDF - y);
  if (diff == 0.0)
    { // rare, but can happen, give the exact flow size from CDF
      xk = cdfData.at (index).xFlowSize;
      yk = cdfData.at (index).yCDF;
      xk1 = 0.0;
      yk1 = 0.0;
    }
  else if (diff < 0.0)
    { // between index and previous sample
      xk = cdfData.at (index).xFlowSize;
      yk = cdfData.at (index).yCDF;
      xk1 = cdfData.at (index - 1).xFlowSize;
      yk1 = cdfData.at (index - 1).yCDF;
    }
  else if (diff > 0.0)
    { // between index and next sample
      xk = cdfData.at (index + 1).xFlowSize;
      yk = cdfData.at (index + 1).yCDF;
      xk1 = cdfData.at (index).xFlowSize;
      yk1 = cdfData.at (index).yCDF;
    }
  // calculate flow size
  if (y == 0)
    {
      x = 1;    // unlikely but can cause problem if not accounted for
    }
  else
    {
      x = ((y - yk1) / (yk - yk1)) * (xk - xk1) + xk1;  // see above for comment pls
    }
  // return flow size in packets
  return (uint64_t) round (x);
}

string
Ipv4ToString (int add)
{ // from an ipv4 address in integer form, return IP address xxx.xxx.xxx.xxx
  // cannot find it in the ns3 libraries
  // cout << "Ipv4ToString(...) " << add << endl;
  ostringstream ostr;
  ostr << ((add & 0xFF000000) >> 24) << ".";
  ostr << ((add & 0xFF0000) >> 16) << ".";
  ostr << ((add & 0xFF00) >> 8) << ".";
  ostr << (add & 0xFF);
  return ostr.str ();
}

void
PreComputePFlows(string type, const NodeContainer &allHosts)
{
  // compute pFabric flows using the traffic type specified
  // type="WEB" for web services, type="DM" for data mining
  // num is number of flows to generate
  cout << "PreComputePFlows(...) " << flowNum << endl;
  if (type == "WEB" || type == "DM" || type == "IMC")
    {
      int meanFS = 0;
      if (pTraffic == "DM")
        {
          meanFS = 0;
        }
      else if (pTraffic == "WEB")
        {
          meanFS = 1;
        }
      else if (pTraffic == "IMC")
        {
          meanFS = 2;
        }
      // get lambda and mean inter-arrival time. Lambda is per host, not universal
      double lambda = ((linkSpeed * pload)
          / (meanFlowSize[meanFS] * 8.0 / MAXDATASIZE * MAXPACKETSIZE))
          / (g_totalHost - 1);
      double intArr = 1 / lambda * 1000;
      cout << "lambda (per server): " << lambda << endl;
      cout << "Average intArr (per server): " << intArr << " ms" << endl;

      int tmp = (int) ((g_totalHost) * (g_totalHost - 1));
      cout << "flowNum: " << flowNum << "\ttmp: " << tmp << endl;
      // check if the number of flows entered is too small
      if (flowNum < tmp)
        {
          flowNum = (int) (tmp);
          cout << "flowNum is changed to " << flowNum << endl;
        }
      // a flow is valid if time != -1.0 or size != -1, for error checking
      pFlow temp =
        { 0, 0, 0, -1, "0.0.0.0", "0.0.0.0", 0, 0 };
      flowList.assign (flowNum, temp);   // initialise the list

      uint64_t total[g_totalHost];  // starting time of the last flow gemerated, for every source
      uint16_t p = startPort;     // port number to assign to a flow
      int totalFlows = 0;   // just to count

      // initialise total[]
      for (int i = 0; i < (int) g_totalHost; i++)
        {
          total[i] = 0;
        }
      // generate flow specs
      for (int i = 0; i < (int) (flowNum / ((g_totalHost) * (g_totalHost - 1))); i++)
        { // this makes sure that flow generation is uniform
          for (int iS = 0; iS < (int) g_totalHost; iS++)
            {    // iS defines which node will be source
              for (int iD = 0; iD < (int) g_totalHost; iD++)
                {  // iD defines which node will be destination
                  if (iS != iD)
                    { // do not allow a flow to have same source and destination
                      double r1 = drand48 ();
                      uint64_t delay = (uint64_t) (GetITTime (r1, lambda, meanFlowSize[meanFS], pload) * 1000); // flow inter-arrival time
                      total[iS] += delay;        // flow time
                      double r2 = drand48 ();    // could use r1 too
                      uint64_t size = ExtrapolateFlowSize (r2);  // get random flow size from CDF

                      // record the flow time and size
                      pFlow temp;
                      temp.time = total[iS];
                      //temp.delay = delay;
                      temp.size = size;
                      temp.srcPort = 0;       // I do not know the source port number ahead of time
                      temp.dstPort = iD + 1;       // I do not know the source port number ahead of time
                      temp.iSrc = iS;
                      temp.iDst = iD;
                      // get src and dst IP
                      Ptr<Node> srcNode = allHosts.Get (iS);  // get src IP, it complains if called "srcNode"
                      Ptr<Ipv4> ipv4Client = srcNode->GetObject<Ipv4> ();
                      Ipv4InterfaceAddress iaddrClient = ipv4Client->GetAddress (1, 0); // (0, 0) is loopback
                      Ipv4Address ipv4AddrClient = iaddrClient.GetLocal ();
                      temp.srcIP = Ipv4ToString (ipv4AddrClient.Get ());

                      Ptr<Node> dstNode = allHosts.Get (iD);
                      Ptr<Ipv4> ipv4Server = dstNode->GetObject<Ipv4> ();
                      Ipv4InterfaceAddress iaddrServer = ipv4Server->GetAddress (1, 0);
                      Ipv4Address ipv4AddrServer = iaddrServer.GetLocal ();
                      temp.dstIP = Ipv4ToString (ipv4AddrServer.Get ());
                      flowList.at (totalFlows) = temp;
                      p++;
                      totalFlows++;
                    }
                }
            }
        }
    }
  else
    {
      cout << "Bad traffic selection at PreComputePFlows(...)" << cout;
      exit (EXIT_FAILURE);
    }
}

//-------------------------   P_PERMUTATION  -------------------------//
double
CalculateLambda()
{
  return g_lamda;
}

void
ComputePermutationFlows ()
{
  flowNum = g_totalHost * (g_totalHost-1); // total flows = each host * host's flows
  pFlow temp = { 0, 0, 0, -1, "0.0.0.0", "0.0.0.0", 0, 0 };
  flowList.assign (flowNum, temp);   // initialise the list

  int totalFlows = 0; // just to count the scheduled flows

  uint64_t totalDelay[g_totalHost]; // starting time of the last flow generated, for every source
  for (int i = 0; i < (int) g_totalHost; i++)
    { // initialise totalDelay[]
      totalDelay[i] = 0;
    }

  static UniformVariable uniformPermutation;
  std::vector<Ptr<Node> > des = hostArray;
  uint32_t totalHost = des.size ();
  assert(totalHost == g_totalHost);
  uint32_t s = 0, d = 0;
  uint32_t hostFlows = 0; // flows per host
  uint32_t connFailed = 0;
  while (totalHost)
    {
      d = uniformPermutation.GetInteger (0, totalHost - 1);
      cout << "Src -> Dst (" << hostArray[s]->GetId() << " -> " << des[d]->GetId() << ")" << endl;

      // try to prevent a source connecting to itself (as needed in Permutation)
      if (s == d)
        {
          if (connFailed > 10000) exit(1); // better to exit in such cases
          connFailed++;
          cout << "src == dst (" << connFailed << ")" << endl;
          continue;
        }
      assert (s != d);

      // computes for a fixed number of flows per host (== g_totalHost-1)
      while (hostFlows < g_totalHost-1)
        {
          uint64_t delay = (uint64_t) (exponential(CalculateLambda()) * 1000); // next arrival (ms)
          totalDelay[s] += delay;  // flow scheduling time
          uint64_t eFlowSize = ExtrapolateFlowSize (drand48 ()); // get random flow size from CDF

          pFlow temp;
          temp.time = totalDelay[s]; // (ms)
          temp.size = eFlowSize;
          temp.srcPort = 0; // set at run time
          temp.dstPort = 0; // set at run time
          temp.iSrc = s;
          temp.iDst = d;
          temp.srcIP = GetIpv4AddressFromNodeInStr (hostArray[s]);
          temp.dstIP = GetIpv4AddressFromNodeInStr (des[d]);
          flowList.at (totalFlows) = temp;
          totalFlows++;
          hostFlows++;
        }
      s++;                         // move on to the next source
      Ptr<Node> tmp = des[d];      // save current des node
      des[d] = des[totalHost - 1]; // move the last des node to the current des node
      des[totalHost - 1] = tmp;    // move saved des to the last des node
      totalHost--;                 // never use the last des node again
      hostFlows = 0;               // reset hostFlows for the next source
      connFailed = 0;              // reset connection failure for the next source
    }
}

void
ExecutePermutationFlows (const NodeContainer &allHosts)
{
  cout << "ExecutePermutationFlows(...)" << endl;
  for (int i = 0; i < flowNum; i++)
    { // get source and destination of each computed flow
      int src = flowList.at (i).iSrc;
      int dst = flowList.at (i).iDst;
      double sendTime = flowList.at (i).time;  // this is in ms
      uint64_t fSize = flowList.at (i).size;
      uint64_t flowSizeBytes = fSize * MAXDATASIZE;
      uint32_t flowId = i;
      uint16_t port = dst + 1;

      Ipv4Address ipv4AddrClient = GetIpv4AddressFromNode (allHosts.Get (src));
      Ipv4Address ipv4AddrServer = GetIpv4AddressFromNode (allHosts.Get (dst));

      MpTcpBulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address (ipv4AddrServer), port));
      source.SetAttribute ("MaxBytes", UintegerValue (flowSizeBytes)); // use computed flow size
      string socketModelTmp = GetSocketModel ();
      source.SetAttribute ("SocketModel", StringValue (socketModelTmp));

      if (flowSizeBytes < g_FlowSizeThresh)
        { // Short flow group
          ++g_totalShortFlows;
          source.SetAttribute ("FlowType", StringValue ("Short"));
          cmAnalisys (SHORT, ipv4AddrClient, ipv4AddrServer);

          if (g_shortFlowType != SF_NONE)
            { // IssuePermutation()
              SetupShortFlow(source, socketModelTmp);
            }
        }
      else
        { // Large flows group
          ++g_totalLargeFlows;
          source.SetAttribute ("FlowType", StringValue ("Large")); // do I keep this with pFabric? YES
          cmAnalisys (LARGE, ipv4AddrClient, ipv4AddrServer);
          // IssuePermutation()
          if (g_slowDownEcnLike)
            source.SetAttribute ("SlowDownEcnLike", BooleanValue (g_slowDownEcnLike));
        }
      source.SetAttribute ("FlowId", UintegerValue (flowId));
      string flowLayer = GetFlowLayer (ipv4AddrClient, ipv4AddrServer);
      source.SetAttribute ("FlowLayer", StringValue (flowLayer));
      source.SetAttribute ("OutputFileName", StringValue (SetupSimFileName ("RESULT"))); // change this?
      source.SetAttribute ("OutputFileNameDctcp", StringValue (SetupSimFileName ("DCTCP")));

      // Schedule application start
      ApplicationContainer tmp = source.Install (allHosts.Get (src)); // install application on src node
      tmp.Start (Seconds (sendTime / 1000.0));
      Ptr<MpTcpBulkSendApplication> app = DynamicCast<MpTcpBulkSendApplication>(tmp.Get(0));
      app->m_Notify = MakeCallback (FlowExit); // Keep track of completed flows
      sourceLargeFlowApps.push_back (tmp);
    }
  cout << "\nSimulation is performing with [" << pTraffic << "] workload over a Permutation matrix...\n" << endl;
}

void
SetupRightSubflows(uint32_t &sfsf, uint32_t &lfsf)
{
  switch (g_socketType)
    {
  case MPTCP:
    sfsf = g_shortFlowSubflows;
    lfsf = g_subflows;
    break;
  case TCP:
    sfsf = 1;
    lfsf = 1;
    break;
  default:
    break;
    }
}

void
AddNameToHosts()
{
  int host = 0;
  int tor = 0;
  stringstream name;
  for (uint32_t p = 0; p < g_numPod; p++)
    {
      for (uint32_t t = 0; t < g_numToR; t++)
        {
          name << "tor-" << p << "-" << t;
          Names::Add (name.str (), Tor_c.Get (tor));
          name.str ("");
          name << "aggre-" << p << "-" << t;
          Names::Add (name.str (), Aggr_c.Get (tor));
          tor++;
          name.str ("");
          for (uint32_t h = 0; h < g_numHost; h++)
            { // Hosts
              name << "host-" << p << "-" << t << "-" << h;
              Names::Add (name.str (), Host_c.Get (host));
              host++;
              name.str ("");
            }
        }
    }
}

Ipv4Address
GetIpv4AddressFromNode(Ptr<Node> node)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ipv4InterfaceAddress ipv4InterfaceAddressDst = ipv4->GetAddress (1, 0);
  Ipv4Address ipv4Address = ipv4InterfaceAddressDst.GetLocal ();
  return ipv4Address;
}

string
GetIpv4AddressFromNodeInStr(Ptr<Node> node)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ipv4InterfaceAddress ipv4InterfaceAddressDst = ipv4->GetAddress (1, 0);
  Ipv4Address ipv4Address = ipv4InterfaceAddressDst.GetLocal ();
  return Ipv4ToString(ipv4Address.Get());
}

void
InitHostArray(const NodeContainer &Host_c)
{
  for (uint32_t i = 0; i < Host_c.GetN (); i++)
    {
      Host_c.Get (i)->m_locked = 0;
      hostArray.push_back (Host_c.Get (i));
    }
}

// Note: flow size must be less than flowSizeThreshold
uint32_t
GetShortFlowSize()
{
  static UniformVariable uniformFlowSize (1, g_FlowSizeThresh/1024); // 1KB ~ 1024KB, lasting max ~20ms @ 100Mbps
  return static_cast<uint32_t> (uniformFlowSize.GetValue () * 1024); // 1024~ 1048576 B
}

// Note: flow size must be large than flowSizeThreshold
uint32_t
LargeFlowSize()
{
  if (g_trafficMatrix == PERMUTATION_DIST)
    {
      if (g_linkCapacity == "100Mbps")
        {
          static UniformVariable uniformFlowSize (6, 51);   // 64M ~ 512M, or lasting for 0.5s ~ 4s
          return static_cast<uint32_t> (uniformFlowSize.GetValue () * 1024 * 1024);
        }
      else
        { // g_linkCapacity == "1000Mbps"
          static UniformVariable uniformFlowSize (64, 512); // 64M ~ 512M, or lasting for 0.5s ~ 4s
          return static_cast<uint32_t> (uniformFlowSize.GetValue () * 1024 * 1024);
        }
    }
  else
    { // g_trafficMatrix == RANDOM_DIST or INCAST_DIST
      if (g_linkCapacity == "100Mbps")
        {
          static ParetoVariable paretoFlowSize (2.0, 1.5, 8.0); // 2/3*9M=6M,    2*96M=18M,   8*9M=72M
          double pareto = paretoFlowSize.GetValue ();
          return static_cast<uint32_t> (pareto * 9 * 1024 * 1024);
        }
      else
        { // g_linkCapacity == "1000Mbps"
          static ParetoVariable paretoFlowSize (2.0, 1.5, 8.0); // 2/3*96M=64M,  2*96M=192M,  8*96M=768M
          double pareto = paretoFlowSize.GetValue ();
          return static_cast<uint32_t> (pareto * 96 * 1024 * 1024);
        }
    }
  cout << "LargeFlowSize() -> TM is not match...exit(100) will be called...check you simulation parameters!" << endl;
  exit (100);
  return 0;
}

void
LocateHostCoordinates(Ptr<Node> node, uint32_t& ipod, uint32_t& itor, uint32_t& ihost)
{
  NS_LOG_FUNCTION_NOARGS();
  uint32_t addr = GetIpv4AddressFromNode(node).Get();
  ipod = (addr & 0x00FF0000) >> 16;
  itor = (addr & 0x0000FF00) >> 8;
  ihost= (addr & 0x000000FF);
//ihost= ((addr & 0x000000FF) - 2) / nHostAddr;
//cout << "LocateHostCoordinates()-> "<< GetIpv4AddressFromNode (node) << " = 10." << ipod << "." << itor << "." << ihost << endl;
}

Ptr<Node>
RandomPickHost(uint32_t& ipod, uint32_t& itor, uint32_t& ihost)
{
  NS_LOG_FUNCTION_NOARGS();
  static UniformVariable uniformPod (0, g_K);
  static UniformVariable uniformTor (0, g_K / 2);
  static UniformVariable uniformHost (0, g_K - (g_K/2)); // upLinksOfTOR = K/2
  static UniformVariable uniformArray (0, g_K * (g_K / 2) * (g_K - (g_K/2))); // upLinksOfTOR = K/2
  // locally : pick one node randomly from node container
  if (ipod != (uint32_t) -1 && itor != (uint32_t) -1 && ihost != (uint32_t) -1)
    {
      uint32_t i = static_cast<uint32_t> (uniformArray.GetValue ());
      LocateHostCoordinates (hostArray.at (i), ipod, itor, ihost);
      return hostArray[i];
    }
  // globally: pick one pod, tor and host randomly from their corresponding containers
  if (ipod == (uint32_t) -1)
    ipod = static_cast<uint32_t> (uniformPod.GetValue ());
  if (itor == (uint32_t) -1)
    itor = static_cast<uint32_t> (uniformTor.GetValue ());
  if (ihost == (uint32_t) -1)
    ihost = static_cast<uint32_t> (uniformHost.GetValue ());
  return host[ipod][itor].Get (ihost);
}

void
PermutationTraffic()
{
  static UniformVariable uniformPermutation;
  std::vector<Ptr<Node> > des = hostArray;
  uint32_t size = des.size();
  uint32_t s = 0, d = 0;
  while (size)
    {
      d = uniformPermutation.GetInteger(0, size - 1);
      TransferData(hostArray[s], des[d], LargeFlowSize());
      s++;
      Ptr<Node> tmp = des[d];
      des[d] = des[size - 1];
      des[size - 1] = tmp;
      size--;
    }
}

bool
IssueLargeFlows(Ptr<Node> src, Ptr<Node> des, double prob)
{
  NS_LOG_FUNCTION_NOARGS();
  static UniformVariable uniformVar (0, 1.0);
  if (uniformVar.GetValue () > prob)
    return false;
  uint32_t ipod, itor, ihost;
  while (!src || src == des)
    { // Pick Random Source
      ipod = 0, itor = 0, ihost = 0;
      src = RandomPickHost (ipod, itor, ihost);
    }
  while (!des || src == des)
    { // Pick Random Destination
      ipod = 0, itor = 0, ihost = 0;
      des = RandomPickHost (ipod, itor, ihost);
      if (largeFlowCap > 0 && des->m_locked >= largeFlowCap)
        des = 0; // re-select one
    }
  NS_ASSERT (src != des);
  uint32_t appid = TransferData (src, des, LargeFlowSize ());
  des->m_locked++; // how many large flows are destined to des node ?
  src->m_largeFlows[appid] = des;
  return true;
}

void
IssueNotInnerRackFlows(Ptr<Node> src)
{
  NS_LOG_FUNCTION_NOARGS();
  uint32_t ipod, itor, ihost, dpod, dtor, dhost;
  LocateHostCoordinates (src, ipod, itor, ihost);
  Ptr<Node> des = 0;
  do
    {
      dpod = -1, dtor = -1, dhost = -1;
      des = RandomPickHost (dpod, dtor, dhost);
//      if (largeFlowCap > 0 && des->m_locked >= largeFlowCap)
//        continue;
    }
  while ((ipod == dpod && itor == dtor) || (largeFlowCap > 0 && des->m_locked >= largeFlowCap));
  IssueLargeFlows (src, des, 1.0);
}

void
IssuePermutation (const NodeContainer &allHosts)
{
  ComputePermutationFlows ();
  ExecutePermutationFlows (allHosts);
  PrintPFlowsToFile (SetupSimFileName ("FLOW_LIST"));
}

void
SetupShortFlow(MpTcpBulkSendHelper &source, string &socketModelTmp)
{ // By activating "sfltcp", all transport schemes will use TCP for short flows
  if (g_shortFlowType != SF_NONE)
      { // ShortFlowConfig()
        switch (g_shortFlowType)
          {
        case SF_TCP:
          socketModelTmp = "TCP";
          source.SetAttribute ("DCTCP", BooleanValue (false));
          source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
          break;
        case SF_DCTCP:
          socketModelTmp = "DCTCP";
          source.SetAttribute ("DCTCP", BooleanValue (true));
          source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
          break;
        case SF_XMP:
          socketModelTmp = "XMP";
          source.SetAttribute ("DCTCP", BooleanValue (false));
          source.SetAttribute ("CongestionControl", StringValue ("XMP"));
          break;
        case SF_ECN:
          socketModelTmp = "ECN";
          source.SetAttribute ("DCTCP", BooleanValue (true));
          source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
          source.SetAttribute ("SlowDownEcnLike", BooleanValue (true));
          break;
        default:
          break;
          }
        g_shortFlowSubflows = 1;
        source.SetAttribute ("MaxSubflows", UintegerValue (g_shortFlowSubflows));
        source.SetAttribute ("SocketModel", StringValue (socketModelTmp));
      }
}

void
IssuePFabric (const NodeContainer &allHosts)
{
  pFabSeed = g_seed;  // take seed from above in the code
  linkSpeed = static_cast<double>(GetLinkRate (g_linkCapacity));

  cout << "Generating pFabric flows for " << g_totalHost << " hosts";
  cout << "\tload: " << pload;
  cout << "\tpFabSeed: " << pFabSeed;
  cout << endl;

  srand48 (pFabSeed); // set seed for random number generator
//GetCDFData (pTraffic); // populate CDF table [we populate it within the main]

  // pre-compute pFabric flows
  cout << "Computing " << flowNum << " flows...";
  PreComputePFlows (pTraffic, allHosts);
  PrintPFlowsToFile (SetupSimFileName ("FLOW_LIST"));
  cout << "DONE" << endl;
  // prepare the sending applications on every host
  // I have essentially copied this part from what is done below
  // sink apps are installed earlier, before pFabric generation traffic code
  // this could be combined with the flow generation loops, keeping them separate can avoid confusion
  for (int i = 0; i < flowNum; i++)
    { // cout << "Installing flow " << i+1 << " / " << flowNum << endl;
      // get flow source and destination
      int src = flowList.at (i).iSrc;
      int dst = flowList.at (i).iDst;
      double sendTime = flowList.at (i).time;  // this is in ms
      uint64_t fSize = flowList.at (i).size;
      uint64_t flowSizeBytes = fSize * MAXDATASIZE;
      uint32_t flowId = i;
      uint16_t port = dst + 1;

      Ptr<Node> srcNode = allHosts.Get (src);  // get src IP, it complains if called "srcNode"
      Ptr<Ipv4> ipv4Client = srcNode->GetObject<Ipv4> ();
      Ipv4InterfaceAddress iaddrClient = ipv4Client->GetAddress (1, 0);  // (0, 0) is loopback
      Ipv4Address ipv4AddrClient = iaddrClient.GetLocal ();

      Ptr<Node> dstNode = allHosts.Get (dst);
      Ptr<Ipv4> ipv4Server = dstNode->GetObject<Ipv4> ();
      Ipv4InterfaceAddress iaddrServer = ipv4Server->GetAddress (1, 0);
      Ipv4Address ipv4AddrServer = iaddrServer.GetLocal ();
      // set up application attributes
      // for InetSocketAddress(<address>, <dstport>)
      // use the sinks made previously
      MpTcpBulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address (ipv4AddrServer), port));
      source.SetAttribute ("MaxBytes", UintegerValue (flowSizeBytes));  // use computed flow size
      string socketModelTmp = GetSocketModel();
      source.SetAttribute("SocketModel", StringValue(socketModelTmp));

      if (flowSizeBytes < g_FlowSizeThresh)
        {
          ++g_totalShortFlows;
          source.SetAttribute ("FlowType", StringValue ("Short"));  // do I keep this with pFabric? YES
          cmAnalisys (SHORT, ipv4AddrClient, ipv4AddrServer);

          // By activating "sfltcp", all transport schemes will use TCP for short flows
          if (g_shortFlowType == SF_TCP || g_shortFlowType == SF_DCTCP)
            { // IssuePFabric()
              if (g_shortFlowType == SF_TCP )
                {
                  socketModelTmp = "TCP";
                  source.SetAttribute ("DCTCP", BooleanValue (false)); // DCTCP is disable by default
                }
              if (g_shortFlowType == SF_DCTCP)
                {
                  socketModelTmp = "DCTCP";
                  source.SetAttribute ("DCTCP", BooleanValue (true)); // DCTCP is disable by default
                }
              g_shortFlowSubflows = 1;
              source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
              source.SetAttribute ("MaxSubflows", UintegerValue (g_shortFlowSubflows));
              source.SetAttribute ("SocketModel", StringValue (socketModelTmp));
            }
        }
      else
        {
          ++g_totalLargeFlows;
          source.SetAttribute ("FlowType", StringValue ("Large"));  // do I keep this with pFabric? YES
          cmAnalisys (LARGE, ipv4AddrClient, ipv4AddrServer);
          // IssuePFabric()
          if (g_slowDownEcnLike)
            source.SetAttribute ("SlowDownEcnLike", BooleanValue (g_slowDownEcnLike));
        }
      source.SetAttribute ("FlowId", UintegerValue (flowId));
      string flowLayer = GetFlowLayer(ipv4AddrClient, ipv4AddrServer);
      source.SetAttribute("FlowLayer", StringValue(flowLayer));
      source.SetAttribute ("OutputFileName", StringValue (SetupSimFileName ("RESULT")));   // change this?
      source.SetAttribute ("OutputFileNameDctcp", StringValue (SetupSimFileName ("DCTCP")));

      // schedule application start
      ApplicationContainer tmp = source.Install (allHosts.Get (src)); // install application on src node
      tmp.Start (Seconds (sendTime / 1000.0));
      sourceLargeFlowApps.push_back (tmp);
    }
  cout << "\nSimulation is performing with " << pTraffic << " traffic matrix (pFabric) ...\n" << endl;
}

void
IssueDistributedRead()
{
  NS_LOG_FUNCTION_NOARGS();
  static UniformVariable uniformVar;
//static ExponentialVariable expArrival(readArrivalInterval, 10*readArrivalInterval); // ms
  static LogNormalVariable logNormalArrival (
      log (readArrivalInterval) - 0.5 * log (1.0 + 10.0 * readArrivalInterval / (readArrivalInterval * readArrivalInterval)),
      sqrt (log (1.0 + 10.0 * readArrivalInterval / (readArrivalInterval * readArrivalInterval))));

  std::vector<Ptr<Node> > hosts = hostArray;
  uint32_t totalhost = hosts.size ();
  // randomly pick a client node
  uint32_t idx = uniformVar.GetInteger (0, totalhost - 1);
  Ptr<Node> client = hosts[idx];
  hosts[idx] = hosts[totalhost - 1];
  totalhost--;
  // create a read job
  DatacenterJob_t* job = new DatacenterJob_t;
  job->tmBegin = Simulator::Now ();
  job->requestNode = client;
  job->flowNum = readFlows;
  job->jobId = ++jobId;
  job->nextJobArrival = std::min (10.0 * readArrivalInterval, logNormalArrival.GetValue ()); // ms
  // randomly pick some server nodes
  for (uint32_t i = 0; i < job->flowNum; i++)
    {
      idx = uniformVar.GetInteger (0, totalhost - 1);
      Ptr<Node> server = hosts[idx];
      hosts[idx] = hosts[totalhost - 1];
      totalhost--;
      // issue requests
      uint32_t appid = TransferData (client, server, readRequestFlowSize);
      NS_ASSERT(appid != (uint32_t ) -1 && job->requestFlows.find (appid) == job->requestFlows.end () && client->m_dcJobs.find (appid) == client->m_dcJobs.end ());
      job->requestFlows[appid] = server;
      client->m_dcJobs[appid] = job;
    }
  activeJobs++;
  peakActiveJobs = std::max (peakActiveJobs, activeJobs);
  cout << Simulator::Now ().GetSeconds () << " " << "Jobs: " << totalJobs << "/"
       << peakActiveJobs << "/" << activeJobs << " " << "NextArrival: "
       << job->nextJobArrival << "ms" << std::endl;
}

void
OutPutJobCompletion(double jct, uint32_t client, uint32_t jobId)
{
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (SetupSimFileName ("JOB"), std::ios::app);
  ostream *os = stream->GetStream ();
  *os << Simulator::Now ().GetSeconds () << " " << jct << " [$" << client << "$][+" << jobId << "+]" << "[-" << jct << "-]" << endl;
}

void
FlowExit(Ptr<Node> node, uint32_t index, uint32_t size, double elapse, double rate)
{
  //cout << node->GetId () << " " << index << " " << size << " " << endl;
}

void
TransferExit(Ptr<Node> node, uint32_t index, uint32_t size, double elapse, double rate)
{
  NS_LOG_FUNCTION(node->GetId()<< index << size);
  // Jobs
  std::map<uint32_t, DatacenterJob_t*>::iterator itJob;
  itJob = node->m_dcJobs.find (index);
  if (itJob != node->m_dcJobs.end ())
    {
      DatacenterJob_t* job = (DatacenterJob_t*) itJob->second;
      node->m_dcJobs.erase (itJob);
      if (job->requestNode == node) // request exits
        {
          std::map<uint32_t, Ptr<Node> >::iterator itRequest;
          itRequest = job->requestFlows.find (index);
          NS_ASSERT(itRequest != job->requestFlows.end ());
          Ptr<Node> server = (Ptr<Node> ) itRequest->second;
          job->requestFlows.erase (itRequest);
          // issue respond flows
          uint32_t appid = TransferData (server, node, readResponseFlowSize);
          NS_ASSERT(appid != (uint32_t ) -1 && server->m_dcJobs.find (appid) == server->m_dcJobs.end ());
          server->m_dcJobs[appid] = job;
        }
      else // respond exits
        {
          job->flowNum--;
          if (job->flowNum == 0) // job exits
            {
              NS_ASSERT(job->requestFlows.size () == 0);
              double completionTime = Simulator::Now ().GetSeconds () - job->tmBegin.GetSeconds ();
              completionTime *= 1000.0;
              double nextArrival = job->nextJobArrival; //ms
              OutPutJobCompletion (completionTime, job->requestNode->GetId (), job->jobId); // Add JCT to _JOB.data
              uint32_t tmtId = job->jobId;
              job->requestNode = 0;
              delete job;
              activeJobs--;
              totalJobs++;
              std::clog << Simulator::Now ().GetSeconds () << " Jobs: " << totalJobs << "/" << peakActiveJobs << "/" << activeJobs
                  << " JobId: " << tmtId << " JobTime: " << completionTime << "ms NextJobArrival: " << nextArrival << "ms" << std::endl;
              // issue the next job
              if (g_trafficMatrix == INCAST_STRIDE)
                {
                  if (Simulator::Now().GetSeconds() <  (static_cast<double>(g_simTime)))
                    Simulator::Schedule (Seconds (nextArrival / 1000.0), &IssueDistributedRead);
//                  if (Simulator::Now ().GetSeconds () > g_simTime)
//                    {
//                      cout << "Stop -> ST[" << Simulator::Now ().GetSeconds () << "] TM[" << GetKeyFromValueTM (g_trafficMatrix)
//                          << "] TotalJobs[" << totalJobs << "] ActiveJobs[" << activeJobs << "]" << endl;
//                      g_isSimActive = false;
//                      Simulator::Stop (Seconds (0.0));
//                    }
                }
              else
                {
                  if (totalLargeFlows + activeLargeFlows < maxTotalLargeFlows)
                    Simulator::Schedule (Seconds (nextArrival / 1000.0), &IssueDistributedRead);
                }
            }
        }
    }

  // Large flows
  std::map<uint32_t, Ptr<Node> >::iterator itLarge;
  itLarge = node->m_largeFlows.find (index);
  if (itLarge != node->m_largeFlows.end ())
    {
      Ptr<Node> desNode = itLarge->second;
      node->m_largeFlows.erase (itLarge);
      desNode->m_locked--;
    }
  if (size > g_FlowSizeThresh)
    {
      activeLargeFlows--;
      totalLargeFlows++;
      cout << Simulator::Now ().GetSeconds () << " LargeFlows: "
           << totalLargeFlows << "/" << peakActiveLarges << "/"
           << activeLargeFlows << " " << "Exit: " << size << " " << elapse << " "
           << rate << std::endl;
    }

  // cleanup app
  //node->RemoveApplication (index);

  if (g_trafficMatrix == INCAST_DIST)
    {
      if (totalLargeFlows + activeLargeFlows < maxTotalLargeFlows)
        { // issue a new large flow
          if (size > g_FlowSizeThresh)
            Simulator::ScheduleNow (&IssueNotInnerRackFlows, node);
        }
      else
        { // simulator exits
          if (activeJobs + activeLargeFlows == 0)
            {
              g_isSimActive = false;
              Simulator::Stop(Seconds(0.0));
            }
        }
    }
  else if (g_trafficMatrix == PERMUTATION_DIST)
    {
      if (activeLargeFlows == 0)
        {
          if (totalLargeFlows >= maxTotalLargeFlows)
            Simulator::Stop (Seconds (0.0));
          else
            Simulator::ScheduleNow (&PermutationTraffic);
        }
    }
  else if (g_trafficMatrix == RANDOM_DIST)
    {
      if (totalLargeFlows + activeLargeFlows < maxTotalLargeFlows)
        IssueLargeFlows (node, 0, 1.0);
      else if (activeLargeFlows == 0)
        Simulator::Stop (Seconds (0.0));
    }
  else
    {
//      if (activeJobs + activeLargeFlows == 0)
//        {
//          cout << "Traffic Matrix is undefined -> Simulator exit" << endl;
//          g_isSimActive = false;
//          Simulator::Stop (Seconds (0.0));
//        }
    }
}

uint32_t
TransferData(Ptr<Node> src, Ptr<Node> des, uint32_t flowSize)
{
  NS_LOG_FUNCTION(src->GetId() << des->GetId() << flowSize );
  if (src == des)
    return -1;

  Ipv4Address ipv4AddressDst = GetIpv4AddressFromNode(des);
  Ipv4Address ipv4AddressSrc = GetIpv4AddressFromNode(src);

  static ObjectFactory appFactory ("ns3::MpTcpBulkSendApplication");
  Ptr<MpTcpBulkSendApplication> app = appFactory.Create<MpTcpBulkSendApplication> ();
  app->SetAttribute ("Remote", AddressValue (InetSocketAddress (ipv4AddressDst, des->GetId () + 1)));
  app->SetAttribute ("MaxBytes", UintegerValue (flowSize)); // Zero is unlimited.
  string socketModelTmp = GetSocketModel();
  app->SetAttribute("SocketModel", StringValue(socketModelTmp));

  string flowType;
  uint32_t sfsf = 0, lfsf = 0;
  SetupRightSubflows(sfsf, lfsf);
  if (flowSize > 0 && flowSize <= g_FlowSizeThresh)
    {
      flowType = "Short";
      app->SetAttribute("MaxSubflows", UintegerValue(sfsf));
      app->SetAttribute("DCTCP", BooleanValue(false));  // false by default - dctcp is not active for SF

      // By activating "sfltcp", all transport schemes will use TCP for short flows
      if (g_shortFlowType == SF_TCP || g_shortFlowType == SF_DCTCP || g_shortFlowType == SF_ECN)
        { // TransferData ()
          if (g_shortFlowType == SF_TCP)
            {
              socketModelTmp = "TCP";
              app->SetAttribute ("DCTCP", BooleanValue (false)); // All short flows are standard TCP (no dctcp)
            }
          if (g_shortFlowType == SF_DCTCP)
            {
              socketModelTmp = "DCTCP";
              app->SetAttribute ("DCTCP", BooleanValue (true)); // All short flows are standard TCP (no dctcp)
            }
          if (g_shortFlowType == SF_ECN)
            {
              socketModelTmp = "ECN";
              app->SetAttribute ("DCTCP", BooleanValue (true));
              app->SetAttribute ("SlowDownEcnLike", BooleanValue (true));
            }
          g_shortFlowSubflows = 1;
          app->SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
          app->SetAttribute ("MaxSubflows", UintegerValue (g_shortFlowSubflows));
          app->SetAttribute ("SocketModel", StringValue (socketModelTmp));
        }
    }
  else
    {
      flowType = "Large";
      app->SetAttribute("MaxSubflows", UintegerValue(lfsf));
      activeLargeFlows++;
      peakActiveLarges = std::max (peakActiveLarges, activeLargeFlows);

      // TransferData()
      if (g_slowDownEcnLike)
        app->SetAttribute ("SlowDownEcnLike", BooleanValue (g_slowDownEcnLike));
    }
  app->SetAttribute("FlowType", StringValue(flowType));
  int flowId = src->GetNApplications();
  app->SetAttribute("FlowId", UintegerValue (flowId));
  string flowLayer = GetFlowLayer(ipv4AddressSrc, ipv4AddressDst);
  app->SetAttribute("FlowLayer", StringValue(flowLayer));
  string tempString = SetupSimFileName("RESULT");
  app->SetAttribute("OutputFileName", StringValue(tempString));
  app->SetAttribute("OutputFileNameDctcp", StringValue(SetupSimFileName("DCTCP")));

  if (flowType == "Short")
    cmAnalisys(SHORT, ipv4AddressSrc, ipv4AddressDst);
  else
    cmAnalisys(LARGE, ipv4AddressSrc, ipv4AddressDst);

  app->SetStartTime(Seconds (0.0));
  if (g_trafficMatrix == INCAST_STRIDE)
    app->SetStopTime (Seconds (g_simTime));

  app->m_Notify = MakeCallback (TransferExit);
  app->m_index = src->AddApplication (app); //  this node will generate start/stop events.

  if (flowType == "Large")
    {
      cout << Simulator::Now ().GetSeconds () << " " << "LargeFlows: " << totalLargeFlows << "/" << peakActiveLarges << "/"
           << activeLargeFlows << " " << "Transfer: " << flowSize << " [" << Names::FindName (src) << " " << Names::FindName (des)
           << "] {" << des->m_locked << "} [" << src->GetId () << " -> " << des->GetId () << "] AppId[" << app->m_index << "]"
           << std::endl;
    }
  return app->m_index;
}

string
isActive(bool param)
{
  if (param)
    return "On";
  else
    return "Off";
}

uint64_t
GetLinkRate(string linkRate)
{
  DataRate tmp(linkRate);
  return tmp.GetBitRate();
}

void
PrintLinkRate(Ptr<Node> node)
{
  Ptr<NetDevice> dev;
  DataRateValue str; // StringValue str;
  uint32_t devices = node->GetNDevices();
  for (uint32_t i = 1; i < devices; i++)
    {
      dev = node->GetDevice(i);
      dev->GetAttribute(string("DataRate"), str);
      cout << "Device(" << i << ") ->" << str.Get().GetBitRate() / 1000000 << "Mbps" << endl;
    }
}

// SF_NONE, SF_TCP, SF_MPTCP, SF_DCTCP, SF_ECN, SF_XMP
void
SetupStringToSFT()
{
  stringToShortFlowType["NONE"] = SF_NONE;
  stringToShortFlowType["TCP"] = SF_TCP;
  stringToShortFlowType["MPTCP"] = SF_MPTCP;
  stringToShortFlowType["DCTCP"] = SF_DCTCP;
  stringToShortFlowType["ECN"] = SF_ECN;
  stringToShortFlowType["XMP"] = SF_XMP;
}

// PERMUTATION, PERMUTATION_LIMIT, STRIDE, RANDOM, SHORT_FLOW
void
SetupStringToTM()
{
  stringToTrafficMatrix["PERMUTATION"] = PERMUTATION;
  stringToTrafficMatrix["STRIDE"] = STRIDE;
  stringToTrafficMatrix["RANDOM"] = RANDOM;
  stringToTrafficMatrix["SHORT_FLOW"] = SHORT_FLOW;
  stringToTrafficMatrix["NONE"] = NONE;
  stringToTrafficMatrix["INCAST_STRIDE"] = INCAST_STRIDE;
  stringToTrafficMatrix["INCAST_DIST"] = INCAST_DIST;
  stringToTrafficMatrix["RANDOM_DIST"] = RANDOM_DIST;
  stringToTrafficMatrix["PERMUTATION_DIST"] = PERMUTATION_DIST;
  stringToTrafficMatrix["P_FABRIC"] = P_FABRIC;
  stringToTrafficMatrix["P_PERMUTATION"] = P_PERMUTATION;
}

void
SetupStringToST()
{
  stringToSocketType["TCP"] = TCP;
  stringToSocketType["MPTCP"] = MPTCP;
}

void
SetupStringToHSL()
{
  stringToHotSpotLayers["Host"] = Host;
  stringToHotSpotLayers["Tor"]  = Tor;
  stringToHotSpotLayers["Aggr"] = Aggr;
  stringToHotSpotLayers["Core"] = Core;
}

string
GetKeyFromValueSFT(ShortFlowType_t sft)
{
  map<string, ShortFlowType_t>::const_iterator it = stringToShortFlowType.begin();
  for (; it != stringToShortFlowType.end(); it++)
    {
      if (it->second == sft)
        return it->first;
    }
  return "";
}

string
GetKeyFromValueTM(TrafficMatrix_t tm)
{
  map<string, TrafficMatrix_t>::const_iterator it = stringToTrafficMatrix.begin();
  for (; it != stringToTrafficMatrix.end(); it++)
    {
      if (it->second == tm)
        return it->first;
    }
  return "";
}

string
GetKeyFromValueST(SocketType_t st)
{
  map<string, SocketType_t>::const_iterator it = stringToSocketType.begin();
  for (; it != stringToSocketType.end(); it++)
    {
      if (it->second == st)
        return it->first;
    }
  return "";
}

string
GetKeyFromValueHSL(Layers_t tl)
{
  map<string, Layers_t>::const_iterator it = stringToHotSpotLayers.begin();
  for (; it != stringToHotSpotLayers.end(); it++)
    {
      if (it->second == tl)
        return it->first;
    }
  return "";
}

string
SetupSimFileName(string input)
{
  ostringstream oss;
  oss.str ("");
  oss << g_simName << "_" << g_topology << "_" << g_totalHost << "_"
      << GetSocketModel () << "_" << GetKeyFromValueTM (g_trafficMatrix) << "_"
      << input << "_" << g_simInstance << ".data";
  string tmp = oss.str ();
  oss.str ("");
  return tmp;
}

uint32_t
GetReTxThresh(Ipv4Address ipv4Src, Ipv4Address ipv4Dst)
{
  uint8_t src[4];
  ipv4Src.Serialize(src);
  uint8_t dst[4];
  ipv4Dst.Serialize(dst);
  if (src[1] == dst[1] && src[2] == dst[2])
    {
      return 0;
    }
  else if (src[1] == dst[1])
    {
      return g_numAggr; // 4
    }
  else
    {
      return g_totalCore; // 16
    }
}

string
GetSocketModel ()
{
  string socketmodel = "NULL";
  switch (g_socketType)
    {
  case MPTCP:
    if (g_enableDCTCP)
      socketmodel = "DCMPTCP";
    else
      socketmodel = "MPTCP";
    break;
  case TCP:
    if (g_enableDCTCP)
      socketmodel = "DCTCP";
    else
      socketmodel = "TCP";
    break;
  default:
    break;
    }
  return socketmodel;
}

string
GetFlowLayer(Ipv4Address ipv4Src, Ipv4Address ipv4Dst)
{
  uint8_t src[4];
  ipv4Src.Serialize(src);
  uint8_t dst[4];
  ipv4Dst.Serialize(dst);
  if (src[1] == dst[1] && src[2] == dst[2])
    {
      return "TOR";
    }
  else if (src[1] == dst[1])
    {
      return "AGGR"; // 4
    }
  else
    {
      return "CORE"; // 16
    }
}

uint32_t
GetSubflows(Ipv4Address ipv4Src, Ipv4Address ipv4Dst)
{
  uint8_t src[4];
  ipv4Src.Serialize(src);
  uint8_t dst[4];
  ipv4Dst.Serialize(dst);
  if (src[1] == dst[1] && src[2] == dst[2])
    {
      return 1;
    }
  else if (src[1] == dst[1])
    {
      return std::min((int)g_numAggr, 8);   // min (4, 8)
    }
  else
    {
      return std::min((int)g_totalCore, 8); // min (16, 8)
    }
}

void
cmAnalisys(FlowType_t ft, Ipv4Address ipv4Src, Ipv4Address ipv4Dst)
{
  uint8_t src[4];
  ipv4Src.Serialize(src);
  uint8_t dst[4];
  ipv4Dst.Serialize(dst);

  switch (ft)
    {
  case SHORT:
    if (src[1] == dst[1] && src[2] == dst[2])
      {
        ShortFlow[0]++;
        AllFlow[0]++;
      }
    else if (src[1] == dst[1])
      {
        ShortFlow[1]++;
        AllFlow[1]++;
      }
    else if (src[1] != dst[1])
      {
        ShortFlow[2]++;
        AllFlow[2]++;
      }
    else
      exit(1);
    break;
  case LARGE:
    if (src[1] == dst[1] && src[2] == dst[2])
      {
        LargeFlow[0]++;
        AllFlow[0]++;
      }
    else if (src[1] == dst[1])
      {
        LargeFlow[1]++;
        AllFlow[1]++;
      }
    else if (src[1] != dst[1])
      {
        LargeFlow[2]++;
        AllFlow[2]++;
      }
    else
      exit(1);
    break;
  default:
    exit(1);
    break;
    }
}

void
OutPutCMStat()
{
  double totalLF = LargeFlow[0] + LargeFlow[1] + LargeFlow[2];
  double totalSF = ShortFlow[0] + ShortFlow[1] + ShortFlow[2];
  double totalAF = AllFlow[0] + AllFlow[1] + AllFlow[2];
  // Large Flows
  double LargeTorPercentage = round(((LargeFlow[0] / totalLF) * 100));
  double LargeAggrPercentage = round(((LargeFlow[1] / totalLF) * 100));
  double LargeCorePercentage = round(((LargeFlow[2] / totalLF) * 100));
  // Short Flows
  double ShortTorPercentage = round(((ShortFlow[0] / totalSF) * 100));
  double ShortAggrPercentage = round(((ShortFlow[1] / totalSF) * 100));
  double ShortCorePercentage = round(((ShortFlow[2] / totalSF) * 100));
  // All Flows
  double AllTorPercentage = round(((AllFlow[0] / totalAF) * 100));
  double AllAggrPercentage = round(((AllFlow[1] / totalAF) * 100));
  double AllCorePercentage = round(((AllFlow[2] / totalAF) * 100));

  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(SetupSimFileName("CM"), std::ios::out);
  SimHeaderWritter(stream);
  ostream *osCM = stream->GetStream();
  *osCM << "FlowType\t"    << "ToR\t" << "Aggr\t" << "Core"   << endl;
  *osCM << "[(Large)]\t[!" << LargeTorPercentage  << "!]\t[@" << LargeAggrPercentage << "@]\t[#" << LargeCorePercentage << "#]" << endl;
  *osCM << "[(Short)]\t[!" << ShortTorPercentage  << "!]\t[@" << ShortAggrPercentage << "@]\t[#" << ShortCorePercentage << "#]" << endl;
  *osCM << "[(All)]\t[!"   << AllTorPercentage    << "!]\t[@" << AllAggrPercentage   << "@]\t[#" << AllCorePercentage   << "#]" << endl;
  SimFooterWritter(stream);
}

void
OutPutCore()
{
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(SetupSimFileName("CORE"), std::ios::out);
  ostream *os = stream->GetStream();
  for (uint32_t s = 0; s <= g_simTime; s++)            // [Seconds]
    {
      *os << s;
      for (uint32_t m = 0; m < 2; m++)                 // [Metrics]
        {
          for (uint32_t n = 0; n < core_c.GetN(); n++) // [Node]
            {
              for (uint32_t d = 1; d <= g_K; d++)      // [Dev]
                {

                  if (s == 0)
                    *os << " " << "Link" << d;
                  else
                    {
                      *os << "    " << core_data[s][m][n][d];
                      if (m == 0)        // Utilization
                        totalCoreUtil += core_data[s][m][n][d];
                      else if (m == 1)   // Loss
                        totalCoreLoss += core_data[s][m][n][d];
                    }
                }
            }
        }
      *os << endl;
    }
}

void
OutPutAggr()
{
  Ptr<OutputStreamWrapper> stream_aggr = Create<OutputStreamWrapper>(SetupSimFileName("AGGR"), std::ios::out);
  ostream *os_aggr = stream_aggr->GetStream();
  for (uint32_t s = 0; s <= g_simTime; s++)            // [Seconds]
    {
      *os_aggr << s;
      for (uint32_t m = 0; m < 2; m++)                 // [Metrics]
        {
          for (uint32_t n = 0; n < Aggr_c.GetN(); n++) // [Node]
            {
              for (uint32_t d = 1; d <= g_K; d++)      // [Dev]
                {

                  if (s == 0)
                    *os_aggr << " " << "Link" << d;
                  else
                    {
                      *os_aggr << "    " << aggr_data[s][m][n][d];
                      if (m == 0)        // Utilization
                        totalAggrUtil += aggr_data[s][m][n][d];
                      else if (m == 1)   // Loss
                        totalAggrLoss += aggr_data[s][m][n][d];
                    }
                }
            }
        }
      *os_aggr << endl;
    }
}

void
OutPutTor()
{
  Ptr<OutputStreamWrapper> stream_tor = Create<OutputStreamWrapper>(SetupSimFileName("TOR"), std::ios::out);
  ostream *os_tor = stream_tor->GetStream();
  for (uint32_t s = 0; s <= g_simTime; s++)            // [Seconds]
    {
      *os_tor << s;
      for (uint32_t m = 0; m < 2; m++)                 // [Metrics]
        {
          for (uint32_t n = 0; n < Tor_c.GetN(); n++)  // [Node]
            {
              for (uint32_t d = 1; d <= g_K; d++)      // [Dev]
                {
                  if (s == 0)
                    *os_tor << " " << "Link" << d;
                  else
                    {
                      *os_tor << "    " << tor_data[s][m][n][d];
                      if (m == 0)        // ToR Utilization
                        totalTorUtil += tor_data[s][m][n][d];
                      else if (m == 1)   // ToR Loss
                        totalTorLoss += tor_data[s][m][n][d];
                    }
                }
            }
        }
      *os_tor << endl;
    }
}

void
OutPutHost(){
  Ptr<OutputStreamWrapper> stream_host = Create<OutputStreamWrapper>(SetupSimFileName("HOST"), std::ios::out);
    ostream *os_host = stream_host->GetStream();
    for (uint32_t s = 0; s <= g_simTime; s++)            // [Seconds]
      {
        *os_host << s;
        for (uint32_t m = 0; m < 2; m++)                 // [Metrics]
          {
            for (uint32_t n = 0; n < Host_c.GetN(); n++) // [Node]
              {
                for (uint32_t d = 1; d <= 1; d++)        // [Dev]
                  {
                    if (s == 0)
                      *os_host << " " << "Link" << d;
                    else
                      {
                        *os_host << "    " << host_data[s][m][n][d];
                        if (m == 0)      // host Utilization
                          totalHostUtil += host_data[s][m][n][d];
                        else if (m == 1) // host Loss
                          totalHostLoss += host_data[s][m][n][d];
                      }
                  }
              }
          }
        *os_host << endl;
      }
}

void
PrintCMStat()
{
  const char * format = "%s \t%.1f \t%.1f \t%.1f    \t%03d \n";
  printf("\n");
  printf("FlowType\tTOR\tAggr\tCore   \t\tTotal\n");
  printf("--------\t----\t----\t-------\t\t-----\n");
  printf(format, "Large   ", round(LargeFlow[0]), round(LargeFlow[1]), round(LargeFlow[2]), g_totalLargeFlows);
  printf(format, "Short   ", round(ShortFlow[0]), round(ShortFlow[1]), round(ShortFlow[2]), g_totalShortFlows);
  printf(format, "All     ", round(AllFlow[0]), round(AllFlow[1]), round(AllFlow[2]), g_totalLargeFlows + g_totalShortFlows);
  printf("\n");
}

void
PrintSimParams()
{
  string shortFlowPlot = isActive(g_enableSfPlotting);
  string largeFlowPlot = isActive(g_enableLfPlotting);
  string redQ = isActive(g_enableRED);
  string dctcpT = isActive(g_enableDCTCP);

  cout << endl;
  cout << "Socket Type      : " << GetKeyFromValueST(g_socketType).c_str() << endl;
  cout << "SF Subflows      : " << g_shortFlowSubflows << endl;
  cout << "LF Subflows      : " << g_subflows << endl;
  cout << "Link Rate        : " << g_linkCapacity << endl;
  cout << "Switching Thrsh  : " << g_FlowSizeThresh << "B" <<endl;
  cout << "Traffic Matrix   : " << GetKeyFromValueTM(g_trafficMatrix).c_str() << endl;
  cout << "ShortFlow TM     : " << GetKeyFromValueTM(g_shortFlowTM).c_str() << endl;
  cout << "Bandwidth Ratio  : " << g_ratio << ":1" << endl;
  cout << "QueueModeBytes   : " << isActive(g_queueModeBytes) << endl;
  cout << "RED              : " << redQ.c_str() << endl;
  cout << "DCTCP            : " << dctcpT.c_str() << endl;
  cout << "XMP's Beta       : " << g_XmpBeta << endl;
  cout << "XMP's Gamma      : " << g_XmpGamma << endl;
  cout << "Host Marking     : " << isActive(g_enabledHostMarking) << endl;
  cout << "ShortFlowType    : " << GetKeyFromValueSFT(g_shortFlowType).c_str() << endl;
  cout << "SF SizeRandom    : " << isActive(g_shortFlowSizeUniRandom) << endl;
  cout << "SF SizeDist      : " << isActive(g_shortFlowSizeDist) << endl;
  cout << "LongFlows        : " << g_connxLimit << "% of totalhosts" << endl;
  cout << "Seed             : " << g_seed << endl;
  cout << "Instance         : " << g_simInstance << endl;
  cout << "ReadFlows        : " << readFlows << endl;
  cout << "ReadArrivalInterv: " << readArrivalInterval << endl;
  cout << "LargeFlowCap     : " << largeFlowCap << endl;
  cout << "DynamicSubflow   : " << isActive(g_dynamicSubflow) << endl;
  cout << "IncastThreshold  : " << g_incastThreshold << endl;
  cout << "IncastExitThresh : " << g_incastExitThreshold << endl;
  cout << "CwndMin          : " << g_cwndMin << endl;
  cout << "RcwndScale       : " << g_rwndScale << endl;
  cout << "------------------ " << endl;
  cout << "SF Plot          : " << shortFlowPlot.c_str() << endl;
  cout << "LF Plot          : " << largeFlowPlot.c_str() << endl;
  cout << "SimDuration      : " << g_simTime << endl;
  cout << "QsampInterval    : " << g_queueSampInterval << endl;
  cout << "QmaxCounter      : " << g_queueMaxCounter << endl;
  cout << "SlowDownXmpLike  : " << isActive (g_slowDownXmpLike) << endl;
  cout << "SlowDownEcnLike  : " << isActive (g_slowDownEcnLike) << endl;
  cout << "---- pFabric ----- " << endl;
  cout << "pFabricLoad      : " << pload << endl;
  cout << "pFabricTM        : " << pTraffic << endl;
  cout << endl;
}

void
SimTimeMonitor()
{
  NS_LOG_UNCOND("ClockTime: " << Simulator::Now().GetSeconds());
  double now = Simulator::Now().GetSeconds();
  cout << "[" << g_simName << "](" << g_topology << "){" << g_totalHost << "}[" << GetSocketModel() << "]{"
      << GetKeyFromValueTM(g_trafficMatrix)
      << "} -> SimClock: " << now << endl;
  if (now < g_simTime)
    Simulator::Schedule(Seconds(0.1), &SimTimeMonitor);
}

vector<connection*>*
GetShortCM(vector<connection*>* CM)
{
  vector<connection*>* ret = new vector<connection*>();
  vector<connection*>::iterator it;

  for (it = (*CM).begin(); it != (*CM).end(); it++)
    {
      if ((*it)->large == true)
        {
          continue;
        }
      else
        ret->push_back((*it));
    }
  return ret;
}

void
ShortFlowConfig(vector<connection*>* CM, const NodeContainer &allHosts)
{
  int cmSize = (*CM).size();
  int pos;
  connection* connx;
  pos = rand() % cmSize;
  connx = (*CM).at(pos);
  int src = connx->src;
  int dst = connx->dst;
  assert(connx->large == false);

  // src setup
  Ptr<Node> srcNode = allHosts.Get(src);
  Ptr<Ipv4> ipv4Src = srcNode->GetObject<Ipv4>();
  Ipv4InterfaceAddress ipv4InterfaceAddressSrc = ipv4Src->GetAddress(1, 0);
  Ipv4Address ipv4AddressSrc = ipv4InterfaceAddressSrc.GetLocal();

  // dst setup
  Ptr<Node> dstNode = allHosts.Get(dst);
  Ptr<Ipv4> ipv4Dst = dstNode->GetObject<Ipv4>();
  Ipv4InterfaceAddress ipv4InterfaceAddressDst = ipv4Dst->GetAddress(1, 0);
  Ipv4Address ipv4AddressDst = ipv4InterfaceAddressDst.GetLocal();

  // Assign flowId
  int flowId = srcNode->GetNApplications();

  // Source
  if (g_shortFlowSizeUniRandom)
    g_shortFlowSize = GetShortFlowSize (); // @ShortFlowConfig()
  if (g_shortFlowSizeDist)
    { // It throws an error if both --sfrand and --sfdist are active
      assert(g_shortFlowSizeUniRandom == 0);
      g_shortFlowSize = ExtrapolateFlowSize (drand48 ()) * MAXDATASIZE; // Sizes are per byte
    }

  MpTcpBulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address(ipv4AddressDst), dstNode->GetId() + 1));
  source.SetAttribute("MaxBytes", UintegerValue(g_shortFlowSize));
  source.SetAttribute("FlowId", UintegerValue(flowId));

  source.SetAttribute("MaxSubflows", UintegerValue(g_shortFlowSubflows));

  string socketModelTmp = GetSocketModel();
  source.SetAttribute("SocketModel", StringValue(socketModelTmp));

  if (g_shortFlowType != SF_NONE)
    { // ShortFlowConfig()
      switch (g_shortFlowType)
        {
      case SF_TCP:
        socketModelTmp = "TCP";
        source.SetAttribute ("DCTCP", BooleanValue (false));
        source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
        break;
      case SF_DCTCP:
        socketModelTmp = "DCTCP";
        source.SetAttribute ("DCTCP", BooleanValue (true));
        source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
        break;
      case SF_XMP:
        socketModelTmp = "XMP";
        source.SetAttribute ("DCTCP", BooleanValue (false));
        source.SetAttribute ("CongestionControl", StringValue ("XMP"));
        break;
      case SF_ECN:
        socketModelTmp = "ECN";
        source.SetAttribute ("DCTCP", BooleanValue (true));
        source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
        source.SetAttribute ("SlowDownEcnLike", BooleanValue (true));
        break;
      default:
        break;
        }
      g_shortFlowSubflows = 1;
      source.SetAttribute ("MaxSubflows", UintegerValue (g_shortFlowSubflows));
      source.SetAttribute ("SocketModel", StringValue (socketModelTmp));
    }

  source.SetAttribute("FlowType", StringValue("Short"));
  string flowLayer = GetFlowLayer(ipv4AddressSrc, ipv4AddressDst);
  source.SetAttribute("FlowLayer", StringValue(flowLayer));
  string tempString = SetupSimFileName("RESULT");
  source.SetAttribute("OutputFileName", StringValue(tempString));
  source.SetAttribute("OutputFileNameDctcp", StringValue(SetupSimFileName("DCTCP")));

  cmAnalisys(SHORT, ipv4AddressSrc, ipv4AddressDst);
  ApplicationContainer tmp = source.Install(srcNode);

  // Start
  tmp.Get(0)->SetStartTime(Seconds(0));

  // STOP
  double diff = (double) g_simTime - Simulator::Now().GetSeconds();
  double nextStop = (diff > 0) ? diff : 0;
  tmp.Get(0)->SetStopTime(Seconds(nextStop));

  sourceShortFlowApps.push_back(tmp); //sourceShortFlowApps[src][dst].push_back(tmp);

  // Schedule next arrival
  if (Simulator::Now().GetSeconds() <  (static_cast<double>(g_simTime) - g_arrivalUpperBound))
    {
      double nextEvent = exponential(g_lamda);
      cout << "[" << GetKeyFromValueST (g_socketType) << "]{" << GetKeyFromValueTM (g_shortFlowTM) << "}[" << flowLayer
          << "] SFT[" << GetKeyFromValueSFT(g_shortFlowType)<< "] SFSF[" << g_shortFlowSubflows
          << "] SockModel[" << socketModelTmp << "] StartNow: " << Simulator::Now ().GetSeconds ()
          << " Stop: " << nextStop << " (" << src << " -> " << dst << ") NextArrival: " << nextEvent << " flowSize(KB): " << g_shortFlowSize/1024 << endl;
      Simulator::Schedule(Seconds(nextEvent), &ShortFlowConfig, CM, allHosts);
    }
}

Ptr<Queue>
FindQueue(Ptr<NetDevice> dev)
{
  PointerValue ptr;
  dev->GetAttribute("TxQueue", ptr);
  return ptr.Get<Queue>();
}

void
SetupHostQueues ()
{ // No marking at host layer
  if (g_enableRED && g_enableDCTCP && !g_hasHostQueueSetup)
    {
      for (uint32_t i = 0; i < Host_c.GetN (); i++)
        { // dev = 1 as loop back interface should not be counted here.
          for (uint32_t j = 1; j < Host_c.Get (i)->GetNDevices (); j++)
            {
              Ptr<Queue> txQueue = FindQueue (Host_c.Get (i)->GetDevice (j));
              Ptr<RedQueue> red = DynamicCast<RedQueue> (txQueue);

              if (g_queueModeBytes)
                {
                  red->SetTh (200 * MAXDATASIZE, 200 * MAXDATASIZE);
                  red->SetQueueLimit (g_queueLimit * MAXDATASIZE);
                }
              else
                {
                  red->SetTh (200, 200);
                  red->SetQueueLimit (g_queueLimit);
                }

              DoubleValue max, min;
              UintegerValue limit;
              red->GetAttribute ("MinTh", min);
              red->GetAttribute ("MaxTh", max);
              red->GetAttribute ("QueueLimit", limit);
              cout << "Host(" << i << ") RedQueue[" << j << "] Max: " << max.Get () << " Min: " << min.Get () << " Limit: "
                  << limit.Get () << endl;
            }
        } // end of loop
      // We only setup Host's RED queues once
      g_hasHostQueueSetup = true;
    } // enf of Host's if block
  if (!g_enableRED && g_enableDCTCP && !g_hasHostQueueSetup)
      {
        for (uint32_t i = 0; i < Host_c.GetN (); i++)
          { // dev = 1 as loop back interface should not be counted here.
            for (uint32_t j = 1; j < Host_c.Get (i)->GetNDevices (); j++)
              {
                Ptr<Queue> txQueue = FindQueue (Host_c.Get (i)->GetDevice (j));
                Ptr<DropTailQueue> dTail = DynamicCast<DropTailQueue> (txQueue);
                BooleanValue isMarkableOld;
                dTail->GetAttribute("Marking", isMarkableOld);
                dTail->SetMarking(false);
                BooleanValue isMarkableNew;
                UintegerValue threhsold, maxPkts;
                dTail->GetAttribute("Marking", isMarkableNew);
                dTail->GetAttribute("MarkingTh", threhsold);
                dTail->GetAttribute("MaxPackets", maxPkts);
                cout << "Host(" << i << ") DropTailQueue[" << j << "] MaxPkts[" << maxPkts.Get() << "] MarkingTh["
                     << threhsold.Get() << "] Marking: " << isActive(isMarkableOld.Get()) << " -> " << isActive(isMarkableNew.Get())
                     << "" << endl;
              }
          } // end of loop
        // We only setup Host's DropTail queues once
        g_hasHostQueueSetup = true;
      } // enf of Host's if block
}

void
SimpleQueueTraces (const Layers_t &layer)
{ // Currently, we look at core's queues only (uplinks and downlinks)
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (SetupSimFileName ("QUEUE"), std::ios::out | std::ios::app);
  ostream* os = stream->GetStream ();

  *os << g_queueCounter << " " << Simulator::Now ().GetMilliSeconds () << " ";
  if (layer == Core || layer >= Core_Aggr)
    {
      for (uint32_t i = 0; i < core_c.GetN (); i++)
        { // j should be 1 as the loopback interface should not be counted
          for (uint32_t j = 1; j < core_c.Get (i)->GetNDevices (); j++)
            {
              Ptr<Queue> txQueue = FindQueue (core_c.Get (i)->GetDevice (j));
              if (g_queueCounter == 0.0)
                *os << "C" << i << "-" << j << " ";
              else
                *os << txQueue->GetNPackets () << " ";
            }
        } // end of loop
    } //end of the core's if block
  if (layer == Aggr || layer >= Core_Aggr)
    {
      for (uint32_t i = 0; i < Aggr_c.GetN (); i++)
        { // j should 1 as loopback interface should not be counted
          uint32_t numAggrDevices = Aggr_c.Get (i)->GetNDevices ();
          uint32_t numAggrUplinks = (Aggr_c.Get (i)->GetNDevices () - 1) / 2;
          for (uint32_t j = 1; j < numAggrDevices; j++)
            {
              //cout << "Aggr[" << i << "] numDevs[" << numAggrDevices << "] numUplinks[" << numAggrUplinks << "]" << endl;
              Ptr<Queue> txQueue = FindQueue (Aggr_c.Get (i)->GetDevice (j));
              if (j > numAggrUplinks)
                { // It should work well when K is even like 8, 10, 12 and etc.
                  if (g_queueCounter == 0.0)
                    *os << "A" << i << "-" << j << " ";
                  else
                    *os << txQueue->GetNPackets () << " ";
                }
            }
        } // end of loop
    } //end of Aggr's if block

  if (layer == Tor || layer >= Core_Aggr_Tor)
    {
      for (uint32_t i = 0; i < Tor_c.GetN(); i++)
        { // j should 1 as loopback interface should not be counted
          for (uint32_t j = 1; j <= g_numHost; j++)
            { // Loop through the number of hosts per tor.
              //cout << "Aggr[" << i << "] numDevs[" << numAggrDevices << "] numUplinks[" << numAggrUplinks << "]" << endl;
              Ptr<Queue> txQueue = FindQueue(Tor_c.Get(i)->GetDevice(j));
              if (g_queueCounter == 0.0)
                *os << "T" << i << "-" << j << " ";
              else
                *os << txQueue->GetNPackets() << " ";
            }
        } // end of loop
    } //end of Aggr's if block
  *os << endl;
  // Update g_queueCounter once...
  g_queueCounter++;

  if ((g_queueCounter < g_queueMaxCounter && ((uint32_t) Simulator::Now().GetSeconds() < g_simTime))
      || (g_trafficMatrix == INCAST_DIST && g_isSimActive))
    {
      Simulator::Schedule(Seconds(g_queueSampInterval), &SimpleQueueTraces, layer);
    }
}

void
SetupTracesNew(const Layers_t &layer)
{ // This works with traffic matrix > INCAST_DIST. This includes pFabric TMs
  uint32_t T = (uint32_t) Simulator::Now().GetSeconds();
  double sumUtil = 0.0;
  if (layer == Core || layer >= Core_Aggr)
    {
      Ptr<OutputStreamWrapper> streamUtil = Create<OutputStreamWrapper> (SetupSimFileName ("CORE_UTIL"),
                                                                         std::ios::out | std::ios::app);
      ostream* osUtil = streamUtil->GetStream ();
      Ptr<OutputStreamWrapper> streamLoss = Create<OutputStreamWrapper> (SetupSimFileName ("CORE_LOSS"),
                                                                         std::ios::out | std::ios::app);
      ostream* osLoss = streamLoss->GetStream ();
      *osUtil << T << "\t";
      *osLoss << T << "\t";
      for (uint32_t i = 0; i < core_c.GetN (); i++)
        { // j should 1 as lookback interface should not be counted
          for (uint32_t j = 1; j < core_c.Get (i)->GetNDevices (); j++)
            {
              Ptr<Queue> txQueue = FindQueue (core_c.Get (i)->GetDevice (j));
              uint32_t totalDropBytes = txQueue->GetTotalDroppedBytes ();
              uint32_t totalRxBytes = txQueue->GetTotalReceivedBytes ();
              uint32_t totalBytes = totalRxBytes + totalDropBytes;
              if (T == 0)
                {
                  *osUtil << "C" << i << "-" << "L" << j << "\t";
                  *osLoss << "C" << i << "-" << "L" << j << "\t";
                }
              else
                {
                  double util = (((double) totalRxBytes * 8 * 100) / GetLinkRate (g_linkCapacity));
                  double loss = (((double) totalDropBytes / totalBytes) * 100);
                  if (isNaN (util)) util = 0;
                  if (isNaN (loss)) loss = 0;
                  sumUtil += util;
                  *osUtil << util << "\t";
                  *osLoss << loss << "\t";
                  txQueue->ResetStatistics (); // Reset txQueue

                  sumCoreUtil += util;
                  sumCoreLoss += loss;
                  countCore++;
                }
            }
        }
      *osUtil << endl;
      *osLoss << endl;
    }
  if (layer == Aggr || layer >= Core_Aggr)
    {
      Ptr<OutputStreamWrapper> streamUtil = Create<OutputStreamWrapper> (SetupSimFileName ("AGGR_UTIL"),
                                                                         std::ios::out | std::ios::app);
      ostream* osUtil = streamUtil->GetStream ();
      Ptr<OutputStreamWrapper> streamLoss = Create<OutputStreamWrapper> (SetupSimFileName ("AGGR_LOSS"),
                                                                         std::ios::out | std::ios::app);
      ostream* osLoss = streamLoss->GetStream ();
      *osUtil << T << "\t";
      *osLoss << T << "\t";
      for (uint32_t i = 0; i < Aggr_c.GetN (); i++)
        { // j = 1 as loop back interface should not be counted
          for (uint32_t j = 1; j < Aggr_c.Get (i)->GetNDevices (); j++)
            {
              Ptr<Queue> txQueue = FindQueue (Aggr_c.Get (i)->GetDevice (j));
              uint32_t totalDropBytes = txQueue->GetTotalDroppedBytes ();
              uint32_t totalRxBytes = txQueue->GetTotalReceivedBytes ();
              uint32_t totalBytes = totalRxBytes + totalDropBytes;
              if (T == 0)
                {
                  *osUtil << "A" << i << "-" << "L" << j << "\t";
                  *osLoss << "A" << i << "-" << "L" << j << "\t";
                }
              else
                {
                  double util = (((double) totalRxBytes * 8 * 100) / GetLinkRate (g_linkCapacity));
                  double loss = (((double) totalDropBytes / totalBytes) * 100);
                  if (isNaN (util)) util = 0;
                  if (isNaN (loss)) loss = 0;
                  sumUtil += util;
                  *osUtil << util << "\t";
                  *osLoss << loss << "\t";
                  txQueue->ResetStatistics ();

                  sumAggrUtil += util;
                  sumAggrLoss+= loss;
                  countAggr++;
                }
            }
        }
      *osUtil << endl;
      *osLoss << endl;
    }
  if (layer == Tor  || layer >= Core_Aggr_Tor)
    {
      Ptr<OutputStreamWrapper> streamUtil = Create<OutputStreamWrapper> (SetupSimFileName ("TOR_UTIL"),
                                                                         std::ios::out | std::ios::app);
      ostream* osUtil = streamUtil->GetStream ();
      Ptr<OutputStreamWrapper> streamLoss = Create<OutputStreamWrapper> (SetupSimFileName ("TOR_LOSS"),
                                                                         std::ios::out | std::ios::app);
      ostream* osLoss = streamLoss->GetStream ();
      *osUtil << T << "\t";
      *osLoss << T << "\t";
      for (uint32_t i = 0; i < Tor_c.GetN(); i++)
        { // dev = 1 as loop back interface should not be counted here.
          for (uint32_t j = 1; j < Tor_c.Get(i)->GetNDevices(); j++)
            {
              Ptr<Queue> txQueue = FindQueue(Tor_c.Get(i)->GetDevice(j));
              uint32_t totalDropBytes = txQueue->GetTotalDroppedBytes();
              uint32_t totalRxBytes = txQueue->GetTotalReceivedBytes();
              uint32_t totalBytes = totalRxBytes + totalDropBytes;
              if (T == 0)
                {
                  *osUtil << "T" << i << "-" << "L" << j << "\t";
                  *osLoss << "T" << i << "-" << "L" << j << "\t";
                }
              else
                {
                  double util = (((double) totalRxBytes * 8 * 100) / GetLinkRate(g_linkCapacity));
                  double loss = (((double) totalDropBytes / totalBytes) * 100);
                  if (isNaN (util)) util = 0;
                  if (isNaN (loss)) loss = 0;
                  sumUtil += util;
                  *osUtil << util << "\t";
                  *osLoss << loss << "\t";
                  txQueue->ResetStatistics ();

                  sumTorUtil += util;
                  sumTorLoss += loss;
                  countTor++;
                }
            }
        }
      *osUtil << endl;
      *osLoss << endl;
    }
  if (layer == Host || layer >= Core_Aggr_Tor_Host)
    {
      Ptr<OutputStreamWrapper> streamUtil = Create<OutputStreamWrapper> (SetupSimFileName ("HOST_UTIL"),
                                                                         std::ios::out | std::ios::app);
      ostream* osUtil = streamUtil->GetStream ();
      Ptr<OutputStreamWrapper> streamLoss = Create<OutputStreamWrapper> (SetupSimFileName ("HOST_LOSS"),
                                                                         std::ios::out | std::ios::app);
      ostream* osLoss = streamLoss->GetStream ();
      *osUtil << T << "\t";
      *osLoss << T << "\t";
      for (uint32_t i = 0; i < Host_c.GetN(); i++)
        { // dev = 1 as loop back interface should not be counted here.
          for (uint32_t j = 1; j < Host_c.Get(i)->GetNDevices(); j++)
            {
              Ptr<Queue> txQueue = FindQueue(Host_c.Get(i)->GetDevice(j));
              uint32_t totalDropBytes = txQueue->GetTotalDroppedBytes();
              uint32_t totalRxBytes = txQueue->GetTotalReceivedBytes();
              uint32_t totalBytes = totalRxBytes + totalDropBytes;
              if (T == 0)
                {
                  *osUtil << "H" << i << "-" << "L" << j << "\t";
                  *osLoss << "H" << i << "-" << "L" << j << "\t";
                }
              else
                {
                  double util = (((double) totalRxBytes * 8 * 100) / GetLinkRate (g_linkCapacity));
                  double loss = (((double) totalDropBytes / totalBytes) * 100);
                  if (isNaN (util)) util = 0.0;
                  if (isNaN (loss)) loss = 0.0;
                  sumUtil += util;
                  *osUtil << util << "\t";
                  *osLoss << loss << "\t";
                  txQueue->ResetStatistics ();

                  sumHostUtil += util;
                  sumHostLoss += loss;
                  countHost++;
                }
            }
        }
      *osUtil << endl;
      *osLoss << endl;
    }
  if (g_isSimActive == false)
    {
      cout << "Time[" << Simulator::Now ().GetSeconds () << "] " << "sumUtil["
           << sumUtil << "] "
           << "=> No Active Flows => Simulation will be stopped now!" << endl;
      Simulator::Stop (Seconds (0.0));
    }
  else
    Simulator::Schedule (Seconds (1), &SetupTracesNew, layer);
}

// [Second][Node][Dev][Metrics]
void
SetupTraces(const Layers_t &layer)
{
  uint32_t T = (uint32_t) Simulator::Now().GetSeconds();
  if (layer == Core || layer >= Core_Aggr)
    {
      for (uint32_t i = 0; i < core_c.GetN(); i++)
        { // j should 1 as lookback interface should not be counted
          for (uint32_t j = 1; j < core_c.Get(i)->GetNDevices(); j++)
            {
              Ptr<Queue> txQueue = FindQueue(core_c.Get(i)->GetDevice(j));
              uint32_t totalDropBytes = txQueue->GetTotalDroppedBytes();
              uint32_t totalRxBytes = txQueue->GetTotalReceivedBytes();
              uint32_t totalBytes = totalRxBytes + totalDropBytes;
              core_data[T][0][i][j] = (((double) totalRxBytes * 8 * 100) / GetLinkRate(g_linkCapacity)); // Link Utilization
              core_data[T][1][i][j] = (((double) totalDropBytes / totalBytes) * 100); // LossRate
              // Make sure util is not nan
              if (isNaN(core_data[T][0][i][j]))
                core_data[T][0][i][j] = 0;
              // Make sure loss is not nan
              if (isNaN(core_data[T][1][i][j]))
                core_data[T][1][i][j] = 0;
              // Reset txQueue
              txQueue->ResetStatistics();
            }
        }
    }
  if (layer == Aggr || layer >= Core_Aggr)
    {
      for (uint32_t i = 0; i < Aggr_c.GetN(); i++)
        { // j = 1 as loop back interface should not be counted
          for (uint32_t j = 1; j < Aggr_c.Get(i)->GetNDevices(); j++)
            {
              Ptr<Queue> txQueue = FindQueue(Aggr_c.Get(i)->GetDevice(j));
              uint32_t totalDropBytes = txQueue->GetTotalDroppedBytes();
              uint32_t totalRxBytes = txQueue->GetTotalReceivedBytes();
              uint32_t totalBytes = totalRxBytes + totalDropBytes;
              aggr_data[T][0][i][j] = (((double) totalRxBytes * 8 * 100) / GetLinkRate(g_linkCapacity));  // Link Utilization
              aggr_data[T][1][i][j] = (((double) totalDropBytes / totalBytes) * 100);  // LossRate
              // Make sure aggregation util is not nan
              if (isNaN(aggr_data[T][0][i][j]))
                aggr_data[T][0][i][j] = 0;
              // Make sure aggregation loss is not nan
              if (isNaN(aggr_data[T][1][i][j]))
                aggr_data[T][1][i][j] = 0;
              txQueue->ResetStatistics();
            }
        }
    }
  if (layer == Tor || layer >= Core_Aggr_Tor)
    {
      for (uint32_t i = 0; i < Tor_c.GetN(); i++)
        { // dev = 1 as loop back interface should not be counted here.
          for (uint32_t j = 1; j < Tor_c.Get(i)->GetNDevices(); j++)
            {
              Ptr<Queue> txQueue = FindQueue(Tor_c.Get(i)->GetDevice(j));
              uint32_t totalDropBytes = txQueue->GetTotalDroppedBytes();
              uint32_t totalRxBytes = txQueue->GetTotalReceivedBytes();
              uint32_t totalBytes = totalRxBytes + totalDropBytes;
              tor_data[T][0][i][j] = (((double) totalRxBytes * 8 * 100) / GetLinkRate(g_linkCapacity));
              tor_data[T][1][i][j] = (((double) totalDropBytes / totalBytes) * 100);
              // Make sure ToR util is not nan
              if (isNaN(tor_data[T][0][i][j]))
                tor_data[T][0][i][j] = 0;
              // Make sure ToR loss is not nan
              if (isNaN(tor_data[T][1][i][j]))
                tor_data[T][1][i][j] = 0;
              txQueue->ResetStatistics();
            }
        }
    }
  if (layer == Host || layer >= Core_Aggr_Tor_Host)
    {
      for (uint32_t i = 0; i < Host_c.GetN(); i++)
        { // dev = 1 as loop back interface should not be counted here.
          for (uint32_t j = 1; j < Host_c.Get(i)->GetNDevices(); j++)
            {
              Ptr<Queue> txQueue = FindQueue(Host_c.Get(i)->GetDevice(j));
              uint32_t totalDropBytes = txQueue->GetTotalDroppedBytes();
              uint32_t totalRxBytes = txQueue->GetTotalReceivedBytes();
              uint32_t totalBytes = totalRxBytes + totalDropBytes;
              double util = (((double) totalRxBytes * 8 * 100) / GetLinkRate(g_linkCapacity));
              double loss = (((double) totalDropBytes / totalBytes) * 100);
              if (isNaN(util))
                util = 0;
              if (isNaN(loss))
                loss = 0;
              host_data[T][0][i][j] = util;
              host_data[T][1][i][j] = loss;

              txQueue->ResetStatistics();
            }
        }
    }
  if (T < g_simTime)
    Simulator::Schedule(Seconds(1), &SetupTraces, layer);
}

string
GetDateTimeNow()
{
  time_t T = time(0);
  struct tm* now = localtime(&T);
  string simStartDate = asctime(now);
  return simStartDate.substr(0, 24);
}

void
SetSimStartTime()
{
  g_simStartTime = GetDateTimeNow();
}

string
GetSimStartTime()
{
  return g_simStartTime;
}

void
SimHeaderWritter(Ptr<OutputStreamWrapper> stream)
{
  ostream *os = stream->GetStream();
  *os << "SimStart["        << g_simStartTime                         << "] "
      << "SimName["         << g_simName                              << "] "
      << "Topology["        << g_topology                             << "] "
      << "TotalHost["       << g_totalHost                            << "] "
      << "SockType["        << GetKeyFromValueST(g_socketType)        << "] "
      << "TM["              << GetKeyFromValueTM(g_trafficMatrix)     << "] "
      << "Ratio["           << g_ratio                                << "] "
      << "SFSize["          << g_shortFlowSize                        << "] "
      << "LFSize["          << g_flowSize                             << "] "
      << "LR["              << g_linkCapacity                         << "] "
      << "LD["              << g_linkDelay                            << "] "
      << "Lambda["          << g_lamda                                << "] "
      << "SimTime["         << g_simTime                              << "] "
      << "LFLimit["         << g_connxLimit                           << "] "
      << "FlowSizeThresh["  << g_FlowSizeThresh                       << "] "
      << "SFSF["            << g_shortFlowSubflows                    << "] "
      << "LFSF["            << g_subflows                             << "] "
      << "DCTCP["           << isActive(g_enableDCTCP)                << "] "
      << "RED["             << isActive(g_enableRED)                  << "] "
      << "Gamma["           << g_XmpGamma                             << "] "
      << "Beta["            << g_XmpBeta                              << "] "
      << "QMB["             << isActive(g_queueModeBytes)             << "] "
      << "HM["              << isActive(g_enabledHostMarking)         << "] "
      << "LFDCTCP["         << isActive(g_largeFlowDCTCP)             << "] "
      << "SFT["             << GetKeyFromValueSFT(g_shortFlowType)    << "] "
      << "SFRand["          << isActive(g_shortFlowSizeUniRandom)     << "] "
      << "RedMinTh["        << g_REDminTh                             << "] "
      << "RedMaxTh["        << g_REDmaxTh                             << "] "
      << "DctcpWeight["     << g_DCTCPWeight                          << "] "
      << "Seed["            << g_seed                                 << "] "
      << "Instance["        << g_simInstance                          << "] "
      << "cwndMin["         << g_cwndMin                              << "] "
      << "rcwndScale["      << g_rwndScale                            << "] "
      << "DynamicSubflow["  << g_dynamicSubflow                       << "] "
      << "IncastThreshold[" << g_incastThreshold                      << "] "
      << "IncastExitThresh["<< g_incastExitThreshold                  << "] "
      << "CC["              << g_cc                                   << "] "
      << "ReadFlows["       << readFlows                              << "] "
      << "ReadInterval["    << readArrivalInterval                    << "] "
      << "LargeFlowCap["    << largeFlowCap                           << "] "
      << "ShortFlowTM["     << GetKeyFromValueTM(g_shortFlowTM)       << "] "
      << "LFTM["            << GetKeyFromValueTM(g_backgroundFlowTM)  << "] "
      << "HostPerToR["      << g_numHost                              << "] "
			<< "pload["           << pload                                  << "] "
			<< "ptraffic["        << pTraffic                               << "] "
			<< endl;
}

void
SimFooterWritter(Ptr<OutputStreamWrapper> stream)
{
  ostream *os = stream->GetStream();
  *os << "SimEnd [" << GetDateTimeNow() << "] AllFlows[" << sourceLargeFlowApps.size() + sourceShortFlowApps.size()
      << "] LargeFlow[" << sourceLargeFlowApps.size() << "] ShortFlows[" << sourceShortFlowApps.size() << "]  CoreUtil["
      << meanCoreUtil << "] CoreLoss[" << meanCoreLoss << "] AggrUtil[" << meanAggrUtil << "] AggrLoss[" << meanAggrLoss << "]"
      << endl;
}

void
SimOverallResultWritter()
{
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(SetupSimFileName("OVERALL"), std::ios::out | std::ios::app);
  SimHeaderWritter(stream);
  ostream *os = stream->GetStream();
  if (g_trafficMatrix < INCAST_DIST)
    {
      meanCoreUtil = totalCoreUtil / (g_simTime * g_totalCore * g_K);
      meanCoreLoss = totalCoreLoss / (g_simTime * g_totalCore * g_K);
      if (isNaN (meanCoreUtil))
        meanCoreUtil = 0;
      if (isNaN (meanCoreLoss))
        meanCoreLoss = 0;
      meanAggrUtil = totalAggrUtil / (g_simTime * g_totalAggr * g_K);
      meanAggrLoss = totalAggrLoss / (g_simTime * g_totalAggr * g_K);
      if (isNaN (meanAggrUtil))
        meanAggrUtil = 0;
      if (isNaN (meanAggrLoss))
        meanAggrLoss = 0;
      meanTorUtil = totalTorUtil / (g_simTime * g_totalToR * (g_numAggr + g_numHost));
      meanTorLoss = totalTorLoss / (g_simTime * g_totalToR * (g_numAggr + g_numHost));
      if (isNaN (meanTorUtil))
        meanTorUtil = 0;
      if (isNaN (meanTorLoss))
        meanTorLoss = 0;
      meanHostUtil = totalHostUtil / (g_simTime * g_totalHost);
      meanHostLoss = totalHostLoss / (g_simTime * g_totalHost);
      if (isNaN (meanHostUtil))
        meanHostUtil = 0;
      if (isNaN (meanHostLoss))
        meanHostLoss = 0;
  }
  else
    { // g_trafficMatrix > INCAST_DIST, e.g. P_FABRIC
      meanCoreUtil = sumCoreUtil / countCore;
      meanCoreLoss = sumCoreLoss / countCore;
      if (isNaN (meanCoreUtil)) meanCoreUtil = 0;
      if (isNaN (meanCoreLoss)) meanCoreLoss = 0;
      meanAggrUtil = sumAggrUtil / countAggr;
      meanAggrLoss = sumAggrLoss / countAggr;
      if (isNaN (meanAggrUtil)) meanAggrUtil = 0;
      if (isNaN (meanAggrLoss)) meanAggrLoss = 0;
      meanTorUtil  = sumTorUtil  / countTor;
      meanTorLoss  = sumTorLoss  / countTor;
      if (isNaN (meanTorUtil)) meanTorUtil = 0;
      if (isNaN (meanTorLoss)) meanTorLoss = 0;
      meanHostUtil = sumHostUtil / countHost;
      meanHostLoss = sumHostLoss / countHost;
      if (isNaN (meanHostUtil)) meanHostUtil = 0;
      if (isNaN (meanHostLoss)) meanHostLoss = 0;
    }
  *os << "CoreUtil [!"     << meanCoreUtil << "!]\nCoreLoss [@" << meanCoreLoss << "@]\nAggrUtil [#" << meanAggrUtil
      << "#]\nAggrLoss [$" << meanAggrLoss << "$]\nTorUtil [%"  << meanTorUtil  << "%]\nTorLoss [^"  << meanTorLoss
      << "^]\nHostUtil [&" << meanHostUtil << "&]\nHostLoss [*" << meanHostLoss << "*]" << endl;
  SimFooterWritter(stream);
}

void
OutPutPoissonDist()
{
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(SetupSimFileName("POISSON"), std::ios::out);
  ostream *os = stream->GetStream();
  for (uint32_t i = 0; i < poissonArrival.size(); i++)
    {
      if (i>0)
        *os << i << "\t" << poissonArrival[i] << endl;
    }
}

void
OutPutTotalArrivals()
{
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(SetupSimFileName("ARRIVAL"), std::ios::out);
  ostream *os = stream->GetStream();
  for (uint32_t i = 0; i < totalArrival.size(); i++)
    *os << totalArrival[i].first << "\t" << totalArrival[i].second << endl;
}

void
OutPutArrivalsPerHost(vector<connection*>* CM, const NodeContainer &allHosts)
{
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(SetupSimFileName("AVERAGE"), std::ios::out);
  ostream *os = stream->GetStream();
  for (uint32_t i = 0; i < CM->size(); i++)
    {
      connection* connx = (*CM).at(i);
      Ptr<Node> srcNode = allHosts.Get(connx->src);
      uint32_t appId = srcNode->GetNApplications();
      *os << srcNode->GetId() << "\t"<< appId << endl;
    }
}

void
SimResultHeaderGenerator()
{
  Ptr<OutputStreamWrapper> streamSimParam = Create<OutputStreamWrapper>(SetupSimFileName("RESULT"),
      std::ios::out | std::ios::app);
  SimHeaderWritter(streamSimParam);
}

void
SimResultFooterGenerator()
{
  Ptr<OutputStreamWrapper> streamSimParam = Create<OutputStreamWrapper>(SetupSimFileName("RESULT"),
      std::ios::out | std::ios::app);
  SimFooterWritter(streamSimParam);
}

// Setup cmd callbacks
bool
SetNumPort(std::string input)
{
  cout << "SwitchPort: " << g_K << " -> " << input << endl;
  g_K = atoi(input.c_str());
  // Setup topology parameters
  g_numPod = g_K;                    // Pods:                        8
  g_numHost = (g_K / 2) * g_ratio;   // Host per ToR switch:         4
  g_numToR = g_K / 2;                // ToR swtiches per pod:        4
  g_numAggr = g_K / 2;               // Aggr switches per pod:       4
  g_numCore = g_K / 2;               // Core switches in group:      2
  g_numGroup = g_K / 2;              // Core switches in group:      2
  g_totalHost = ((g_K * g_K * g_K) / 4) * g_ratio;
  g_totalToR = g_numToR * g_numPod;
  g_totalAggr = g_numAggr * g_numPod;
  g_totalCore = g_numCore * g_numGroup;
  return true;
}

bool
SetNumSubflow(std::string input)
{
  cout << "Subflows         : " << g_subflows << " -> " << input << endl;
  g_subflows = atoi(input.c_str());
  return true;
}

bool
SetCongestionControl(std::string input)
{
  cout << "CongestionControl: " << g_cc << " -> " << input << endl;
  g_cc = input;
  return true;
}

bool
SetFlowSize(std::string input)
{
  cout << "FlowSize         : " << g_flowSize << " -> " << input << endl;
  g_flowSize = atoi(input.c_str());
  return true;
}

bool
SetShortFlowSize(std::string input)
{
  g_shortFlowSize = atoi(input.c_str()) * 1000;
  cout << "ShortFlowSize    : " << g_shortFlowSize << endl;
  return true;
}

bool
SetSimTime(std::string input)
{
  cout << "SimDuration      : " << g_simTime << " -> " << input << endl;
  g_simTime = atoi(input.c_str());
  g_queueMaxCounter = static_cast<uint32_t>(round(g_simTime/g_queueSampInterval));
  return true;
}

bool
SetLamda(std::string input)
{
  cout << "Lamda            : " << g_lamda << " -> " << input << endl;
  g_lamda = atoi(input.c_str());
  return true;
}

bool
SetTrafficMatrix(std::string input)
{
  cout << "TrafficMatrix    : " << GetKeyFromValueTM(g_trafficMatrix) << " -> " << input << endl;
  if (stringToTrafficMatrix.count(input) != 0)
    {
      g_trafficMatrix = stringToTrafficMatrix[input];
    }
  else
    NS_FATAL_ERROR("Input for setting up traffic matrix has spelling issue - try again!");
  return true;
}

bool
SetSFTM(std::string input)
{
  cout << "ShortFlowTM      : " << GetKeyFromValueTM(g_shortFlowTM) << " -> " << input << endl;
  if (stringToTrafficMatrix.count(input) != 0)
    {
      g_shortFlowTM = stringToTrafficMatrix[input];
    }
  else
    {
      cerr << "Input for setting up short flow raffic matrix has spelling issue - try again!" << endl;
    }
  return true;
}

bool
SetSocketType(std::string input)
{
  cout << "SocketType       : " << GetKeyFromValueST(g_socketType) << " -> " << input << endl;
  if (stringToSocketType.count(input) != 0)
    {
      g_socketType = stringToSocketType[input];
    }
  else
    NS_FATAL_ERROR("Input for setting up socket type has spelling issue - try again!");
  return true;
}

bool
SetSimInstance(std::string input)
{
  cout << "SimInstance      : " << g_simInstance << " -> " << input << endl;
  g_simInstance = input;
  return true;
}

bool
SetConnxLimit(std::string input)
{
  cout << "ConnectionLimit  : " << g_connxLimit << " -> " << input << endl;
  g_connxLimit = atoi(input.c_str());
  return true;
}

bool
SetRatio(std::string input)
{
  cout << "Bandwidth ratio  : " << g_ratio << " -> " << input << endl;
  g_ratio = atoi(input.c_str());
  g_numHost = (g_K / 2) * g_ratio;
  g_totalHost = ((g_K * g_K * g_K) / 4) * g_ratio;
  return true;
}

bool
SetLinkRate(std::string input)
{
  cout << "Link rate        : " << g_linkCapacity << " -> " << input+"Mbps" << endl;
  g_linkCapacity = input+"Mbps";
  return true;
}

bool
SetSimName(std::string input)
{
  cout << "SimName          : " << g_simName << " -> " << input << endl;
  g_simName = input;
  return true;
}

bool
SetSFSF(std::string input)
{
  cout << "shortFlowSubflows: " << g_shortFlowSubflows << " -> " << input << endl;
  g_shortFlowSubflows = atoi(input.c_str());
  return true;
}

bool
SetLfPlot(std::string input)
{
  cout << "LargeFlowPlotting: " << g_enableLfPlotting << " -> " << input << endl;
  g_enableLfPlotting = atoi(input.c_str());
  return true;
}

bool
SetSfPlot(std::string input)
{
  cout << "ShortFlowPlotting: " << g_enableSfPlotting << " -> " << input << endl;
  g_enableSfPlotting = atoi(input.c_str());
  return true;
}

bool
SetRandomGap(std::string input)
{
  cout << "SetRandomGap     : " << g_rGap << " -> " << input << endl;
  g_rGap = atoi(input.c_str());
  return true;
}

bool
SetArrivalUpperBound(std::string input)
{
  cout << "SetUpperBound    : " << g_arrivalUpperBound << " -> " << input << endl;
  g_arrivalUpperBound = atof(input.c_str());
  return true;
}

bool
SetShortFlowDCTCP(std::string input)
{ // Only for backward compatibility reason
  if (atoi (input.c_str ()) == 0)
    {
      cout << "ShortFlowDCTCP   : " << GetKeyFromValueSFT (g_shortFlowType) << " -> " << "SF_NONE" << endl;
      g_shortFlowType = SF_NONE;
    }
  else
    {
      cout << "ShortFlowDCTCP   : " << GetKeyFromValueSFT (g_shortFlowType) << " -> " << "SF_DCTCP" << endl;
      g_shortFlowType = SF_DCTCP;
    }
  return true;
}

bool
SetLargeFlowDCTCP(std::string input)
{
  cout << "LargeFlowDCTCP   : " << g_largeFlowDCTCP << " -> " << input << endl;
  g_largeFlowDCTCP = atoi(input.c_str());
  return true;
}

bool
SetSFST(std::string input)
{
  cout << "SetSFST          : " << g_shortFlowStartTime << " -> " << input << endl;
  g_shortFlowStartTime = atof(input.c_str());
  return true;
}

bool
SetFlowSizeThresh(std::string input)
{
  g_FlowSizeThresh = atoi(input.c_str());
  g_FlowSizeThresh = g_FlowSizeThresh * 1024;
  cout << "SetFlowSizeThresh: " << g_FlowSizeThresh/(double)1024 << "KB" <<endl;
  return true;
}

bool
SetRED(std::string input)
{
  cout << "Enable RED Queue : " << g_enableRED << " -> " << input << endl;
  g_enableRED = atoi(input.c_str());
  return true;
}

bool
SetDCTCP(std::string input)
{
  cout << "Enable DCTCP     : " << g_enableDCTCP << " -> " << input << endl;
  g_enableDCTCP = atoi(input.c_str());
  return true;
}
bool
SetDctcpRecord(std::string input)
{
  cout << "EnableDctcpRecord: " << g_dctcpFastReTxRecord << " -> " << input << endl;
  g_dctcpFastReTxRecord = atoi(input.c_str());
  return true;
}

bool
SetREDmin(std::string input)
{
  cout << "RED Min Threshold: " << g_REDminTh << " -> " << input << endl;
  g_REDminTh = atof(input.c_str());
  return true;
}

bool
SetREDmax(std::string input)
{
  cout << "RED Max Threshold: " << g_REDmaxTh << " -> " << input << endl;
  g_REDmaxTh = atof(input.c_str());
  return true;
}

bool
SetDTQ(std::string input)
{
  cout << "DTQ              : " << g_enableDTQmark << " -> " << input << endl;
  g_enableDTQmark = atoi (input.c_str ());
  return true;
}


bool
SetDTQMarkTh(std::string input)
{
  cout << "DTQmarkTh        : " << g_DTQmarkTh << " -> " << input << endl;
  g_DTQmarkTh = atoi (input.c_str ());
  return true;
}

bool
SetDCTCPweight(std::string input)
{
  cout << "DCTCP Weight     : " << g_DCTCPWeight << " -> " << input << endl;
  g_DCTCPWeight = atof(input.c_str());
  return true;
}

bool
SetREDweight(std::string input)
{
  cout << "RED Max Weight   : " << g_REDWeight << " -> " << input << endl;
  g_REDWeight = atof(input.c_str());
  return true;
}

bool
SetQueueLimit(std::string input)
{
  cout << "QueueLimit       : " << g_queueLimit << " -> " << input << endl;
  g_queueLimit = atof (input.c_str ());
  return true;
}

bool
SetQueueSampInterval(std::string input)
{
  cout << "QueueSampInter   : " << g_queueSampInterval << " -> " << input << endl;
  g_queueSampInterval = atof(input.c_str());
  g_queueMaxCounter = static_cast<uint32_t>(round(g_simTime/g_queueSampInterval));
  return true;
}

bool
SetHostMarking (std::string input)
{
  cout << "HostMarking      : " << g_enabledHostMarking << " -> " << input << endl;
  g_enabledHostMarking = atoi (input.c_str ());
  return true;
}
bool
SetSimpleQueueTrace (std::string input)
{
  cout << "SimpleQueueTrace : " << g_simpleQueueTrace << " -> " << input << endl;
  g_simpleQueueTrace = atoi (input.c_str ());
  return true;
}

bool
SetQueueMode (std::string input)
{
  cout << "QueueModeBytes   : " << g_queueModeBytes << " -> " << input << endl;
  g_queueModeBytes = atoi (input.c_str ());
  return true;
}

bool
SetFabricTM (std::string input)
{
  cout << "Set pFabric TM   : " << pTraffic << " -> " << input << endl;
  pTraffic = input;
  return true;
}

bool
SetFabricLoad (std::string input)
{
  cout << "Set pFabric Load : " << pload << " -> " << input << endl;
  pload = atof (input.c_str ());
  return true;
}

bool
SetFabricFlowNum (std::string input)
{
  cout << "Set pFabric flow#: " << flowNum << " -> " << input << endl;
  flowNum = atoi (input.c_str ());
  return true;
}

bool
SetShortFlowLegacyTcp (std::string input)
{ // Only for backward compatibility reason
  if (atoi (input.c_str ()) == 0)
    {
      cout << "ShortFlowLegcyTcp: " << GetKeyFromValueSFT (g_shortFlowType) << " -> " << "SF_NONE" << endl;
      g_shortFlowType = SF_NONE;
    }
  else
    {
      cout << "ShortFlowLegcyTcp: " << GetKeyFromValueSFT (g_shortFlowType) << " -> " << "SF_TCP" << endl;
      g_shortFlowType = SF_TCP;
    }
  return true;
}

bool
SetShortFlowSizeUniRandom (std::string input)
{
  cout << "ShortFlowSizeRand: " << g_shortFlowSizeUniRandom << " -> " << input << endl;
  g_shortFlowSizeUniRandom = atoi (input.c_str ());
  return true;
}

bool
SetShortFlowSizeDist (std::string input)
{
  cout << "ShortFlowSizeDist: " << g_shortFlowSizeDist << " -> " << input << endl;
  g_shortFlowSizeDist = atoi (input.c_str ());
  return true;
}

bool
SetLargeFlowCap (std::string input)
{
  cout << "LargeFlowCap     : " << largeFlowCap << " -> " << input << endl;
  largeFlowCap = atoi (input.c_str ());
  return true;
}

bool
SetReadFlows (std::string input)
{
  cout << "ReadFlows        : " << readFlows << " -> " << input << endl;
  readFlows = atoi (input.c_str ());
  return true;
}

bool
SetMaxTotalLargeFlows (std::string input)
{
  cout << "MaxTotalLargeFlow: " << maxTotalLargeFlows << " -> " << input << endl;
  maxTotalLargeFlows = atoi (input.c_str ());
  return true;
}

bool
SetXmpGamma (std::string input)
{
  cout << "XMP's Gamma      : " << g_XmpGamma << " -> " << input << endl;
  g_XmpGamma = atoi (input.c_str ());
  return true;
}

bool
SetXmpBeta (std::string input)
{
  cout << "XMP's Beta       : " << g_XmpBeta << " -> " << input << endl;
  g_XmpBeta = atoi (input.c_str ());
  return true;
}

bool
SetRatePlot (std::string input)
{
  cout << "RatePlotting     : " << g_enableRatePlotting << " -> " << input << endl;
  g_enableRatePlotting = atoi (input.c_str ());
  return true;
}

bool
SetRateBeat (std::string input)
{
  cout << "RateBeat         : " << g_rateBeat << " -> " << input << endl;
  g_rateBeat = atof (input.c_str ());
  return true;
}
bool
SetSDXL (std::string input)
{
  cout << "SlowDownXMPLike  : " << g_slowDownXmpLike << " -> " << input << endl;
  g_slowDownXmpLike = atoi (input.c_str ());
  return true;
}

bool
SetSDEL (std::string input)
{
  cout << "SlowDownEcnLike  : " << g_slowDownEcnLike << " -> " << input << endl;
  g_slowDownEcnLike = atoi (input.c_str ());
  return true;
}

bool
SetShortFlowType(std::string input)
{
  cout << "ShortFlowType    : " << GetKeyFromValueSFT (g_shortFlowType) << " -> " << input << endl;
  if (stringToShortFlowType.count (input) != 0)
    {
      g_shortFlowType = stringToShortFlowType[input];
    }
  else
    NS_FATAL_ERROR("Input for setting up short flow type has spelling issue - try again!");
  return true;
}

bool
SetDynamicSubflow (std::string input)
{
  cout << "DynamicSubflow   : " << g_dynamicSubflow << " -> " << input << endl;
  g_dynamicSubflow = atoi (input.c_str ());
  return true;
}

bool
SetRcwndScale (std::string input)
{
  cout << "RwndScale        : " << g_rwndScale << " -> " << input << endl;
  g_rwndScale = atoi (input.c_str ());
  return true;
}

bool
SetCwndMin (std::string input)
{
  cout << "CwndMin          : " << g_cwndMin << " -> " << input << endl;
  g_cwndMin = atoi (input.c_str ());
  return true;
}

bool
SetIncastThreshold (std::string input)
{
  cout << "Incast Threshold : " << g_incastThreshold << " -> " << input << endl;
  g_incastThreshold = atoi (input.c_str ());
  return true;
}

bool
SetReadArrivalInterval (std::string input)
{
  cout << "ReadArrivalInterv: " << readArrivalInterval << " -> " << input << endl;
  readArrivalInterval = atof (input.c_str ());
  return true;
}

bool
SetReadRequestFlowSize (std::string input)
{
  cout << "ReadRequestSize : " << readRequestFlowSize << " -> " << input << endl;
  readRequestFlowSize = atoi (input.c_str ());
  return true;
}

bool
SetReadResponseFlowSize (std::string input)
{
  cout << "ReadResponseSize : " << readResponseFlowSize << " -> " << input << endl;
  readResponseFlowSize = atoi (input.c_str ());
  return true;
}

bool
SetReadJobs (std::string input)
{
  cout << "ReadJobs         : " << readJobs << " -> " << input << endl;
  readJobs = atoi (input.c_str ());
  return true;
}

bool
SetIncastExitThreshold (std::string input)
{
  cout << "IncastExitThresh : " << g_incastExitThreshold << " -> " << input << endl;
  g_incastExitThreshold = atoi (input.c_str ());
  return true;
}

bool
SetLinkDelay (std::string input)
{
  cout << "LinkDelay        : " << g_linkDelay << " -> " << input+"us" << endl;
  g_linkDelay = input+"us";
  return true;
}

// Main
int
main(int argc, char *argv[])
{
  SetupStringToTM(); // Should be done before cmd parsing
  SetupStringToST();
  SetupStringToHSL();
  SetupStringToSFT();
  SetSimStartTime();

  // Enable log components
  LogComponentEnable("FatTree", LOG_ALL);

  // Set up command line parameters
  CommandLine cmd;
  cmd.AddValue("rj", "Number of parallel Jobs ", MakeCallback(SetReadJobs));
  cmd.AddValue("rrqs", "ReadReQuestSize  ", MakeCallback(SetReadRequestFlowSize));
  cmd.AddValue("rrss", "ReadReSponseSize ", MakeCallback(SetReadResponseFlowSize));
  cmd.AddValue("rai", "Read Arrival Interval ", MakeCallback(SetReadArrivalInterval));
  cmd.AddValue("iet", "Incast Threshold ",MakeCallback(SetIncastExitThreshold));
  cmd.AddValue("it", "Incast Threshold ", MakeCallback(SetIncastThreshold));
  cmd.AddValue("cwndmin", "Minimum cwnd size", MakeCallback (SetCwndMin));
  cmd.AddValue("rwndscale", "Rwnd scale factor", MakeCallback (SetRcwndScale));
  cmd.AddValue("ds", "Dynamic Subflow ", MakeCallback(SetDynamicSubflow));
  cmd.AddValue("sft", "Set short flow type ", MakeCallback(SetShortFlowType));
  cmd.AddValue("sdel", " slow down ecn like", MakeCallback (SetSDEL));
  cmd.AddValue("sdxl", " slow down xmp like", MakeCallback (SetSDXL));
  cmd.AddValue("ratebeat", " Activate Rate Plotting", MakeCallback (SetRateBeat));
  cmd.AddValue("rateplot", " Activate Rate Plotting", MakeCallback (SetRatePlot));
  cmd.AddValue("sp", "Number of Switch Port", MakeCallback(SetNumPort));
  cmd.AddValue("sf", "Number of MPTCP SubFlows", MakeCallback(SetNumSubflow));
  cmd.AddValue("cc", "MPTCP Congestion Control algorithm", MakeCallback(SetCongestionControl));
  cmd.AddValue("fs", "Flow Size", MakeCallback(SetFlowSize));
  cmd.AddValue("sfs", "Short Flow Size", MakeCallback(SetShortFlowSize));
  cmd.AddValue("st", "Simulation Time", MakeCallback(SetSimTime));
  cmd.AddValue("lamda", "Set lamda param for poisson process", MakeCallback(SetLamda));
  cmd.AddValue("cm", "Set traffic matrix", MakeCallback(SetTrafficMatrix));
  cmd.AddValue("sfcm", "Set short flow traffic matrix", MakeCallback(SetSFTM));
  cmd.AddValue("socket", "Set socket type ", MakeCallback(SetSocketType));
  cmd.AddValue("i", "Set simulation instance number as a string", MakeCallback(SetSimInstance));
  cmd.AddValue("cl", "Set connection limit for large flows", MakeCallback(SetConnxLimit));
  cmd.AddValue("ratio", "Set over subscription ratio", MakeCallback(SetRatio));
  cmd.AddValue("lr", "Set p2p link rate", MakeCallback(SetLinkRate));
  cmd.AddValue("ld", "Set Link Delay", MakeCallback (SetLinkDelay));
  cmd.AddValue("sim", "Set sim name", MakeCallback(SetSimName));
  cmd.AddValue("sfsf", "Set num of subflows for shortflow", MakeCallback(SetSFSF));
  cmd.AddValue("lfplot", "Activate plotting at MpTcpSocketBase", MakeCallback(SetLfPlot));
  cmd.AddValue("sfplot", "Activate short flow plotting at MpTcpSocketBase", MakeCallback(SetSfPlot));
  cmd.AddValue("rgap", "Set rando gap between subflows setup", MakeCallback(SetRandomGap));
  cmd.AddValue("aub", "Set arrival upper bound", MakeCallback(SetArrivalUpperBound));
  cmd.AddValue("sfdctcp", "Enable short flow to use DCTCP", MakeCallback(SetShortFlowDCTCP)); // backward compatibility
  cmd.AddValue("lfdctcp", "Enable large flow to use DCTCP", MakeCallback(SetLargeFlowDCTCP));
  cmd.AddValue("sfst", "Short flow start time", MakeCallback(SetSFST));
  cmd.AddValue("sfth", "Switching point", MakeCallback(SetFlowSizeThresh));
  cmd.AddValue("red", "Enable RED Queue Disiplone", MakeCallback(SetRED));
  cmd.AddValue("dctcp", "Enable DCTCP Capability", MakeCallback(SetDCTCP));
  cmd.AddValue("redmax", "RED Max Threshold", MakeCallback(SetREDmax));
  cmd.AddValue("redmin", "RED min Threshold", MakeCallback(SetREDmin));
  cmd.AddValue("ql", "Queue Limit (RED or DT)", MakeCallback(SetQueueLimit));
  cmd.AddValue("redweight", "RED Weight", MakeCallback(SetREDweight));
  cmd.AddValue("DCTCPweight", "DCTCP Weight", MakeCallback(SetDCTCPweight));
  cmd.AddValue("qsi", "Queue Sampling Interval", MakeCallback(SetQueueSampInterval));
  cmd.AddValue("record", "Enable DCTCP Record of Fraction and Alpha", MakeCallback(SetDctcpRecord));
  cmd.AddValue("hm", "Enable Host Marking Capability", MakeCallback(SetHostMarking));
  cmd.AddValue("sqt", "Enable Simple Queue Tracing", MakeCallback(SetSimpleQueueTrace));
  cmd.AddValue("qmb", "QUEUE_MODE_BYTES", MakeCallback(SetQueueMode));
  cmd.AddValue("dtq", "DropTailQueue Marking Threshold", MakeCallback(SetDTQ));
  cmd.AddValue("dtqmt", "DropTailQueue Marking Threshold", MakeCallback(SetDTQMarkTh));
  cmd.AddValue("ptraffic", "PFabric Traffic Matrices", MakeCallback(SetFabricTM));
  cmd.AddValue("pload", "PFabric Loads", MakeCallback(SetFabricLoad));
  cmd.AddValue("pflow", "PFabric Flow Number", MakeCallback(SetFabricFlowNum));
  cmd.AddValue("sfltcp","Short Flow Legacy TCP", MakeCallback(SetShortFlowLegacyTcp)); // backward compatibility
  cmd.AddValue("sfrand","Short Flow Size Uniform Random", MakeCallback(SetShortFlowSizeUniRandom));
  cmd.AddValue("sfdist","Short Flow Size Distribution", MakeCallback(SetShortFlowSizeDist));
  cmd.AddValue("lfcap" ," Large Flow Cap", MakeCallback(SetLargeFlowCap));
  cmd.AddValue("readflows" ," Number of parallel flows for a incast job", MakeCallback(SetReadFlows));
  cmd.AddValue("maxtlf" ," Maximum number of total large flows", MakeCallback(SetMaxTotalLargeFlows));
  cmd.AddValue("gamma"," XMP's gamma", MakeCallback(SetXmpGamma));
  cmd.AddValue("beta" ," XMP's beta",  MakeCallback(SetXmpBeta));

  cmd.Parse(argc, argv);

  Config::SetDefault ("ns3::MpTcpSocketBase::IncastExitThresh", UintegerValue(g_incastExitThreshold));
  Config::SetDefault ("ns3::MpTcpSocketBase::IncastThresh", UintegerValue(g_incastThreshold));
  Config::SetDefault ("ns3::MpTcpSocketBase::CwndMin", UintegerValue (g_cwndMin));
  Config::SetDefault ("ns3::MpTcpSocketBase::RwndScale", UintegerValue (g_rwndScale));
  Config::SetDefault ("ns3::MpTcpSocketBase::DynamicSubflow", BooleanValue(g_dynamicSubflow));
  Config::SetDefault ("ns3::MpTcpSocketBase::gamma", UintegerValue(g_XmpGamma));
  Config::SetDefault ("ns3::MpTcpSocketBase::beta", UintegerValue (g_XmpBeta));
  Config::SetDefault ("ns3::MpTcpSocketBase::SlowDownXmpLike", BooleanValue (g_slowDownXmpLike));
  Config::SetDefault ("ns3::MpTcpSocketBase::RatePlotSf", BooleanValue (g_enableRatePlotting));
  Config::SetDefault ("ns3::MpTcpSocketBase::RatePlotCl", BooleanValue (g_enableRatePlotting));
  Config::SetDefault ("ns3::MpTcpSocketBase::RateInterval", DoubleValue (g_rateBeat));

  // Set up default simulation parameters
  Config::SetDefault("ns3::Ipv4GlobalRouting::FlowEcmpRouting", BooleanValue(true));
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(MAXDATASIZE));
  Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(0));
  Config::SetDefault("ns3::DropTailQueue::Mode", StringValue("QUEUE_MODE_PACKETS"));
  Config::SetDefault("ns3::DropTailQueue::MaxPackets", UintegerValue(g_queueLimit));
  if (g_queueModeBytes)
    {
      Config::SetDefault("ns3::DropTailQueue::Mode", StringValue("QUEUE_MODE_BYTES"));
      Config::SetDefault("ns3::DropTailQueue::MaxBytes", UintegerValue(g_queueLimit * MAXDATASIZE));
    }
  Config::SetDefault("ns3::MpTcpSocketBase::LargePlotting", BooleanValue(g_enableLfPlotting));
  Config::SetDefault("ns3::MpTcpSocketBase::ShortPlotting", BooleanValue(g_enableSfPlotting));
  Config::SetDefault("ns3::MpTcpSocketBase::RandomGap", UintegerValue(g_rGap));
  Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue (true));
  if (g_trafficMatrix >= INCAST_DIST)
    { // Goodput of LFs should be calculated when all outstanding data has been acked!
      Config::SetDefault ("ns3::MpTcpSocketBase::BackgroundFlow", BooleanValue (false));
    }
  if (g_dctcpFastReTxRecord)
    { // Danger - check this carefully for normal run
      Config::SetDefault("ns3::MpTcpSocketBase::DctcpFastReTxRecord", BooleanValue(g_dctcpFastReTxRecord));
      Config::SetDefault("ns3::RedQueue::UseCurrent", BooleanValue(true));
    }
  if (g_enableDCTCP)
    {
      Config::SetDefault("ns3::MpTcpSocketBase::DCTCP", BooleanValue(g_enableDCTCP)); //Sink Control
      Config::SetDefault("ns3::MpTcpBulkSendApplication::DCTCP", BooleanValue(g_enableDCTCP));// Source Control
      Config::SetDefault("ns3::MpTcpSocketBase::DCTCPWeight", DoubleValue(g_DCTCPWeight));
      Config::SetDefault("ns3::RedQueue::UseCurrent", BooleanValue(true));
    }
  if (g_enableRED)
    {
      Config::SetDefault("ns3::RedQueue::Mode", StringValue("QUEUE_MODE_PACKETS"));
      Config::SetDefault("ns3::RedQueue::QueueLimit", UintegerValue(g_queueLimit));
      Config::SetDefault("ns3::RedQueue::MeanPktSize", UintegerValue(MAXDATASIZE));
      Config::SetDefault("ns3::RedQueue::Wait", BooleanValue(true));
      Config::SetDefault("ns3::RedQueue::Gentle", BooleanValue(false));
      Config::SetDefault("ns3::RedQueue::QW", DoubleValue(g_REDWeight));
      Config::SetDefault("ns3::RedQueue::MinTh", DoubleValue(g_REDminTh));
      Config::SetDefault("ns3::RedQueue::MaxTh", DoubleValue(g_REDmaxTh));
      Config::SetDefault("ns3::RedQueue::UseCurrent", BooleanValue(true));
      if (g_queueModeBytes)
        {
          Config::SetDefault("ns3::RedQueue::Mode", StringValue("QUEUE_MODE_BYTES"));
          Config::SetDefault("ns3::RedQueue::QueueLimit", UintegerValue(g_queueLimit * MAXDATASIZE));
          Config::SetDefault("ns3::RedQueue::MinTh", DoubleValue(g_REDminTh * MAXDATASIZE));
          Config::SetDefault("ns3::RedQueue::MaxTh", DoubleValue(g_REDmaxTh * MAXDATASIZE));
        }
    }
  if (g_enableDTQmark)
    {
      Config::SetDefault ("ns3::DropTailQueue::Marking", BooleanValue (g_enableDTQmark));
      Config::SetDefault ("ns3::DropTailQueue::MarkingTh", UintegerValue (g_DTQmarkTh));
      if (g_queueModeBytes)
        {
          Config::SetDefault ("ns3::DropTailQueue::MarkingTh", UintegerValue (g_DTQmarkTh * MAXDATASIZE));
        }
    }
  if (g_cc == "XMP")
    {
      assert(g_enableDCTCP == false);
    }

  switch (g_socketType)
    {
  case MPTCP:
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(MpTcpSocketBase::GetTypeId()));
    Config::SetDefault("ns3::MpTcpSocketBase::CongestionControl", StringValue(g_cc));
    Config::SetDefault("ns3::MpTcpBulkSendApplication::CongestionControl", StringValue(g_cc));
    Config::SetDefault("ns3::MpTcpSocketBase::MaxSubflows", UintegerValue(g_subflows)); // Sink
    Config::SetDefault("ns3::MpTcpBulkSendApplication::MaxSubflows", UintegerValue((uint8_t)g_subflows));//Source
    Config::SetDefault("ns3::MpTcpSocketBase::PathManagement", StringValue("NdiffPorts"));
    break;
  case TCP:
    g_subflows = 1; // For TCP, this should be one!
    g_shortFlowSubflows = 1;
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(MpTcpSocketBase::GetTypeId()));
    Config::SetDefault("ns3::MpTcpSocketBase::MaxSubflows", UintegerValue(g_subflows)); // For the sink
    Config::SetDefault("ns3::MpTcpBulkSendApplication::MaxSubflows", UintegerValue(g_subflows)); // TCP need one subflow only
    Config::SetDefault("ns3::MpTcpSocketBase::CongestionControl", StringValue("Uncoupled_TCPs"));
    Config::SetDefault("ns3::MpTcpBulkSendApplication::CongestionControl", StringValue("Uncoupled_TCPs"));
    Config::SetDefault("ns3::MpTcpSocketBase::PathManagement", StringValue("Default"));
    break;
  default:
    break;
    }

  g_seed = static_cast<uint32_t>(atoi(g_simInstance.c_str()));
//RngSeedManager::SetSeed(1); // default is 1
  RngSeedManager::SetRun(g_seed);
  cout << "Seed             : " << g_seed << endl;
  srand(g_seed);
  cout << "ns3:Seed(" << RngSeedManager::GetSeed () << ") ns3:Run(" << RngSeedManager::GetRun () << ")" << endl;

  GetCDFData (pTraffic);   // populate CDF table

  // SimResult
  SimResultHeaderGenerator();

  // Initialise animation parameter
  double xLoc = 0.0;
  double yLoc = 0.0;
  
  cout << endl;
  cout << "switchPorts      :  " << g_K << endl;
  cout << "totalCores       :  " << g_totalCore << endl;
  cout << "totalAggrs       :  " << g_totalAggr << endl;
  cout << "totalToRs        :  " << g_totalToR << endl;
  cout << "totalHosts       :  " << g_totalHost << endl;

  PrintSimParams();
  InternetStackHelper internet;
// ------------------ Topology Construction ------------------
  NodeContainer allHosts;

  // Host Layer Nodes
  for (uint32_t i = 0; i < g_numPod; i++)
    {
      for (uint32_t j = 0; j < g_numToR; j++)
        { // host[g_numPod][g_numToR]
          host[i][j].Create(g_numHost); // 20 hosts per each ToR switch
          internet.Install(host[i][j]);
          allHosts.Add(host[i][j]);     // Add all server to GlobalHostContainer
          Host_c.Add(host[i][j]);       // Add all server to Host_c for link utilisation
        }
    }

  InitHostArray(Host_c);

  // Access layer Nodes
  NodeContainer tor[g_numPod];          // NodeContainer for ToR switches
  for (uint32_t i = 0; i < g_numPod; i++)
    {
      tor[i].Create(g_numToR);
      internet.Install(tor[i]);
      Tor_c.Add(tor[i]);
    }
  // Aggregation layer Nodes
  NodeContainer aggr[g_numPod];         // NodeContainer for aggregation switches
  for (uint32_t i = 0; i < g_numPod; i++)
    {
      aggr[i].Create(g_numAggr);
      internet.Install(aggr[i]);
      Aggr_c.Add(aggr[i]);
    }
  // Core Layer Nodes
  NodeContainer core[g_numGroup];       // NodeContainer for core switches
  for (uint32_t i = 0; i < g_numGroup; i++)
    {
      core[i].Create(g_numCore);
      internet.Install(core[i]);
      core_c.Add(core[i]);
    }

// -----------------------------------------------------------
  AddNameToHosts();
//------------------------------------------------------------
// Allocate location to nodes
  double Dist = 200;

  // CORE SETUP
  double interval = Dist / ((g_numCore * g_numGroup) + 1);
  for (uint32_t g = 0; g < g_numGroup; g++)
    {
      for (uint32_t c = 0; c < g_numCore; c++)
        {
          Ptr<Node> node = core[g].Get(c);
          Ptr<ConstantPositionMobilityModel> loc = node->GetObject<ConstantPositionMobilityModel>();
          if (loc == 0)
            {
              loc = CreateObject<ConstantPositionMobilityModel>();
              node->AggregateObject(loc);
            }
          xLoc += interval;
          yLoc = 5;
          Vector locVec(xLoc, yLoc, 0);
          loc->SetPosition(locVec);
        }
    }

  // AGGR SETUP
  interval = (Dist / g_numPod) / (g_numAggr + 1);
  xLoc = interval;
  for (uint32_t p = 0; p < g_numPod; p++)
    {
      xLoc = p * (Dist / g_numPod);
      for (uint32_t a = 0; a < g_numAggr; a++)
        {
          Ptr<Node> node = aggr[p].Get(a);
          Ptr<ConstantPositionMobilityModel> loc = node->GetObject<ConstantPositionMobilityModel>();
          if (loc == 0)
            {
              loc = CreateObject<ConstantPositionMobilityModel>();
              node->AggregateObject(loc);
            }
          xLoc += interval;
          yLoc = 38;
          Vector locVec(xLoc, yLoc, 0);
          loc->SetPosition(locVec);
        }
    }

  // TOR SETUP
  interval = (Dist / g_numPod) / (g_numToR + 1);
  xLoc = interval;
  for (uint32_t p = 0; p < g_numPod; p++)
    {
      xLoc = p * (Dist / g_numPod);
      for (uint32_t t = 0; t < g_numToR; t++)
        {
          Ptr<Node> node = tor[p].Get(t);
          Ptr<ConstantPositionMobilityModel> loc = node->GetObject<ConstantPositionMobilityModel>();
          if (loc == 0)
            {
              loc = CreateObject<ConstantPositionMobilityModel>();
              node->AggregateObject(loc);
            }
          xLoc += interval;
          yLoc = 50;
          Vector locVec(xLoc, yLoc, 0);
          loc->SetPosition(locVec);
        }
    }

  // HOSTS SETUP
  interval = (Dist / g_numPod) / ((2 * g_numToR) + 1);
  yLoc = 60;
  for (uint32_t p = 0; p < g_numPod; p++)
    {
      xLoc = p * (Dist / g_numPod);
      yLoc = 60;
      for (uint32_t t = 0; t < g_numToR; t++)
        {
          xLoc += interval;
          yLoc = 60;
          double xTmpToR = xLoc;
          for (uint32_t h = 0; h < g_numHost; h++)
            {
              Ptr<Node> node = host[p][t].Get(h);
              Ptr<ConstantPositionMobilityModel> loc = node->GetObject<ConstantPositionMobilityModel>();
              if (loc == 0)
                {
                  loc = CreateObject<ConstantPositionMobilityModel>();
                  node->AggregateObject(loc);
                }
              if (h % 2 == 0)
                {
                  xLoc = xTmpToR;
                  yLoc += 4;
                }
              else
                xLoc += interval;
              Vector locVec(xLoc, yLoc, 0);
              loc->SetPosition(locVec);
            }
        }
    }

  PointToPointHelper p2p;
  p2p.SetQueue("ns3::DropTailQueue");
  p2p.SetDeviceAttribute("DataRate", StringValue(g_linkCapacity));
  p2p.SetChannelAttribute("Delay", StringValue(g_linkDelay));

  if (g_enableRED)
    {
      p2p.SetQueue ("ns3::RedQueue", "LinkBandwidth",
                    StringValue (g_linkCapacity), "LinkDelay",
                    StringValue (g_linkDelay));
      p2p.SetDeviceAttribute ("DataRate", StringValue (g_linkCapacity));
      p2p.SetChannelAttribute ("Delay", StringValue (g_linkDelay));
  }

  // Initialise address helper
  Ipv4AddressHelper ipv4Address;

//=========== Connect hosts to ToRs ===========//
  NetDeviceContainer hostToTorNetDevice[g_numPod][g_numToR][g_numHost];
//Ipv4InterfaceContainer ipContainer[g_numPod][g_numToR][g_numHost];
  stringstream ss;
  for (uint32_t p = 0; p < g_numPod; p++)
    {
      for (uint32_t t = 0; t < g_numToR; t++)
        {
          ss.str ("");
          ss << "10." << p << "." << t << "." << "0";
          string tmp = ss.str ();
          const char* address = tmp.c_str ();
          ipv4Address.SetBase (address, "255.255.255.252");
          for (uint32_t h = 0; h < g_numHost; h++)
            {
              hostToTorNetDevice[p][t][h] = p2p.Install (NodeContainer (host[p][t].Get (h), tor[p].Get (t)));
              ipv4Address.Assign (hostToTorNetDevice[p][t][h]);
              ipv4Address.NewNetwork ();
            }
        }
    }NS_LOG_INFO("Finished connecting tor switches and hosts");
//=========== Connect aggregate switches to edge switches ===========//
  if (g_enableRED)
    {
      p2p.SetQueue ("ns3::RedQueue", "LinkBandwidth",
                    StringValue (g_linkCapacity), "LinkDelay",
                    StringValue (g_linkDelay));
      p2p.SetDeviceAttribute ("DataRate", StringValue (g_linkCapacity));
      p2p.SetChannelAttribute ("Delay", StringValue (g_linkDelay));
    }
  NetDeviceContainer ae[g_numPod][g_numAggr][g_numToR];
//Ipv4InterfaceContainer ipAeContainer[g_numPod][g_numAggr][g_numToR];
  ipv4Address.SetBase("20.20.0.0", "255.255.255.0");
  for (uint32_t p = 0; p < g_numPod; p++)
    {
      for (uint32_t a = 0; a < g_numAggr; a++) // number of aggr switch per pod
        {
          for (uint32_t t = 0; t < g_numToR; t++)
            {
              ae[p][a][t] = p2p.Install(aggr[p].Get(a), tor[p].Get(t));

              ipv4Address.Assign(ae[p][a][t]);
              ipv4Address.NewNetwork();
            }
        }
    }NS_LOG_INFO("Finished connecting tor switches and aggregation");

//=========== Connect core switches to aggregate switches ===========//
  if (g_enableRED)
    {
      p2p.SetQueue ("ns3::RedQueue", "LinkBandwidth",
                    StringValue (g_linkCapacity), "LinkDelay",
                    StringValue (g_linkDelay));
      p2p.SetDeviceAttribute ("DataRate", StringValue (g_linkCapacity));
      p2p.SetChannelAttribute ("Delay", StringValue (g_linkDelay));
    }
  NetDeviceContainer ca[g_numGroup][g_numCore][g_numPod];
  ipv4Address.SetBase("30.30.0.0", "255.255.255.0");
  for (uint32_t g = 0; g < g_numGroup; g++)
    {
      for (uint32_t c = 0; c < g_numCore; c++)
        {
          for (uint32_t p = 0; p < g_numPod; p++)
            {
              ca[g][c][p] = p2p.Install(core[g].Get(c), aggr[p].Get(g));
              ipv4Address.Assign(ca[g][c][p]);
              ipv4Address.NewNetwork();
            }
        }
    }NS_LOG_INFO("Finished connecting core and aggregation");

// Populate Global Routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  if (g_trafficMatrix < INCAST_DIST)
    SetupTraces (Core_Aggr_Tor_Host);
  else if (g_trafficMatrix == INCAST_DIST)
    SetupTracesNew (Core_Aggr_Tor_Host);
  else
    {
      cout << "TrafficMatrix is " << GetKeyFromValueTM (g_trafficMatrix) << " NO TRACING" << endl;
    }

  if (g_enableDCTCP && !g_enabledHostMarking)
    SetupHostQueues();

  if (g_simpleQueueTrace && (g_trafficMatrix == INCAST_DIST))
    SimpleQueueTraces(Tor);
  else if (g_simpleQueueTrace)
    SimpleQueueTraces(Core_Aggr_Tor_Host);

//=========== Initialise settings for On/Off Application ===========//
  // sink application - It would closed doubled the time of source closure!
  cout << "\nSink App Install on following nodes: " << endl;
  for (uint32_t i = 0; i < allHosts.GetN(); i++)
    {
      MpTcpPacketSinkHelper sink("ns3::TcpSocketFactory",
          InetSocketAddress(Ipv4Address::GetAny(), (allHosts.Get(i)->GetId() + 1)));
      ApplicationContainer tmp = sink.Install(allHosts.Get(i));
      tmp.Start(Seconds(0.0));
    //tmp.Stop(Seconds(g_simTime + 20)); // This works for pFabric, INCAST_DIST and other TMs
      sinkApps.push_back(tmp);
      cout << allHosts.Get(i)->GetId() << " ";
    }
  cout << "\n" <<endl;

  ConnectionMatrix* conns = new ConnectionMatrix((int) g_totalHost);

  switch (g_trafficMatrix)
    {
  case PERMUTATION:
    conns->setPermutation();
    break;
  case STRIDE:
    conns->setStride(g_numToR * g_numHost);
    break;
  case RANDOM:
    conns->setRandomConnection(g_totalHost);
    break;
  case INCAST_STRIDE:
    conns->setStride (g_numToR * g_numHost);
    break;
  case SHORT_FLOW:
    if (g_shortFlowTM == PERMUTATION)
      conns->setPermutation();
    else if (g_shortFlowTM == STRIDE)
      conns->setStride(g_numToR * g_numHost);
    else if (g_shortFlowTM == RANDOM)
      conns->setRandomConnection(g_totalHost);
    break;
  default:
    break;
    }

  // Large flows SOURCE applications - They would be closed by g_simTime
  vector<connection*>* CM = conns->getAllConnections();
  uint32_t totalLargeConnx;
  vector<int> tmpVec;
  int pos;
  vector<connection*>* shortCM;// = GetShortCM(CM);
  //
  if (g_trafficMatrix == P_FABRIC)
    {
      IssuePFabric(allHosts);
    }
  else if (g_trafficMatrix == P_PERMUTATION)
    {
      IssuePermutation (allHosts);
    }
  else if (g_trafficMatrix == INCAST_DIST)
    {
      for (uint32_t i = 0; i < readJobs; i++)
        {
          IssueDistributedRead ();
        }
      for (uint32_t i = 0; i < hostArray.size (); i++)
        {
          IssueNotInnerRackFlows (hostArray[i]);
        }
    }
  else if (g_trafficMatrix == RANDOM_DIST)
    {
      for (uint32_t i = 0; i < hostArray.size (); i++)
        {
          IssueLargeFlows (hostArray[i], 0, 1.0);
        }
    }
  else if (g_trafficMatrix == PERMUTATION_DIST)
    {
      PermutationTraffic ();
    }
  else
    {// opening big else
  //
  if (g_trafficMatrix == SHORT_FLOW)
    {
      totalLargeConnx = static_cast<uint32_t> ((CM->size () * g_connxLimit) / 100);
      cout << "\nCMSize: " << (int) CM->size () << " LargeFlowCM: " << totalLargeConnx << " => " << g_connxLimit << "% of total flows" << endl;
    }
  else // Other TM (PERMUTATION, STRIDE, RANDOM); all for large flows
    {
      totalLargeConnx = CM->size ();
      cout << "CMSize: " << (int) CM->size () << " LargeFlowCM: " << totalLargeConnx << " 100% of total flows" << endl;
    }
  //
  for (uint32_t i = 0; i < totalLargeConnx; i++)
    {
      do
        {
          pos = rand () % CM->size ();
        }
      while (find (tmpVec.begin (), tmpVec.end (), pos) != tmpVec.end ());
      tmpVec.push_back (pos);
      connection* connection = (*CM).at (pos);
      int src = connection->src;
      int dst = connection->dst;
      connection->large = true;
      //
      Ptr<Node> srcNode = allHosts.Get (src);
      Ptr<Ipv4> ipv4Client = srcNode->GetObject<Ipv4> ();
      Ipv4InterfaceAddress iaddrClient = ipv4Client->GetAddress (1, 0);
      Ipv4Address ipv4AddrClient = iaddrClient.GetLocal ();
      //
      Ptr<Node> randomServerNode = allHosts.Get (dst);
      uint32_t flowId = allHosts.Get (src)->GetNApplications ();
      Ptr<Ipv4> ipv4Server = randomServerNode->GetObject<Ipv4> ();
      Ipv4InterfaceAddress iaddrServer = ipv4Server->GetAddress (1, 0);
      Ipv4Address ipv4AddrServer = iaddrServer.GetLocal ();

      MpTcpBulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address (ipv4AddrServer), randomServerNode->GetId () + 1));
      source.SetAttribute ("MaxBytes", UintegerValue (g_flowSize));
      source.SetAttribute ("FlowId", UintegerValue (flowId));
      //source.SetAttribute("MaxSubflows", UintegerValue(8)); // TCP would not work if it is uncommented

      string socketModelTmp = GetSocketModel ();
      source.SetAttribute ("SocketModel", StringValue (socketModelTmp));

      int _maxsub = g_subflows;    // Just for debugging purpose
      int _dctcp = g_enableDCTCP;  // Just for debugging purpose

      // For applicable to long flows
      if (g_slowDownEcnLike)
        {
        }
      source.SetAttribute ("FlowType", StringValue ("Large"));
      string flowLayer = GetFlowLayer (ipv4AddrClient, ipv4AddrServer);
      source.SetAttribute ("FlowLayer", StringValue (flowLayer));
      source.SetAttribute ("OutputFileName", StringValue (SetupSimFileName ("RESULT")));
      source.SetAttribute ("OutputFileNameDctcp", StringValue (SetupSimFileName ("DCTCP")));
      cmAnalisys (LARGE, ipv4AddrClient, ipv4AddrServer);
      sourceLargeFlowApps.push_back (source.Install (allHosts.Get (src)));

      cout << "LargeFlowCM(" << i + 1 << ") " << allHosts.Get (src)->GetId () << " -> " << randomServerNode->GetId ()
           << " Subflows: " << _maxsub << " DCTCP: " << isActive (_dctcp) << " FLowLayer: "
           << flowLayer << " SocketModel[" << socketModelTmp << "]" << endl;
    }
  cout << "\nLarge Flow Source App Installs on following nodes: " << endl;
  for (uint32_t i = 0; i < sourceLargeFlowApps.size (); i++)
    {
      Ptr<MpTcpBulkSendApplication> mptcpBulk = DynamicCast<MpTcpBulkSendApplication> (sourceLargeFlowApps[i].Get (0));
      cout << mptcpBulk->GetNode ()->GetId () << " ";
      sourceLargeFlowApps[i].Start (Seconds (0.0));
      sourceLargeFlowApps[i].Stop (Seconds (g_simTime));
    }
  cout << "\n" << endl;

  if (g_trafficMatrix == INCAST_STRIDE)
    {
      for (uint32_t i = 0; i < readJobs; i++)
        {
          IssueDistributedRead ();
        }
    }
//  vector<connection*>* shortCM = GetShortCM(CM); // Move it up
  shortCM = GetShortCM(CM);
  if (g_trafficMatrix == SHORT_FLOW)
    {
      Simulator::Schedule (Seconds (g_shortFlowStartTime), &ShortFlowConfig, shortCM, allHosts);
    }
  } // closing big else

  SimTimeMonitor();

  PrintCMStat();

  NS_LOG_INFO ("Run Simulation.");

  if (g_trafficMatrix < INCAST_DIST)
    {
      Simulator::Stop (Seconds (g_simTime + 40));
    }

  Simulator::Run();

  if (g_trafficMatrix < INCAST_DIST)
    {
      cout << Simulator::Now().GetSeconds() << " -> Generate Out puts"<< endl;
      OutPutCMStat();
      OutPutCore();
      OutPutAggr();
      OutPutTor();
      OutPutHost();
    }
  SimOverallResultWritter();
  SimResultFooterGenerator(); // OveralResultWritter should be called first!

  Simulator::Destroy();
  cout << Simulator::Now().GetSeconds() << " END "<< endl;
  NS_LOG_INFO ("Done.");
  return 0;
}
