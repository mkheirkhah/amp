/*
 * Author: Morteza Kheirkhah <m.kheirkhah@ed.ac.uk>
 */

#include <stdint.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include "ns3/log.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/gnuplot.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/red-queue.h"
#include "ns3/netanim-module.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("incast");

NodeContainer Sender_c;
NodeContainer Receiver_c;
NodeContainer Core_c;
NodeContainer Total_c;

GnuplotCollection gnu;

vector<pair<double, double> > RedTxQueue;
bool g_enableRatePlotting = true;
bool g_enableLfPlotting = true;
bool g_enableSfPlotting = false;
bool g_enableRED = true;
bool g_enableDCTCP = true;
double g_REDmaxTh = 10;
double g_REDminTh = 10;
double g_REDWeight = 1.0;
bool g_REDGentle = false;
uint32_t g_segmentSize = 1400;
uint32_t g_REDMeanPktSize = g_segmentSize;
uint32_t g_REDQueueLimit = 100;         // Queue limit per packets
double g_DCTCPWeight = 1.0 / 16.0;      // DCTCP weight factor
string g_linkCapacity = "1000Mbps";     // 100Mbps
string g_linkDelay = "56us";            // RTT of empty path; 60us * 4 = 240us
uint32_t g_subflows = 1;                // 8 Subflows
string g_note = "";
bool g_enableAnim = false;
double g_samplingInterval = 0.0001;
uint32_t g_XmpGamma = 1;
uint32_t g_XmpBeta = 4;
uint32_t g_flowSize = 0;      // 0: unlimited, state otherwise!
string g_cc = "XCA";
string g_cc_tmp = "NONE";
uint32_t g_seed = 0;          // RNG seed
string g_simInstance = "0";   // Dynamically adjust at cmd
double g_rateBeat = 0.1;
bool g_ecn = false;
string g_simName = "";
string g_scenario = "INCAST";
double g_rtt = 4*Time(g_linkDelay).GetMicroSeconds();
bool g_slowDownXmpLike = false;
bool g_queueModeBytes = false;// If true, the queues perform per bytes rather than packets.
bool g_dctcpAlphaPerAck = false;
bool g_dctcpFastAlpha = false;
uint32_t g_DTQmarkTh = 1;
bool g_SDEL = false;
// Normal Scenario
string   g_flowType = "XMP";
uint32_t g_flowNumber = 4;
double   g_flowgap = 0.000224;
double   g_simTime = 1;
double   g_flowEndTimeGap = 0.0;
// Special Scenario
bool     g_specialSource = false;
string   g_specialFlowType = "ECN";
uint32_t g_specialFlowNumber = 1;
bool     g_specialSubflow = false;
// Cwnd/Rwnd
uint32_t g_cwndMin = 1;
uint32_t g_rwndScale = 100;
//DynamicSubflows
bool g_dynamicSubflow = false;
uint32_t g_incastThreshold = 10;
uint32_t g_incastExitThreshold = 8;
//sender
uint32_t g_senders = g_flowNumber;
uint32_t g_receivers = 1;
uint32_t g_core_switches = 1;
uint32_t servPort = 5000;
// IncastInterval
bool g_enabledIncastInterval = false;
double g_incastIntervalTime = 1; // seconds
uint64_t g_incastFlowCount = 0;
bool g_singleFileIncast = false;

/* Uncoupled_TCPs, Linked_Increases, RTT_Compensator, Fully_Coupled */
std::string
ConvertCC (string input)
{
  if (input.compare ("Fully_Coupled") == 0)
    return "FC";
  else if (input.compare ("Uncoupled_TCPs") == 0)
    return "UC";
  else if (input.compare ("RTT_Compensator") == 0)
    return "RC";
  else if (input.compare ("XMP") == 0)
    return "XMP";
  else if (input.compare ("Fast_Increases") == 0)
    return "FI";
  else if (input.compare ("Fast_Uncoupled") == 0)
    return "FU";
  else if (input.compare ("XCA") == 0)
    return "XCA";
  return "Unknown";
}

string SetupSimFileName(uint32_t i);

Ipv4Address
GetIpAddr(NodeContainer nc, uint32_t dst)
{
  Ptr<Node> randomServerNode = nc.Get (dst);
  Ptr<Ipv4> ipv4Server = randomServerNode->GetObject<Ipv4> ();
  Ipv4InterfaceAddress iaddrServer = ipv4Server->GetAddress (1, 0);
  return iaddrServer.GetLocal ();
}


string
SetupSource (MpTcpBulkSendHelper& source, uint32_t i)
{
  if (g_specialSubflow)
    {
      source.SetAttribute ("MaxSubflows", UintegerValue (1));
      cout << "Setup Normal  Source SF[" << 1 << "] -> ";
    }
  else
    {
      source.SetAttribute ("MaxSubflows", UintegerValue (g_subflows));
      cout << "Setup Normal  Source SF[" << g_subflows << "] -> ";
    }

  if (g_flowType == "XMP")
    {
      g_cc_tmp = "XMP";
      source.SetAttribute ("DCTCP", BooleanValue (false));
      source.SetAttribute ("CongestionControl", StringValue ("XMP"));
      source.SetAttribute ("SocketModel", StringValue ("XMP"));
    }
  else if (g_flowType == "DCMPTCP")
    {
      g_cc_tmp = "RTT_Compensator";
      source.SetAttribute ("DCTCP", BooleanValue (true));
      source.SetAttribute ("SocketModel", StringValue ("DCMPTCP"));
      source.SetAttribute ("SlowDownEcnLike", BooleanValue (false)); // DCMPTCP should be false!
      source.SetAttribute ("CongestionControl", StringValue ("RTT_Compensator"));
    }
  else if (g_flowType == "XFC")
    {
      g_cc_tmp = g_cc;
      source.SetAttribute ("DCTCP", BooleanValue (true));
      source.SetAttribute ("SocketModel", StringValue ("XFC"));
      source.SetAttribute ("SlowDownEcnLike", BooleanValue (true));
      source.SetAttribute ("CongestionControl", StringValue (g_cc));
    }
  else if (g_flowType == "DCTCP")
    {
      g_cc_tmp = "Uncoupled_TCPs";
      source.SetAttribute ("DCTCP", BooleanValue (true));
      source.SetAttribute ("SocketModel", StringValue ("DCTCP"));
      source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
    }
  else if (g_flowType == "ECN")
    {
      g_cc_tmp = "Uncoupled_TCPs";
      source.SetAttribute ("DCTCP", BooleanValue (true));
      source.SetAttribute ("SocketModel", StringValue ("ECN"));
      source.SetAttribute ("SlowDownEcnLike", BooleanValue (true));
      source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
    }
  else if (g_flowType == "TCP")
    {
      g_cc_tmp = "Uncoupled_TCPs";
      source.SetAttribute ("DCTCP", BooleanValue (false));
      source.SetAttribute ("SocketModel", StringValue ("TCP"));
      source.SetAttribute ("SlowDownEcnLike", BooleanValue (false));
      source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
    }
  else if (g_flowType == "MPTCP")
    {
      g_cc_tmp = g_cc;
      source.SetAttribute ("DCTCP", BooleanValue (false));
      source.SetAttribute ("SocketModel", StringValue ("MPTCP"));
      source.SetAttribute ("SlowDownEcnLike", BooleanValue (false));
      source.SetAttribute ("CongestionControl", StringValue (g_cc));
    }
  return g_flowType;
}

string
SetupSpecialSource (MpTcpBulkSendHelper& source, uint32_t i)
{
  if (g_specialSubflow)
    {
      source.SetAttribute ("MaxSubflows", UintegerValue (g_subflows));
      cout << "Setup Special Source SF[" << g_subflows << "] -> ";
    }
  else
    {
      source.SetAttribute ("MaxSubflows", UintegerValue (1));
      cout << "Setup Special Source SF[" << 1 << "] -> ";
    }
  if (g_specialFlowType == "XMP")
    {
      g_cc_tmp = "XMP";
      source.SetAttribute ("DCTCP", BooleanValue (false));
      source.SetAttribute ("CongestionControl", StringValue ("XMP"));
      source.SetAttribute ("SocketModel", StringValue ("XMP"));
    }
  else if (g_specialFlowType == "XFC")
    {
      g_cc_tmp = g_cc;
      source.SetAttribute ("DCTCP", BooleanValue (true));
      source.SetAttribute ("SocketModel", StringValue ("XFC"));
      source.SetAttribute ("SlowDownEcnLike", BooleanValue (true));
      source.SetAttribute ("CongestionControl", StringValue (g_cc));
    }
  else if (g_specialFlowType == "DCMPTCP")
    {
      g_cc_tmp = "RTT_Compensator";
      source.SetAttribute ("DCTCP", BooleanValue (true));
      source.SetAttribute ("SocketModel", StringValue ("DCMPTCP"));
      source.SetAttribute ("SlowDownEcnLike", BooleanValue (false)); // DCMPTCP should be false!
      source.SetAttribute ("CongestionControl", StringValue ("RTT_Compensator"));
    }
  else if (g_specialFlowType == "DCTCP")
    {
      g_cc_tmp = "Uncoupled_TCPs";
      source.SetAttribute ("DCTCP", BooleanValue (true));
      source.SetAttribute ("SocketModel", StringValue ("DCTCP"));
      source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
    }
  else if (g_specialFlowType == "ECN")
    {
      g_cc_tmp = "Uncoupled_TCPs";
      source.SetAttribute ("DCTCP", BooleanValue (true));
      source.SetAttribute ("SocketModel", StringValue ("ECN"));
      source.SetAttribute ("SlowDownEcnLike", BooleanValue (true));
      source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
    }
  else if (g_specialFlowType == "TCP")
    {
      g_cc_tmp = "Uncoupled_TCPs";
      source.SetAttribute ("DCTCP", BooleanValue (false));
      source.SetAttribute ("SocketModel", StringValue ("TCP"));
      source.SetAttribute ("SlowDownEcnLike", BooleanValue (false));
      source.SetAttribute ("CongestionControl", StringValue ("Uncoupled_TCPs"));
    }
  return g_specialFlowType;
}

uint32_t
GetYrange()
{
  if (g_queueModeBytes)
      return (1400 * g_REDQueueLimit);
  else
    return g_REDQueueLimit;
}

uint32_t
GetXrange()
{
  return g_simTime;
}

string
GetQueueMode()
{
  if (g_queueModeBytes)
    return "QMB";
  else
    return "QMP";
}

//"MPTCP_INCAST_240_SF8_1";
string
SetupSimFileName(uint32_t i)
{
  ostringstream oss;
  oss.str ("");
//oss << g_simName << "_" << g_scenario << "_"<< ConvertCC (g_cc) << "_" << "B" << g_XmpBeta << "_" << "K" << g_REDmaxTh << "_" << g_rtt << "_" << "SF" << g_subflows <<"_" << i << ".data";
  if (g_specialSource)
    oss << g_scenario << "_"  << "CM"<< g_cwndMin << "_" << g_specialFlowType << "_" << g_flowType << "_DS" << g_dynamicSubflow << "_IT" << g_incastThreshold << "_IET" << g_incastExitThreshold << "_FN" <<  g_flowNumber << "_SFN"<< g_specialFlowNumber << "_" << "FG" << g_flowgap*1000000 << g_simName  << "_" << "B" << g_XmpBeta << "_" << GetQueueMode() << "_" <<  "K" << g_REDmaxTh << "_" << g_rtt << "_ST" << g_simTime << "_" << "SF" << g_subflows <<"_" << i << ".data";
  else
    oss << g_scenario << "_"  << "CM"<< g_cwndMin << "_" << g_flowType << "_DS" << g_dynamicSubflow << "_IT" << g_incastThreshold << "_IET"<< g_incastExitThreshold << "_FN" <<  g_flowNumber <<"_SFN"<< g_specialFlowNumber << "_" << "FG" <<  g_flowgap*1000000 << g_simName  << "_" << "B" << g_XmpBeta << "_" << GetQueueMode() << "_" <<  "K" << g_REDmaxTh << "_" << g_rtt << "_ST" << g_simTime << "_" << "SF" << g_subflows <<"_" << i << ".data";
  string tmp = oss.str();
  oss.str("");
  return tmp;
}

void
SimTimeMonitor ()
{
  NS_LOG_UNCOND("ClockTime: " << Simulator::Now().GetSeconds());
  double now = Simulator::Now ().GetSeconds ();
  cout << "[" << SetupSimFileName(0) << "] -> SimClock: " << now << endl;
  if (now < g_simTime)
    Simulator::Schedule (Seconds (1), &SimTimeMonitor);
}

string
SetupQueueFileName()
{
  ostringstream oss;
  oss.str ("");
//oss << g_simName << "_" << g_scenario << "_"<< ConvertCC (g_cc) << "_" << "B" << g_XmpBeta << "_" << "K" << g_REDmaxTh << "_" << g_rtt << "_" << "SF" << g_subflows;
  if (g_specialSource)
    oss << g_scenario << "_" << "CM" << g_cwndMin << "_" << g_specialFlowType << "_" << g_flowType << "_DS" << g_dynamicSubflow << "_IT" << g_incastThreshold << "_IET" << g_incastExitThreshold << "_FN" << g_flowNumber << "_SFN"<< g_specialFlowNumber << "_" << "FG" <<  g_flowgap*1000000 << g_simName << "_" << "B" << g_XmpBeta << "_" << GetQueueMode() << "_" << "K" << g_REDmaxTh << "_" << g_rtt << "_ST" << g_simTime << "_" << "SF" << g_subflows;
  else
    oss << g_scenario << "_" << "CM" << g_cwndMin << "_" << g_flowType << "_DS" << g_dynamicSubflow << "_IT" << g_incastThreshold << "_IET" << g_incastExitThreshold << "_FN" << g_flowNumber << "_SFN"<< g_specialFlowNumber << "_" << "FG" <<  g_flowgap*1000000 << g_simName << "_" << "B" << g_XmpBeta << "_" << GetQueueMode() << "_" << "K" << g_REDmaxTh << "_" << g_rtt << "_ST" << g_simTime << "_" << "SF" << g_subflows;
  string tmp = oss.str();
  oss.str("");
  return tmp;
}

string
IsActive (uint32_t boolean)
{
  if (boolean == 0)
    return "Inactive";
  else
    return "Active";
}

Ptr<Queue>
FindQueue (Ptr<NetDevice> dev)
{
  PointerValue ptr;
  dev->GetAttribute ("TxQueue", ptr);
  return ptr.Get<Queue> ();
}

std::string
GeneratePlotDetail (void)
{
  stringstream oss;
  oss << "LR [" << g_linkCapacity << "] LD [" << g_linkDelay << "] QL [" << g_REDQueueLimit << "pkts] AvgPktSize ["
      << g_REDMeanPktSize << "bytes] MinTh [" << g_REDminTh << "] MaxTh [" << g_REDmaxTh << "]\\n RedW [" << g_REDWeight
      << "] Gentle [" << IsActive (g_REDGentle) << "] DctcpW [" << g_DCTCPWeight << "] SimDur [" << g_simTime << "] simName["
      << g_simName << "] SamInterval[" << g_samplingInterval << "]";
  string tmp = oss.str ();
  oss.str ("");
  return tmp;
}

void
QueueMonitor ()
{
  uint32_t T = (uint32_t) Simulator::Now ().GetSeconds ();
  Ptr<Queue> txQueue = FindQueue (Core_c.Get (0)->GetDevice (Sender_c.GetN()+1));
  if (g_queueModeBytes)
    RedTxQueue.push_back (make_pair (Simulator::Now ().GetSeconds (), txQueue->GetNBytes ()));
  else
    RedTxQueue.push_back (make_pair (Simulator::Now ().GetSeconds (), txQueue->GetNPackets ()));

  txQueue->ResetStatistics ();

  if (T < g_simTime)
    Simulator::Schedule (Seconds (g_samplingInterval), &QueueMonitor);
}

bool
sortbysec (const pair<double, double> &a, const pair<double, double> &b)
{
  return (a.second < b.second);
}
void
GenerateQueueCDFPlot ()
{
  string file = SetupQueueFileName() + "_QUEUE_CDF.data";
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (file, std::ios::out);
  ostream* os = stream->GetStream ();

  std::sort(RedTxQueue.begin (), RedTxQueue.end(), sortbysec);
  for (uint32_t i = 0; i < RedTxQueue.size(); i++)
    *os << (i + 1) / (double)RedTxQueue.size() << "\t" << RedTxQueue[i].second << endl;
}

//<< "set output \"fairnessOutput.eps\"\n"
void
GenerateQueuePlot ()
{
  Gnuplot queueTracerGraph;
  ostringstream oss;
  oss << "set terminal postscript eps noenhanced color solid font 'Times-Bold,30'\n"
      << "set output\""
      << SetupQueueFileName() << "_QUEUE.eps" << "\"\n"
      << "set xlabel \"Time (s)\"           offset 0,0.8,0\n"
      << "set ylabel \"Queue Size (pkts)\"  offset 3,0.0,0\n"
      << "set lmargin 5.0\n"
      << "set rmargin 1.5\n"
      << "set tmargin 1.0\n"
      << "set bmargin 2.5\n"
      << "set yrange [:" << GetYrange() << "]\n"
      << "set xrange [:" << GetXrange() << "]\n"
      << "set xtics offset 0,0.3,0 nomirror\n"
      << "set ytics offset 0.3,0,0 nomirror\n"
      << "set mytics 2\n"
      << "set mxtics 2\n"
      << "set title font ',20' offset 0,-0.6,0 noenhanced\n"
      << "unset grid\n"
      << "unset key\n";
  queueTracerGraph.AppendExtra (oss.str()
  /*"set key bmargin center horizontal Left reverse noenhanced autotitles columnhead nobox\n"*/);
  oss.str("");
  oss << SetupQueueFileName();
  queueTracerGraph.SetTitle (oss.str());
//queueTracerGraph.SetTitle ("RedQueue \\n\\n" + GeneratePlotDetail ());

  Gnuplot2dDataset dataSet;
  dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);
//  std::stringstream title;
//  title << "QueueSize ";
//  dataSet.SetTitle (title.str ());
  vector<pair<double, double> >::iterator it = RedTxQueue.begin ();
  while (it != RedTxQueue.end ())
    {
      dataSet.Add (it->first, it->second);
      it++;
    }
  if (RedTxQueue.size () > 0)
    queueTracerGraph.AddDataset (dataSet);
  gnu.AddPlot (queueTracerGraph);

  string file = SetupQueueFileName() + "_QUEUE.data";
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (file, std::ios::out);
  ostream* os = stream->GetStream ();
  gnu.GenerateOutput (*os);
}

void
PrintParams ()
{
  cout << "-------------------------" << endl;
  cout << "FlowType[" << g_flowType << "] FlowNumber[" << g_flowNumber <<"] FlowGap[" <<g_flowgap << "] SF[" << g_subflows << "] QMB[" << IsActive (g_queueModeBytes) << "]" << endl;
  cout << "Red[" << IsActive (g_enableRED) << "] " << "K[" << g_REDmaxTh << "] QueueSampling[" << g_samplingInterval<< "] SimEnd[" << g_simTime << "]" << endl;
  cout << "SpecialSource[" << IsActive(g_specialSource) << "] SpecialFlowNum[" << g_specialFlowNumber << "] SpecialFlowType[" << g_specialFlowType <<"] SpecialSubflow[" << IsActive(g_specialSubflow) << "]"<< endl;
  cout << "CwndMin[" << g_cwndMin << "] RwndScale[" << g_rwndScale << "] Default_CC[" << g_cc << "]"<< endl;
  cout << "DynamicSubflow[" << IsActive(g_dynamicSubflow) << "] IncastThresh[" << g_incastThreshold << "] IncastExitThresh[" << g_incastExitThreshold << "]" << endl;
  cout << "FlowEndTimeGap[" << g_flowEndTimeGap << "]" << endl;
  cout << "IncastInterval[" << g_enabledIncastInterval <<"] Interval[" << g_incastIntervalTime << "]" << endl;
  cout << "-------------------------" << endl;
}

void
FlowSchedulerNormal()
{
  cout << "FlowSchedulerNormal" << endl;
  for (uint32_t i = 0; i < g_flowNumber; i++)
    {
      string tmpFlowType = "";
      MpTcpBulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (GetIpAddr(Receiver_c, 0), servPort));
      source.SetAttribute ("FlowId", UintegerValue (i + 1));
      source.SetAttribute ("MaxBytes", UintegerValue (g_flowSize));

      if (g_specialSource && i < g_specialFlowNumber)
        tmpFlowType = SetupSpecialSource (source , i);
      else
        tmpFlowType = SetupSource (source, i);

      source.SetAttribute ("OutputFileName", StringValue (SetupSimFileName (i + 1)));
      ApplicationContainer sourceApps = source.Install (Sender_c.Get (i));
      sourceApps.Start (Seconds (i * g_flowgap));
      sourceApps.Stop (Seconds (g_simTime));
      if (g_specialSource && i >= g_specialFlowNumber)
        { // Adjust the flow end time for the non-special flows (i.e. DCTCP)
          sourceApps.Start (Seconds (i * g_flowgap));
          sourceApps.Stop (Seconds (g_simTime - g_flowEndTimeGap));
        }
      cout << "Flow " << i << " [" << tmpFlowType << "] Size[" << g_flowSize << "] CC[" << g_cc_tmp << "] Installed on server " << i << " [" << GetIpAddr (Sender_c, i) << " -> "
          << GetIpAddr (Receiver_c, 0) << "] start at " << i * g_flowgap << endl;
    }
}

void
FlowSchedulerIncastInterval()
{
  cout << "FlowSchedulerIncastInterval" << endl;
  for (uint32_t i = 0; i < g_flowNumber; i++)
    {
      string tmpFlowType = "";
      MpTcpBulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (GetIpAddr (Receiver_c, 0), servPort));

      source.SetAttribute ("MaxBytes", UintegerValue (g_flowSize));
      source.SetAttribute ("FlowType", StringValue ("Large"));
      if (g_singleFileIncast)
        { // Results are stored in a single file
          source.SetAttribute ("FlowId", UintegerValue (1));
          source.SetAttribute ("OutputFileName", StringValue (SetupSimFileName (1)));
        }
      else
        {
          g_incastFlowCount++; // Increase flow counter for each new flow
          source.SetAttribute ("FlowId", UintegerValue (g_incastFlowCount));
          source.SetAttribute ("OutputFileName", StringValue (SetupSimFileName (g_incastFlowCount)));
        }

      if (g_specialSource && i < g_specialFlowNumber)
        tmpFlowType = SetupSpecialSource (source, i);
      else
        tmpFlowType = SetupSource (source, i);

      ApplicationContainer sourceApps = source.Install (Sender_c.Get (i));
      sourceApps.Start (Seconds (i * g_flowgap));
      sourceApps.Stop (Seconds (g_simTime));
      if (g_specialSource && i >= g_specialFlowNumber)
        { // Adjust the flow end time for the non-special flows (i.e. DCTCP)
          sourceApps.Start (Seconds (i * g_flowgap));
          sourceApps.Stop (Seconds (g_simTime - g_flowEndTimeGap));
        }
      cout << "Flow " << i << " [" << tmpFlowType << "] Size[" << g_flowSize << "] CC[" << g_cc_tmp << "] Installed " << i << " ["
          << GetIpAddr (Sender_c, i) << " -> " << GetIpAddr (Receiver_c, 0) << "] start at "
          << Simulator::Now ().GetSeconds () + (i * g_flowgap) << endl;
    }
  if (g_enabledIncastInterval && (Simulator::Now ().GetSeconds () <= g_simTime - g_incastIntervalTime))
    {
      cout << "IncastIntervalTime: " << Simulator::Now ().GetSeconds () + g_incastIntervalTime << " seconds" << endl;
      Simulator::Schedule (Seconds (g_incastIntervalTime), &FlowSchedulerIncastInterval);
    }
}

bool
SetSimName (std::string input)
{
  cout << "SimName          : " << g_simName << " -> " << input << endl;
  g_simName = input;
  return true;
}

bool
SetSimInstance (std::string input)
{
  cout << "SimInstance      : " << g_simInstance << " -> " << input << endl;
  g_simInstance = input;
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
SetLfPlot (std::string input)
{
  cout << "LargeFlowPlotting: " << g_enableLfPlotting << " -> " << input << endl;
  g_enableLfPlotting = atoi (input.c_str ());
  return true;
}

bool
SetSfPlot (std::string input)
{
  cout << "ShortFlowPlotting: " << g_enableSfPlotting << " -> " << input << endl;
  g_enableSfPlotting = atoi (input.c_str ());
  return true;
}
bool
SetRED (std::string input)
{
  cout << "Enable RED Queue : " << g_enableRED << " -> " << input << endl;
  g_enableRED = atoi (input.c_str ());
  return true;
}

bool
SetDCTCP (std::string input)
{
  cout << "Enable DCTCP     : " << g_enableDCTCP << " -> " << input << endl;
  g_enableDCTCP = atoi (input.c_str ());
  return true;
}

bool
SetREDmin (std::string input)
{
  cout << "RED Min Threshold: " << g_REDminTh << " -> " << input << endl;
  g_REDminTh = atof (input.c_str ());
  return true;
}

bool
SetREDmax (std::string input)
{
  cout << "RED Max Threshold: " << g_REDmaxTh << " -> " << input << endl;
  g_REDmaxTh = atof (input.c_str ());
  return true;
}

bool
SetDCTCPweight (std::string input)
{
  cout << "DCTCP Weight     : " << g_DCTCPWeight << " -> " << input << endl;
  g_DCTCPWeight = atof (input.c_str ());
  return true;
}

bool
SetREDweight (std::string input)
{
  cout << "RED Max Weight   : " << g_REDWeight << " -> " << input << endl;
  g_REDWeight = atof (input.c_str ());
  return true;
}

bool
SetREDQueueLimit (std::string input)
{
  cout << "RED Queue Limit  : " << g_REDQueueLimit << " -> " << input << endl;
  g_REDQueueLimit = atof (input.c_str ());
  return true;
}
bool
SetSimTime (std::string input)
{
  cout << "SimDuration      : " << g_simTime << " -> " << input << endl;
  g_simTime = atof (input.c_str ());
  return true;
}
bool
SetNetAnim (std::string input)
{
  cout << "NetAnim          : " << g_enableAnim << " -> " << input << endl;
  g_enableAnim = atoi (input.c_str ());
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
SetCongestionControl (std::string input)
{
  cout << "CongestionControl: " << g_cc << " -> " << input << endl;
  g_cc = input;
  if (g_cc == "XMP")
    {
      g_enableDCTCP = false;
      g_SDEL = false;
    }
  return true;
}

bool
SetNumSubflow (std::string input)
{
  cout << "Subflows         : " << g_subflows << " -> " << input << endl;
  g_subflows = atoi (input.c_str ());
  return true;
}

bool
SetLinkDelay (std::string input)
{
  cout << "LinkDelay        : " << g_linkDelay << " -> " << input+"us" << endl;
  g_linkDelay = input+"us";
  g_rtt = 4*Time(g_linkDelay).GetMicroSeconds();
  return true;
}
bool
SetECN (std::string input)
{
  cout << "ECN              : " << g_ecn << " -> " << input << endl;
  g_ecn = atoi (input.c_str ());
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
SetQueueMode (std::string input)
{
  cout << "QueueModeBytes   : " << g_queueModeBytes << " -> " << input << endl;
  g_queueModeBytes = atoi (input.c_str ());
  return true;
}

bool
SetQueueSamplingInterval (std::string input)
{
  cout << "QueueSamplingInte: " << g_samplingInterval << " -> " << input << endl;
  g_samplingInterval = atof (input.c_str ());
  return true;
}

bool
SetDctcpAlphaPerAck (std::string input)
{
  cout << "DctcpAlphaPerAck : " << g_dctcpAlphaPerAck << " -> " << input << endl;
  g_dctcpAlphaPerAck = atoi (input.c_str ());
  return true;
}
bool
SetDctcpFastAlpha (std::string input)
{
  cout << "DctcpFastAlpha   : " << g_dctcpFastAlpha << " -> " << input << endl;
  g_dctcpFastAlpha = atoi (input.c_str ());
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
SetFlowGap(std::string input)
{
  cout << "FlowGap          : " << g_flowgap << " -> " << input << endl;
  g_flowgap = atof (input.c_str ());
  return true;
}

bool
SetSDEL(std::string input)
{
  cout << "SDXL::ECN        : " << g_SDEL << " -> " << input << endl;
  g_SDEL = atoi (input.c_str ());
  return true;
}

bool
SetFlowType (std::string input)
{
  cout << "FlowType         : " << g_flowType << " -> " << input << endl;
  g_flowType = input.c_str ();
  return true;
}

bool
SetFlowNumber (std::string input)
{
  cout << "FlowNumber       : " << g_flowNumber << " -> " << input << endl;
  g_flowNumber = atoi (input.c_str ());
  g_senders = g_flowNumber;
  cout << "Update # senders : " << g_senders   << endl;
  return true;
}

bool
SetSpecialFlowNumber (std::string input)
{
  cout << "SpecialFlowNumber: " << g_specialFlowNumber << " -> " << input << endl;
  g_specialFlowNumber = atoi (input.c_str ());
  return true;
}

bool
SetSpecialFlowType (std::string input)
{
  cout << "SpecialFlowType  : " << g_specialFlowType << " -> " << input << endl;
  g_specialFlowType = input.c_str ();
  return true;
}

bool
SetSpecialSource (std::string input)
{
  cout << "SpecialSource    : " << g_specialSource << " -> " << input << endl;
  g_specialSource = atoi (input.c_str ());
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
SetSpecialSubflow (std::string input)
{
  cout << "SpecialSubflow   : " << g_specialSubflow << " -> " << input << endl;
  g_specialSubflow = atoi (input.c_str ());
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
SetIncastExitThreshold (std::string input)
{
  cout << "IncastExitThresh : " << g_incastExitThreshold << " -> " << input << endl;
  g_incastExitThreshold = atoi (input.c_str ());
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
SetSenders (std::string input)
{
  cout << "No. Senders      : " << g_senders << " -> " << input << endl;
  g_senders = atoi (input.c_str ());
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
SetFlowEndTimeGap (std::string input)
{
  cout << "FlowEndTimeGap    : " << g_flowEndTimeGap << " -> " << input << endl;
  g_flowEndTimeGap = atof (input.c_str ());
  return true;
}
bool
SetFlowSize(std::string input)
{
  cout << "FlowSize         : " << g_flowSize << " -> " << input << endl;
  g_flowSize = atoi(input.c_str()) * 1024; // convert from b to Kb
  return true;
}
bool
SetIncastInterval(std::string input)
{
  cout << "EnableIncastInter: " << g_enabledIncastInterval << " -> " << input << endl;
  g_enabledIncastInterval = atoi(input.c_str());
  return true;
}
bool
SetIncastIntervalTime(std::string input)
{
  cout << "IncastInterTime  : " << g_incastIntervalTime << " -> " << input << endl;
  g_incastIntervalTime = atof(input.c_str());
  return true;
}
bool
SetSingleFileIncast(std::string input)
{
  cout << "SingleFileIncast : " << g_singleFileIncast << " -> " << input << endl;
  g_singleFileIncast = atoi(input.c_str());
  return true;
}


int
main (int argc, char *argv[])
{
  // Add some logging
  LogComponentEnable ("incast", LOG_ALL);

  // First record of RedTxQueue container, should be 0 because Gnuplot is mad!!
  RedTxQueue.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));

  CommandLine cmd;
  cmd.AddValue("sfi", "SingleFileIncast", MakeCallback(SetSingleFileIncast));
  cmd.AddValue("iit", "IncastIntervalTime", MakeCallback(SetIncastIntervalTime));
  cmd.AddValue("ii", "IncastInterval", MakeCallback(SetIncastInterval));
  cmd.AddValue("fs", "Flow Size", MakeCallback(SetFlowSize));
  cmd.AddValue("fetg", "Flow end time gap", MakeCallback(SetFlowEndTimeGap));
  cmd.AddValue("lr", "Set p2p link rate", MakeCallback(SetLinkRate));
  cmd.AddValue ("senders", "No. of senders", MakeCallback (SetSenders));
  cmd.AddValue ("iet", "Incast Threshold ",MakeCallback(SetIncastExitThreshold));
  cmd.AddValue ("it", "Incast Threshold ", MakeCallback(SetIncastThreshold));
  cmd.AddValue ("ds", "Dynamic Subflow ", MakeCallback(SetDynamicSubflow));
  cmd.AddValue ("cwndmin", "Flows", MakeCallback (SetCwndMin));
  cmd.AddValue ("rwndscale", "Flows", MakeCallback (SetRcwndScale));
  cmd.AddValue ("ssf", "Special subflow", MakeCallback (SetSpecialSubflow));
  cmd.AddValue ("ss",  "Special Source Active", MakeCallback (SetSpecialSource));
  cmd.AddValue ("sft", "Special FlowType", MakeCallback (SetSpecialFlowType));
  cmd.AddValue ("sfn", "Special FlowNumber", MakeCallback (SetSpecialFlowNumber));
  cmd.AddValue ("fn", "Flows", MakeCallback(SetFlowNumber));
  cmd.AddValue ("ft", "FlowType", MakeCallback(SetFlowType));
  cmd.AddValue ("sdel", "Set SlowDownEcnLike -- ECN", MakeCallback(SetSDEL));
  cmd.AddValue ("fg", "Flow Gap", MakeCallback(SetFlowGap));
  cmd.AddValue ("dtqmt", "DropTailQueue Marking Threshold", MakeCallback(SetDTQMarkTh));
  cmd.AddValue ("dfa", " DCTCP Non-Smoothed Alpha", MakeCallback (SetDctcpFastAlpha));
  cmd.AddValue ("dapa", "DCTCP ALPHA PER ACK", MakeCallback (SetDctcpAlphaPerAck));
  cmd.AddValue ("qsi", "queue sampling interval", MakeCallback (SetQueueSamplingInterval));
  cmd.AddValue ("qmb", "QUEUE_MODE_BYTES", MakeCallback (SetQueueMode));
  cmd.AddValue ("sdxl", " slow down xmp like", MakeCallback (SetSDXL));
  cmd.AddValue ("ecn", " enable ECN", MakeCallback (SetECN));
  cmd.AddValue ("ld", " Set Link Delay", MakeCallback (SetLinkDelay));
  cmd.AddValue ("sim", "Set sim name", MakeCallback (SetSimName));
  cmd.AddValue ("sf", "Number of MPTCP SubFlows", MakeCallback (SetNumSubflow));
  cmd.AddValue ("i", "Set simulation instance number as a string", MakeCallback (SetSimInstance));
  cmd.AddValue ("red", "Enable RED Queue Disiplone", MakeCallback (SetRED));
  cmd.AddValue ("dctcp", "Enable DCTCP Capability", MakeCallback (SetDCTCP));
  cmd.AddValue ("redmax", "RED Max Threshold", MakeCallback (SetREDmax));
  cmd.AddValue ("redmin", "RED min Threshold", MakeCallback (SetREDmin));
  cmd.AddValue ("redql", "RED Queue Limit", MakeCallback (SetREDQueueLimit));
  cmd.AddValue ("redweight", "RED Weight", MakeCallback (SetREDweight));
  cmd.AddValue ("DCTCPweight", "DCTCP Weight", MakeCallback (SetDCTCPweight));
  cmd.AddValue ("lfplot", "Activate plotting at MpTcpSocketBase", MakeCallback (SetLfPlot));
  cmd.AddValue ("sfplot", "Activate short flow plotting at MpTcpSocketBase", MakeCallback (SetSfPlot));
  cmd.AddValue ("st", "Simulation Time", MakeCallback (SetSimTime));
  cmd.AddValue ("na", "NetAnim: 1=enable, 0=disable", MakeCallback (SetNetAnim));
  cmd.AddValue ("cc", "MPTCP Congestion Control algorithm", MakeCallback (SetCongestionControl));
  cmd.AddValue ("gamma", " XMP's gamma", MakeCallback (SetXmpGamma));
  cmd.AddValue ("beta", " XMP's beta", MakeCallback (SetXmpBeta));
  cmd.AddValue ("rateplot", " Activate Rate Plotting", MakeCallback (SetRatePlot));
  cmd.AddValue ("ratebeat", " Activate Rate Plotting", MakeCallback (SetRateBeat));

  cmd.Parse (argc, argv);
  Config::SetDefault ("ns3::MpTcpSocketBase::IncastExitThresh", UintegerValue(g_incastExitThreshold));
  Config::SetDefault ("ns3::MpTcpSocketBase::IncastThresh", UintegerValue(g_incastThreshold));
  Config::SetDefault ("ns3::MpTcpSocketBase::DynamicSubflow", BooleanValue(g_dynamicSubflow));
  Config::SetDefault ("ns3::MpTcpSocketBase::CwndMin", UintegerValue (g_cwndMin));
  Config::SetDefault ("ns3::MpTcpSocketBase::RwndScale", UintegerValue (g_rwndScale));
  Config::SetDefault ("ns3::MpTcpSocketBase::DctcpAlphaPerAck", BooleanValue (g_dctcpAlphaPerAck)); //SHOULD BE FALSE!!!
  Config::SetDefault ("ns3::MpTcpSocketBase::SlowDownXmpLike", BooleanValue (g_slowDownXmpLike));
  Config::SetDefault ("ns3::MpTcpSocketBase::ECN", BooleanValue (g_ecn));
  Config::SetDefault ("ns3::MpTcpSocketBase::LargePlotting", BooleanValue (g_enableLfPlotting));
  Config::SetDefault ("ns3::MpTcpSocketBase::ShortPlotting", BooleanValue (g_enableSfPlotting));
  Config::SetDefault ("ns3::MpTcpSocketBase::RatePlotSf", BooleanValue (g_enableRatePlotting));
  Config::SetDefault ("ns3::MpTcpSocketBase::RatePlotCl", BooleanValue (g_enableRatePlotting));
  Config::SetDefault ("ns3::MpTcpSocketBase::RateInterval", DoubleValue (g_rateBeat));
  Config::SetDefault ("ns3::MpTcpSocketBase::RandomGap", UintegerValue (50));
  Config::SetDefault ("ns3::DropTailQueue::Mode", StringValue ("QUEUE_MODE_PACKETS"));
  Config::SetDefault ("ns3::DropTailQueue::MaxPackets", UintegerValue (100));
//Config::SetDefault ("ns3::DropTailQueue::Marking", BooleanValue(g_enableDCTCP));
//Config::SetDefault ("ns3::DropTailQueue::MarkingTh", UintegerValue(g_DTQmarkTh));
  if (g_enabledIncastInterval)
    {// Goodput of LFs should be calculated when all outstanding data has been acked!
      Config::SetDefault ("ns3::MpTcpSocketBase::BackgroundFlow", BooleanValue (false));
    }

  if (g_queueModeBytes)
    { // DropTailQueue
    //Config::SetDefault("ns3::DropTailQueue::MarkingTh", UintegerValue(g_DTQmarkTh * 1400));
      Config::SetDefault("ns3::DropTailQueue::Mode", StringValue("QUEUE_MODE_BYTES"));
      Config::SetDefault("ns3::DropTailQueue::MaxBytes", UintegerValue(g_REDQueueLimit * 1400));
    }
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (g_segmentSize));
  Config::SetDefault ("ns3::MpTcpSocketBase::gamma", UintegerValue (g_XmpGamma));
  Config::SetDefault ("ns3::MpTcpSocketBase::beta", UintegerValue (g_XmpBeta));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));

  if (g_enableDCTCP)
    {
      Config::SetDefault ("ns3::MpTcpSocketBase::DCTCP", BooleanValue (g_enableDCTCP));
      Config::SetDefault ("ns3::MpTcpBulkSendApplication::DCTCP", BooleanValue (g_enableDCTCP)); // Source Control
      Config::SetDefault ("ns3::MpTcpSocketBase::DCTCPWeight", DoubleValue (g_DCTCPWeight));
      Config::SetDefault ("ns3::RedQueue::UseCurrent", BooleanValue (true));
    }
  if (g_enableRED)
    {
      assert (g_REDmaxTh == g_REDminTh);
      Config::SetDefault ("ns3::RedQueue::Mode", StringValue ("QUEUE_MODE_PACKETS"));
      Config::SetDefault ("ns3::RedQueue::MeanPktSize", UintegerValue (g_REDMeanPktSize));
      Config::SetDefault ("ns3::RedQueue::Wait", BooleanValue (true));
      Config::SetDefault ("ns3::RedQueue::Gentle", BooleanValue (g_REDGentle));
      Config::SetDefault ("ns3::RedQueue::QW", DoubleValue (g_REDWeight));
      Config::SetDefault ("ns3::RedQueue::MinTh", DoubleValue (g_REDminTh));
      Config::SetDefault ("ns3::RedQueue::MaxTh", DoubleValue (g_REDmaxTh));
      Config::SetDefault ("ns3::RedQueue::QueueLimit", UintegerValue (g_REDQueueLimit));
      Config::SetDefault ("ns3::RedQueue::UseCurrent", BooleanValue (true));
      if (g_queueModeBytes)
        {
          Config::SetDefault("ns3::RedQueue::Mode", StringValue("QUEUE_MODE_BYTES"));
          Config::SetDefault("ns3::RedQueue::QueueLimit", UintegerValue(g_REDQueueLimit * 1400));
          Config::SetDefault("ns3::RedQueue::MinTh", DoubleValue(g_REDminTh * 1400));
          Config::SetDefault("ns3::RedQueue::MaxTh", DoubleValue(g_REDmaxTh * 1400));
        }
    }
  /* Uncoupled_TCPs, Linked_Increases, RTT_Compensator, Fully_Coupled */
  Config::SetDefault ("ns3::MpTcpSocketBase::CongestionControl", StringValue (g_cc));
  Config::SetDefault ("ns3::MpTcpBulkSendApplication::CongestionControl", StringValue (g_cc));
  Config::SetDefault ("ns3::Ipv4GlobalRouting::FlowEcmpRouting", BooleanValue (true));
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (MpTcpSocketBase::GetTypeId ()));
  Config::SetDefault ("ns3::MpTcpSocketBase::PathManagement", StringValue ("NdiffPorts"));
  Config::SetDefault ("ns3::MpTcpSocketBase::MaxSubflows", UintegerValue (g_subflows)); // Sink
  Config::SetDefault ("ns3::MpTcpBulkSendApplication::MaxSubflows", UintegerValue (g_subflows)); //Source

  g_seed = static_cast<uint32_t> (atoi (g_simInstance.c_str ()));
  cout << "Seed             : " << g_seed << endl;
  srand (g_seed);

  // Topology construction
  NS_LOG_INFO("Create nodes");
  // senders
  Sender_c.Create(g_senders);
  Total_c.Add(Sender_c);
  // core switches
  Core_c.Create(g_core_switches);
  Total_c.Add(Core_c);
  // receivers
  Receiver_c.Create(g_receivers);
  Total_c.Add(Receiver_c);

  NS_LOG_INFO("Install network stack");
  InternetStackHelper netStack;
  netStack.Install (Total_c);

  NS_LOG_INFO("Create channel");

  PointToPointHelper p2p;
  p2p.SetQueue ("ns3::DropTailQueue");
  p2p.SetDeviceAttribute ("DataRate", StringValue (g_linkCapacity));
  p2p.SetChannelAttribute ("Delay", StringValue (g_linkDelay));

  if (g_enableRED)
    {
      p2p.SetQueue ("ns3::RedQueue", // only backbone link has RED queue
          "LinkBandwidth", StringValue (g_linkCapacity), "LinkDelay", StringValue (g_linkDelay));
      p2p.SetDeviceAttribute ("DataRate", StringValue (g_linkCapacity));
      p2p.SetChannelAttribute ("Delay", StringValue (g_linkDelay));
    }

  // Initialized Address Helper
  NS_LOG_INFO("Assign IP addresses");
  Ipv4AddressHelper ipv4Address;

  // Connecting Senders to Edge Switches
  cout << "-------------------------------" << endl;
  cout << "Sender to Core" << endl;
  stringstream ss;
  for (uint32_t s = 0; s < Sender_c.GetN (); s++)
    {
      for (uint32_t c = 0; c < Core_c.GetN (); c++)
        {
          ss.str ("");
          ss << "10." << Sender_c.Get (s)->GetId () << "." << Core_c.Get (c)->GetId () << "." << "0";
          string tmp = ss.str ();
          cout << tmp << endl;
          const char* address = tmp.c_str ();
          ipv4Address.SetBase (address, "255.255.255.0");
          ipv4Address.Assign (p2p.Install (NodeContainer (Sender_c.Get (s), Core_c.Get (c))));
        }
    }
  // Connecting receiver edge switches to receivers
  cout << "Core to Receivers" << endl;
  for (uint32_t c = 0; c < Core_c.GetN (); c++)
    {
      for (uint32_t r = 0; r < Receiver_c.GetN (); r++)
        {
          ss.str ("");
          ss << "10." << Core_c.Get (c)->GetId () << "." << Receiver_c.Get (r)->GetId () << "." << "0";
          string tmp = ss.str ();
          cout << tmp << endl;
          const char* address = tmp.c_str ();
          ipv4Address.SetBase (address, "255.255.255.0");
          ipv4Address.Assign (p2p.Install (NodeContainer (Core_c.Get (c), Receiver_c.Get(r))));
        }
    }

  NS_LOG_INFO("Install Routing tables");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  QueueMonitor ();

  NS_LOG_INFO("Create Applications");
  // MPTCP SINK
  for (uint32_t i = 0; i < Receiver_c.GetN (); i++)
    {
      MpTcpPacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), servPort));
      ApplicationContainer sinkApp = sink.Install (Receiver_c.Get (i));
      sinkApp.Start (Seconds (0.0));
    }

  PrintParams();

  // MPTCP SOURCE
  if (g_enabledIncastInterval)
    {
      FlowSchedulerIncastInterval();
    }
  else
    {
      FlowSchedulerNormal();
    }

  // NetAnim
  if (g_enableAnim)
    { // Create Animation object and configure for specified output
      AnimationInterface anim ("incast-animation", 1000000000);
      anim.EnablePacketMetadata (true);
    }

  NS_LOG_INFO("Simulation run");
  SimTimeMonitor();//Simulator::Schedule (Seconds (1), &SimTimeMonitor);
  Simulator::Run ();
  Simulator::Stop (Seconds (100));
  GenerateQueuePlot ();
  GenerateQueueCDFPlot ();
  Simulator::Destroy ();
  NS_LOG_INFO("Simulation End");
}
