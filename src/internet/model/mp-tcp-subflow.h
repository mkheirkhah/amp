/*
 * MultiPath-TCP (MPTCP) implementation.
 * Programmed by Morteza Kheirkhah from University of Sussex.
 * Some codes here are modeled from ns3::TCPNewReno implementation.
 * Email: m.kheirkhah@sussex.ac.uk
 */
#ifndef MP_TCP_SUBFLOW_H
#define MP_TCP_SUBFLOW_H

#include <stdint.h>
#include <vector>
#include <queue>
#include <list>
#include <set>
#include <map>
#include "ns3/object.h"
#include "ns3/uinteger.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/sequence-number.h"
#include "ns3/rtt-estimator.h"
#include "ns3/event-id.h"
#include "ns3/packet.h"
#include "ns3/tcp-socket.h"
#include "ns3/ipv4-end-point.h"
#include "ns3/ipv4-address.h"

using namespace std;

namespace ns3{

class MpTcpSubFlow : public Object
{
public:
  static TypeId
  GetTypeId(void);

  MpTcpSubFlow();
  ~MpTcpSubFlow();

  void AddDSNMapping(uint8_t sFlowIdx, uint64_t dSeqNum, uint16_t dLvlLen, uint32_t sflowSeqNum, uint32_t ack/*, Ptr<Packet> pkt*/);
  void StartTracing(string traced);
  void CwndTracer(uint32_t oldval, uint32_t newval);
  void SetFinSequence(const SequenceNumber32& s);
  bool Finished();
  DSNMapping *GetunAckPkt();
  void RateTracerSf(double &interval, bool &flowCompletionTime);

  uint16_t routeId;           // Subflow's ID
  bool connected;             // Subflow's connection status
  TcpStates_t state;          // Subflow's connection state
  Ipv4Address sAddr;          // Source Ip address
  uint16_t sPort;             // Source port
  Ipv4Address dAddr;          // Destination address
  uint16_t dPort;             // Destination port
  uint32_t oif;               // interface related to the subflow's sAddr
  EventId retxEvent;          // Retransmission timer
  EventId m_lastAckEvent;     // Timer for last ACK
  EventId m_timewaitEvent;    // Timer for closing connection at sender side
  EventId nextRateEvent;
  uint32_t MSS;               // Maximum Segment Size
  uint32_t cnCount;           // Count of remaining connection retries
  uint32_t cnRetries;         // Number of connection retries before giving up
  Time     cnTimeout;         // Timeout for connection retry
  TracedValue<uint32_t> cwnd; // Congestion window (in bytes)
  uint32_t ssthresh;          // Slow start threshold
  uint32_t maxSeqNb;          // Highest sequence number of a sent byte. Equal to (TxSeqNumber - 1) until a retransmission occurs
  uint32_t highestAck;        // Highest received ACK for the subflow level sequence number
  uint64_t bandwidth;         // Link's bandwidth
  uint32_t m_initialCWnd;     // Initial cWnd value
  SequenceNumber32 m_recover; // Previous highest Tx seqNb for fast recovery
  uint32_t m_retxThresh;      // Fast Retransmit threshold
  bool m_inFastRec;           // Currently in fast recovery
  bool m_limitedTx;           // perform limited transmit
  uint32_t m_dupAckCount;     // DupACK counter
  Ipv4EndPoint* m_endPoint;   // L4 stack object
  list<DSNMapping *> mapDSN;  // List of all sent packets
  multiset<double> measuredRTT;
  Ptr<RttMeanDeviation> rtt;  // RTT calculator
  Time lastMeasuredRtt;       // Last measured RTT, used for plotting
  uint32_t TxSeqNumber;       // Subflow's next expected sequence number to send
  uint32_t RxSeqNumber;       // Subflow's next expected sequence number to receive
//uint32_t lastTxSeqNumer;
  uint32_t totalSentByte;
  uint64_t PktCount;          // number of sent packets
  bool m_gotFin;              // Whether FIN is received
  SequenceNumber32 m_finSeq;  // SeqNb of received FIN
  bool AccumulativeAck;
  uint32_t m_limitedTxCount;
  uint32_t initialSequnceNumber; // Plotting
  uint32_t  m_duplicatesSize; //The size of the inflation we do during fast recovery
  uint32_t  m_highTxMark;
  // DCTCP
  Time m_ssThreshLastChange;
//  TracedValue<uint32_t> m_EcnState;         // Current ECN State, represented as combination of EcnState values
  TracedValue<SequenceNumber32> m_EcnEchoSeq; // Sequence number of the last received ECN echo
//  bool m_EcnTransition;
  uint32_t dctcp_total;
  uint32_t dctcp_marked;
  uint32_t dctcp_alpha_update_seq;
  uint32_t dctcp_maxseq;
  double dctcp_alpha;
  double fast_alpha;
  bool curEcnState;
  uint32_t g_AckSeqNumber;
  double dctcp_last_fraction;
  //XMP parameters
  uint32_t m_begSeq;
  Time     m_baseRTT; // min sample rtt observed
  Time     m_minRTT; // min sample rtt during last RTT
  uint32_t m_cnRTT;  // the number of sample rtts during last RTT
  uint64_t m_sumRTT; // the sum of sample rtts during last RTT, us
  uint64_t m_rounds;
  double   m_gamma;  // threshold, when above it, bi-increase exits.
  double   m_weight;
  double   m_equilibrium;
  double   m_instantRate;
  uint32_t m_minQueueDelay; // us
  bool     m_bAvailable;
  uint32_t m_cwr;    // 0: disable, 1: normal, 2: cwr
  uint32_t m_refWin; // used to determine a round
  double   m_incCum;
  uint32_t m_nECE;
  SequenceNumber32 m_cwrHighSeq; // used to determine when to quit from cwr

  //plotting
  vector<pair<double, uint32_t> > cwndTracer;
  vector<pair<double, uint32_t> > sstTracer;
  vector<pair<double, double> > rtoTracer;
  vector<pair<double, double> > rttTracer;
  vector<pair<double, double> > rateTracerSf;

  vector<pair<double, double> > ECN_ECHO;
  vector<pair<double, double> > ECN_CWND_CUT_POINT;
  vector<pair<double, double> > XMP_CWR1;
  vector<pair<double, double> > XMP_CWR2;
  vector<pair<double, double> > BEG;
  vector<pair<double, double> > DCTCP_ALPHA;
  vector<pair<double, double> > DCTCP_FRACTION;
  vector<pair<double, double> > DCTCP_ALPHA_RTT;
  vector<pair<double, double> > DCTCP_FRACTION_RTT;
  vector<pair<double, double> > ssthreshtrack;
  vector<pair<double, double> > CWNDtrack;
  vector<pair<double, uint32_t> > DATA;
  vector<pair<double, uint32_t> > ACK;
  vector<pair<double, uint32_t> > DROP;
  vector<pair<double, uint32_t> > RETRANSMIT;
  vector<pair<double, uint32_t> > DUPACK;
  vector<pair<double, double> > _ss;
  vector<pair<double, double> > _ca;
  vector<pair<double, double> > _FR_FA;
  vector<pair<double, double> > _FR_PA;
  vector<pair<double, double> > _FReTx;
  vector<pair<double, double> > _TimeOut;
  vector<pair<double, double> > _RTT;
  vector<pair<double, double> > _AvgRTT;
  vector<pair<double, double> > _RTO;
};

}
#endif /* MP_TCP_SUBFLOW */
