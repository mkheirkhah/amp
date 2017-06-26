/*
 * MultiPath-TCP (MPTCP) implementation.
 * Programmed by Morteza Kheirkhah from University of Sussex.
 * Some codes here are modeled from ns3::TCPNewReno implementation.
 * Email: m.kheirkhah@sussex.ac.uk
 */
#include <iostream>
#include "ns3/mp-tcp-typedefs.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "mp-tcp-subflow.h"

NS_LOG_COMPONENT_DEFINE("MpTcpSubflow");

namespace ns3{

NS_OBJECT_ENSURE_REGISTERED(MpTcpSubFlow);

TypeId
MpTcpSubFlow::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::MpTcpSubFlow")
      .SetParent<Object>()
      .AddConstructor<MpTcpSubFlow>()
      .AddTraceSource("cWindow",
          "The congestion control window to trace.",
           MakeTraceSourceAccessor(&MpTcpSubFlow::cwnd));
  return tid;
}

MpTcpSubFlow::MpTcpSubFlow() :
    routeId(0),
    state(CLOSED),
    sAddr(Ipv4Address::GetZero()),
    sPort(0),
    dAddr(Ipv4Address::GetZero()),
    dPort(0),
    oif(0),
    mapDSN(0),
    lastMeasuredRtt(Seconds(0.0))
{
  connected = false;
  TxSeqNumber = rand() % 1000;
  RxSeqNumber = 0;
  //lastTxSeqNumer = TxSeqNumber;
  bandwidth = 0;
  cwnd = 0;
  ssthresh = 65535;
  maxSeqNb = TxSeqNumber - 1;
  m_highTxMark = TxSeqNumber - 1;
  highestAck = 0;
  rtt = CreateObject<RttMeanDeviation>();
  rtt->Gain(0.1); //TODO: XMP uses 0.125 for the gain, shall we use as well?
  rtt->SetG((1.0 / 16.0));
  //rtt->SetExpectedNextSeq(SequenceNumber32(TxSeqNumber + 1));
  Time estimate;
  estimate = Seconds(1.5);
  rtt->SetCurrentEstimate(estimate);
  cnRetries = 3;
  Time est = MilliSeconds(200);
  cnTimeout = est;
  initialSequnceNumber = 0;
  m_retxThresh = 3;
  m_inFastRec = false;
  m_limitedTx = false;
  m_dupAckCount = 0;
  PktCount = 0;
  m_recover = SequenceNumber32(0);
  m_gotFin = false;
  AccumulativeAck = false;
  m_limitedTxCount = 0;
  m_duplicatesSize = 0;
  // ECN & DCTCP
  m_ssThreshLastChange = Simulator::Now(); // means zero somehow...
//  m_EcnState = NO_ECN;
  m_EcnEchoSeq = 0;
//  m_EcnTransition = false;
  dctcp_last_fraction = 0;
  dctcp_total = 0;
  dctcp_marked = 0;
  dctcp_alpha_update_seq = 0;
  dctcp_maxseq = 0;
  dctcp_alpha = 0.0;
  fast_alpha = 0.0;
  curEcnState = false;
  g_AckSeqNumber = 0;
  // XMP parameters
  m_begSeq = 0;
  m_baseRTT= Seconds(60.0);
  m_minRTT = Seconds(60.0);
  m_cnRTT  = 0;
  m_sumRTT = 0;
  m_rounds = 0;
  m_gamma = 2;
  m_weight = 1.0;
  m_equilibrium = 0;
  m_instantRate = 0;
  m_minQueueDelay = 0;
  m_bAvailable = false; // When established it becomes available
  m_cwr = 1; //0; init to 1 to no need to assign to 1 at subflow setup.
  m_refWin = 0;
  m_incCum = 0.0;
  m_nECE = 0;
  m_cwrHighSeq = 0;
  totalSentByte = 0;

//  DATA.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  ACK.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  DROP.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  ECN_CWND_CUT_POINT.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  ECN_ECHO.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  XMP_CWR1.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  XMP_CWR2.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  BEG.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  DCTCP_ALPHA.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  DCTCP_ALPHA_RTT.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  DCTCP_FRACTION.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  DCTCP_FRACTION_RTT.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  RETRANSMIT.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  _ss.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
//  _ca.push_back (make_pair (Simulator::Now ().GetSeconds (), 0));
}

MpTcpSubFlow::~MpTcpSubFlow()
{
  m_endPoint = 0;
  routeId = 0;
  sAddr = Ipv4Address::GetZero();
  oif = 0;
  state = CLOSED;
  bandwidth = 0;
  cwnd = 0;
  maxSeqNb = 0;
  m_highTxMark = 0;
  highestAck = 0;
  for (list<DSNMapping *>::iterator it = mapDSN.begin(); it != mapDSN.end(); ++it)
    {
      DSNMapping * ptrDSN = *it;
      delete ptrDSN;
    }
  mapDSN.clear();
}

bool
MpTcpSubFlow::Finished(void)
{
  return (m_gotFin && m_finSeq.GetValue() < RxSeqNumber);
}

void
MpTcpSubFlow::StartTracing(string traced)
{
  //NS_LOG_UNCOND("("<< routeId << ") MpTcpSubFlow -> starting tracing of: "<< traced);
  TraceConnectWithoutContext(traced, MakeCallback(&MpTcpSubFlow::CwndTracer, this)); //"CongestionWindow"
}

void
MpTcpSubFlow::CwndTracer(uint32_t oldval, uint32_t newval)
{
  //NS_LOG_UNCOND("Subflow "<< routeId <<": Moving cwnd from " << oldval << " to " << newval);
  cwndTracer.push_back(make_pair(Simulator::Now().GetSeconds(), newval));
  sstTracer.push_back(make_pair(Simulator::Now().GetSeconds(), ssthresh));
  rttTracer.push_back(make_pair(Simulator::Now().GetSeconds(), rtt->GetCurrentEstimate().GetMicroSeconds()));
  rtoTracer.push_back(make_pair(Simulator::Now().GetSeconds(), rtt->RetransmitTimeout().GetMilliSeconds()));
}

void
MpTcpSubFlow::RateTracerSf(double &interval, bool &flowCompletionTime)
{
//  double sampledGoodput = (((TxSeqNumber - lastTxSeqNumer) * 8) / interval);
//  sampledGoodput = sampledGoodput/1000000;
  double sampledGoodput = ((totalSentByte * 8) / interval);
  sampledGoodput = sampledGoodput/1000000;
  rateTracerSf.push_back(make_pair(Simulator::Now().GetSeconds(), sampledGoodput));
//  lastTxSeqNumer = TxSeqNumber;
  totalSentByte = 0;
  if (flowCompletionTime)
    nextRateEvent = Simulator::Schedule (Seconds (interval), &MpTcpSubFlow::RateTracerSf, this, interval, flowCompletionTime);
}

void
MpTcpSubFlow::AddDSNMapping(uint8_t sFlowIdx, uint64_t dSeqNum, uint16_t dLvlLen, uint32_t sflowSeqNum, uint32_t ack/*,
    Ptr<Packet> pkt*/)
{
  NS_LOG_FUNCTION_NOARGS();
  mapDSN.push_back(new DSNMapping(sFlowIdx, dSeqNum, dLvlLen, sflowSeqNum, ack/*, pkt*/));
}

void
MpTcpSubFlow::SetFinSequence(const SequenceNumber32& s)
{
  NS_LOG_FUNCTION (this);
  m_gotFin = true;
  m_finSeq = s;
  if (RxSeqNumber == m_finSeq.GetValue())
    ++RxSeqNumber;
}

DSNMapping *
MpTcpSubFlow::GetunAckPkt()
{
  NS_LOG_FUNCTION(this);
  DSNMapping * ptrDSN = 0;
  for (list<DSNMapping *>::iterator it = mapDSN.begin(); it != mapDSN.end(); ++it)
    {
      DSNMapping * ptr = *it;
      if (ptr->subflowSeqNumber == highestAck + 1)
        {
          ptrDSN = ptr;
          break;
        }
    }
  return ptrDSN;
}
}
