/*
 * Morteza-MultiPath-TCP (MMPTCP) implementation.
 * Programmed by Morteza Kheirkhah from University of Sussex.
 * Email: m.kheirkhah@sussex.ac.uk
 * WebPage: http://www.sussex.ac.uk/Users/mk382/
 */
#define NS_LOG_APPEND_CONTEXT \
  if (m_node) { std::clog << Simulator::Now ().GetSeconds () << " [node " << m_node->GetId () << "] "; }

#include "mmp-tcp-socket-base.h"
#include "tcp-l4-protocol.h"
#include "ns3/log.h"
#include "ns3/error-model.h"
#include "ns3/node.h"


NS_LOG_COMPONENT_DEFINE("MMpTcpSocketBase");

using namespace std;
namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(MMpTcpSocketBase);

TypeId
MMpTcpSocketBase::GetTypeId(void)
{
  static TypeId tid =
      TypeId("ns3::MMpTcpSocketBase")
      .SetParent<MpTcpSocketBase>()
      .AddConstructor<MMpTcpSocketBase>()
      .AddAttribute(
          "SwitchingMode",
          "Mechanism for switching from packet-scatter to mptcp",
          EnumValue(FlowSize),
          MakeEnumAccessor(&MMpTcpSocketBase::SetSwitchingMode),
          MakeEnumChecker(FlowSize, "FlowSize", CwndSize, "CwndSize", CwndSignal, "CwndSignal"))
      .AddAttribute("FlowSizeThresh",
          "Threshold related to total byte sent by MPTCP connection",
          UintegerValue(100000), //100Kb
          MakeUintegerAccessor(&MMpTcpSocketBase::SetFlowSizeThresh),
          MakeUintegerChecker<uint32_t>())
//      .AddAttribute ("DupAckThresh", "Threshold for fast retransmit",
//                  UintegerValue (0),
//                  MakeUintegerAccessor (&MMpTcpSocketBase::m_dupAckThresh),
//                  MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("RTOmin", "RTOmin for initial subflow (packet scatter)",
                  UintegerValue (200),
                  MakeUintegerAccessor (&MMpTcpSocketBase::m_rtoMin),
                  MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("IsLimitedTx", " ",
                  BooleanValue (false),
                  MakeBooleanAccessor (&MMpTcpSocketBase::m_isLimitedTx),
                  MakeBooleanChecker())
      .AddAttribute ("IsRTObackoff", " ",
                  BooleanValue (false),
                  MakeBooleanAccessor (&MMpTcpSocketBase::m_isRTObackoff),
                  MakeBooleanChecker())
      .AddAttribute ("IsRTOmin", " ",
                  BooleanValue (false),
                  MakeBooleanAccessor (&MMpTcpSocketBase::m_isRTOmin),
                  MakeBooleanChecker())
      .AddAttribute ("MMPTCPV2", " ",
                  BooleanValue (false),
                  MakeBooleanAccessor (&MMpTcpSocketBase::m_mmptcpv2),
                  MakeBooleanChecker())
      .AddAttribute ("MMPTCPV3", "DCTCP Link Congestion Control ",
                  BooleanValue (false),
                  MakeBooleanAccessor (&MMpTcpSocketBase::m_mmptcpv3),
                  MakeBooleanChecker())
      .AddAttribute ("SwitchingCondition", "Switching Condition ",
                  BooleanValue (false),
                  MakeBooleanAccessor (&MMpTcpSocketBase::m_switchingCondition),
                  MakeBooleanChecker())
      .AddAttribute ("IsThinStream", " ",
                  BooleanValue (false),
                  MakeBooleanAccessor (&MMpTcpSocketBase::m_isThinStream),
                  MakeBooleanChecker());
  return tid;
}

MMpTcpSocketBase::MMpTcpSocketBase() :
    MpTcpSocketBase(), m_packetScatter(true), m_subflowInitiation(true), m_totalBytesSent(0), m_dupAckThresh(0)
{
  NS_LOG_FUNCTION(this << m_totalBytesSent << m_packetScatter << subflows.size());
}

MMpTcpSocketBase::~MMpTcpSocketBase()
{
  NS_LOG_FUNCTION(this);
}

void
MMpTcpSocketBase::DoForwardUp(Ptr<Packet> p, Ipv4Header header, uint16_t port, Ptr<Ipv4Interface> interface)
{
  NS_LOG_FUNCTION(this); //
  //NS_LOG_UNCOND("DoForwardUp() -> Subflows: " << subflows.size());

  if (m_endPoint == 0)
    {
      NS_LOG_UNCOND(this << "[" << m_node->GetId() << "] No endpoint exist");
      return;
    }

  Address fromAddress = InetSocketAddress(header.GetSource(), port);
  Address toAddress = InetSocketAddress(header.GetDestination(), m_endPoint->GetLocalPort());

  m_localAddress = header.GetDestination();
  m_remoteAddress = header.GetSource();
  /*
   * This is the port number of incoming packet. This could be not the same as endpoint port
   */
  m_remotePort = port;

  // Peel off TCP header and do validity checking
  TcpHeader mptcpHeader;
  p->RemoveHeader(mptcpHeader);

  // DCTCP
  ExtractPacketTags(p);

  m_localPort = mptcpHeader.GetDestinationPort();
  //NS_ASSERT(m_localPort == m_endPoint->GetLocalPort());

  // Listening socket being dealt with here......
  if (subflows.size() == 0 && m_state == LISTEN)
    {
      NS_ASSERT(server && m_state == LISTEN);
      NS_LOG_UNCOND( this<< " Listening socket, it seems it need to be CLONED... " << mptcpHeader << " LocalTOken: " << localToken);

      // Update the flow control window
      remoteRecvWnd = (uint32_t) mptcpHeader.GetWindowSize();

      // We need to define another ReadOption with no subflow in it
      if (ReadOptions(p, mptcpHeader) == false)
        return;

      // We need to define another ProcessListen with no subflow in it
      ProcessListen(p, mptcpHeader, fromAddress, toAddress);

      // Reset all variables after cloning is ended to ready for next connection
      mpRecvState = MP_NONE;
      mpEnabled = false;
      remoteToken = 0;
      localToken = 0;
      remoteRecvWnd = 1;
      return;
    }

  // Accepted sockets being dealt with from here on .......
  // Lookup for a subflow based on 4-tuple of incoming packet
  int sFlowIdx;

  // PS: Re-directing PACKET-SCATTER to subflow[0] of receiver based on pScatter flag in OPT_DSN.
  if (IsPktScattered(mptcpHeader))
    {
      sFlowIdx = 0;
      NS_ASSERT(server);
      NS_ASSERT(subflows.size() >= 1);
      NS_ASSERT(subflows[0]->sAddr == m_localAddress);
      NS_ASSERT(subflows[0]->dAddr == m_remoteAddress);
      NS_ASSERT(subflows[0]->sPort == m_localPort);
      //NS_LOG_UNCOND("DoForwardUp() -> Incoming packet is packet-scatter! - this condition only executed in receiver side!");
    }
  else
    { // pScatter param of incoming packet is false (i.e., zero).
      //NS_LOG_UNCOND("DoForwardUp() -> Packet seems MP-TCP and pScatter: " << m_packetScatter);
      sFlowIdx = LookupSubflow(m_localAddress, m_localPort, m_remoteAddress, m_remotePort);
    }

  NS_ASSERT_MSG(sFlowIdx <= maxSubflows, "Subflow number should be smaller than MaxNumOfSubflows");
  NS_ASSERT_MSG(sFlowIdx >= 0,
      "sFlowIdx is -1, i.e., invalid packet received - This is not a bug we need to deal with it - sFlowIdx: "<< sFlowIdx);

  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  //uint32_t dataLen;   // packet's payload length
  remoteRecvWnd = (uint32_t) mptcpHeader.GetWindowSize(); //update the flow control window

  if (mptcpHeader.GetFlags() & TcpHeader::ACK)
    { // This function update subflow's lastMeasureRtt variable.
      EstimateRtt(sFlowIdx, mptcpHeader);
    }

  if (ReadOptions(sFlowIdx, p, mptcpHeader) == false)
    return;

  // TCP state machine code in different process functions
  // C.f.: tcp_rcv_state_process() in tcp_input.c in Linux kernel
  currentSublow = sFlow->routeId;
  switch (sFlow->state)
    {
  case ESTABLISHED:
    ProcessEstablished(sFlowIdx, p, mptcpHeader);
    break;
  case LISTEN:
    ProcessListen(sFlowIdx, p, mptcpHeader, fromAddress, toAddress);
    break;
  case TIME_WAIT:
    // Do nothing
    break;
  case CLOSED:
    NS_LOG_INFO(" ("<< sFlow->routeId << ") " << TcpStateName[sFlow->state] << " -> Send RST");
    // Send RST if the incoming packet is not a RST
    if ((mptcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG)) != TcpHeader::RST)
      { // Since sFlow->m_endPoint is not configured yet, we cannot use SendRST here
        TcpHeader h;
        h.SetFlags(TcpHeader::RST);
        h.SetSequenceNumber(SequenceNumber32(sFlow->TxSeqNumber));
        h.SetAckNumber(SequenceNumber32(sFlow->RxSeqNumber));
        h.SetSourcePort(sFlow->sPort);
        h.SetDestinationPort(sFlow->dPort);
        h.SetWindowSize(AdvertisedWindowSize());
        m_tcp->SendPacket(Create<Packet>(), h, header.GetDestination(), header.GetSource(),
        FindOutputNetDevice(header.GetDestination()));
      }
    break;
  case SYN_SENT:
    ProcessSynSent(sFlowIdx, p, mptcpHeader);
    break;
  case SYN_RCVD:
    ProcessSynRcvd(sFlowIdx, p, mptcpHeader, fromAddress, toAddress);
    break;
  case FIN_WAIT_1:
  case FIN_WAIT_2:
  case CLOSE_WAIT:
    ProcessWait(sFlowIdx, p, mptcpHeader);
    break;
  case CLOSING:
    //ProcessClosing(sFlowIdx, p, mptcpHeader);
    break;
  case LAST_ACK:
    ProcessLastAck(sFlowIdx, p, mptcpHeader);
    break;
  default:
    // mute compiler
    break;
    }
}

/** Received a packet upon SYN_SENT */
void
MMpTcpSocketBase::ProcessSynSent(uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << mptcpHeader);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mptcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG);

  // Execute a action based on tcpflags
  if (tcpflags == 0)
    { // Bare data, accept it and move to ESTABLISHED state. This is not a normal behaviour. Remove this?
      NS_ASSERT(tcpflags != 0);
    }
  else if (tcpflags == TcpHeader::ACK)
    {
      NS_ASSERT(tcpflags != TcpHeader::ACK);
    }
  else if (tcpflags == TcpHeader::SYN)
    {
      NS_ASSERT(tcpflags != TcpHeader::SYN);
    }
  else if (tcpflags == (TcpHeader::SYN | TcpHeader::ACK))
    { // Handshake completed for sender... Send final ACK
      //NS_LOG_UNCOND("---------------------- HandShake is Completed in ClientSide ----------------------" << subflows.size());
      if (!m_connected)
        { // This function only excute for initial subflow since when it has established then MPTCP connection is already established!!
          m_connected = true;
          m_endPoint->SetPeer(m_remoteAddress, m_remotePort); // TODO Is this needed at all?
          fLowStartTime = Simulator::Now().GetSeconds();      // It seems to be in right location for FCT!!
        }NS_LOG_INFO("(" << sFlow->routeId << ") "<< TcpStateName[sFlow->state] << " -> ESTABLISHED");
      //cout <<Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "](" << sFlow->routeId << ") "<< TcpStateName[sFlow->state] << " -> ESTABLISHED" << endl;
      sFlow->state = ESTABLISHED;
      sFlow->retxEvent.Cancel();
      if ((m_largePlotting && (flowType.compare("Large") == 0)) || (m_shortPlotting && (flowType.compare("Short") == 0)))
        sFlow->StartTracing("cWindow");
      sFlow->rtt->Init(mptcpHeader.GetAckNumber());
      sFlow->initialSequnceNumber = (mptcpHeader.GetAckNumber().GetValue());
      NS_LOG_INFO("(" <<sFlow->routeId << ") InitialSeqNb of data packet should be --->>> " << sFlow->initialSequnceNumber << " Cwnd: " << sFlow->cwnd);
      sFlow->RxSeqNumber = (mptcpHeader.GetSequenceNumber()).GetValue() + 1;
      sFlow->highestAck = std::max(sFlow->highestAck, mptcpHeader.GetAckNumber().GetValue() - 1);
      sFlow->TxSeqNumber = mptcpHeader.GetAckNumber().GetValue();
      sFlow->maxSeqNb = sFlow->TxSeqNumber - 1;
      sFlow->m_highTxMark = sFlow->TxSeqNumber - 1; // cwnd blowup
      Time estimate;
      estimate = Seconds(1.5);
      sFlow->rtt->SetCurrentEstimate(estimate);

      SendEmptyPacket(sFlowIdx, TcpHeader::ACK);

      // Advertise available addresses...
      if (addrAdvertised == false)
        {
          NS_LOG_WARN("---------------------- AdvertiseAvailableAddresses By Client ---------------------");
          switch (pathManager)
            {
          case Default:
            // No address advertisement
            break;
          case FullMesh:
            // Address need to be advertised
            AdvertiseAvailableAddresses();
            break;
          case NdiffPorts:
            // New subflow can be initiated based on random source ports
            InitiateMultipleSubflows();
            break;
          default:
            break;
            }
          addrAdvertised = true;
        }

      // New Condition mmptcpv2
      if (m_mmptcpv2 && flowType.compare("Large") == 0 && m_subflowInitiation && m_packetScatter)
        {
          m_subflowInitiation = false;
          m_packetScatter = false;
          InitiateMultipleSubflows();
        }

      //------------------------
      if (m_state != ESTABLISHED)
        {
          m_state = ESTABLISHED;
          NotifyConnectionSucceeded();
        }

      // PS: Update subflow[0]->m_retxThresh to m_dupAckThresh
      if (sFlowIdx == 0)
        {
          // Initial window to 2
          //sFlow->cwnd = sFlow->MSS * m_initialCWnd;
          //cout << sFlow->cwnd << " initialCWND: " << m_initialCWnd << endl;

          sFlow->m_retxThresh = 3 + m_dupAckThresh;
          //cout << " retxThresh: " << sFlow->m_retxThresh << endl;
          if (m_isLimitedTx)
            sFlow->m_limitedTx = true;
          if (m_isRTOmin)
            sFlow->rtt->SetMinRto(MilliSeconds(m_rtoMin));
          NS_LOG_UNCOND("[" <<m_node->GetId()<< "] reTxThresh of subflow " << (int)sFlowIdx<< " has changed to " << sFlow->m_retxThresh << " DupAckThresh: " << m_dupAckThresh << " FlowSizeThresh: "<< m_flowSizeThresh);
        }

      // Initial window change for both PS and MPTCP phase of MMPTCP
      sFlow->cwnd = sFlow->MSS * m_initialCWnd;
      //cout << "[" << m_node->GetId() << ":" << flowId << "]("<< sFlow->routeId <<") cwnd: " << sFlow->cwnd << " iw: " << m_initialCWnd << endl;

      // PS: Switch to MPTCP - Should be before SendPendingData
      if (sFlowIdx != 0 && m_packetScatter && subflows.size() > 1 && !m_subflowInitiation)
        {
          NS_LOG_UNCOND("["<< m_node->GetId() << "] -> Switched to MPTCP! CWND: " << subflows[0]->cwnd.Get());
          cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "](" << (int) sFlowIdx << ") -> Switched to MPTCP! CWND: " << subflows[0]->cwnd.Get()  << " TotalByteSent: "<< m_totalBytesSent << endl;
          m_packetScatter = false;
        }

      if (subflows.size() > 1)
        {
          SendPendingData(sFlowIdx); // in processSynSent()
        }
      //NS_LOG_UNCOND("ProcessSynSent -> SubflowsSize: " << subflows.size());
    } // end of else if (SYN/ACK)
  else
    { // Other in-sequence input
      if (tcpflags != TcpHeader::RST)
        { // When (1) rx of FIN+ACK; (2) rx of FIN; (3) rx of bad flags
          NS_LOG_LOGIC ("Illegal flag " << std::hex << static_cast<uint32_t> (tcpflags) << std::dec << " received. Reset packet is sent.");NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") Bad FtcpFlag received - SendRST");
          cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") {"<< flowId <<"} SendRST(ProcessSynSent)" << endl;
          SendRST(sFlowIdx);
        }
      CloseAndNotifyAllSubflows();
    }
}

/** Process the newly received ACK */
void
MMpTcpSocketBase::ReceivedAck(uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << sFlowIdx << mptcpHeader);

  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint32_t ack = (mptcpHeader.GetAckNumber()).GetValue();
  sFlow->g_AckSeqNumber = ack;

  if (m_DCTCP)
    {
      CalculateDCTCPAlpha (sFlowIdx, ack);
    }

#ifdef PLOT
  uint32_t tmp = ((ack - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->ACK.push_back(make_pair(Simulator::Now().GetSeconds(), tmp));
#endif

  //PS: InitiateSubflows first then switch to MPTCP
  if (m_packetScatter && m_subflowInitiation)
    {
      switch (m_switchingMode)
        {
      case FlowSize:
        if (m_totalBytesSent > m_flowSizeThresh && maxSubflows > 1)
          {
            if (m_switchingCondition)
              {
                if ((sFlow->cwnd < sFlow->ssthresh) || sFlow->m_inFastRec || (sFlow->maxSeqNb > sFlow->TxSeqNumber - 1))
                  {
                    //cout << "Condition for Switching can't be met" << endl;
                    return;
                  }
              }
            //m_packetScatter = false;
            m_subflowInitiation = false;
            MpTcpSocketBase::InitiateMultipleSubflows();
            cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") FlowType(" <<flowType << ") cwnd[" << sFlow->cwnd<< "] SwitchMode(" << "FlowSize" <<")"<< endl;
            NS_LOG_UNCOND("Prepare to switch to MPTCP by initiating subflows: " << subflows.size() << " totalBytesSend: " << m_totalBytesSent << " Threshold: " << m_flowSizeThresh << " PacketScatterMode: " << m_packetScatter << " MaxSubflow: " << (int)maxSubflows);
          }
        break;
      case CwndSize:
        if (IsCwndExceedThresh(sFlowIdx) && maxSubflows > 1)
          {
            //m_packetScatter = false;
            m_subflowInitiation = false;
            MpTcpSocketBase::InitiateMultipleSubflows();
            cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") FlowType(" <<flowType << ") cwnd[" << sFlow->cwnd<< "] SwitchMode(" << "CwndSize" <<")"<< endl;
            NS_LOG_UNCOND("Prepare to switch to MPTCP by initiating subflows: " << subflows.size() << " totalBytesSend: " << m_totalBytesSent << " Threshold: " << m_flowSizeThresh << " PacketScatterMode: " << m_packetScatter << " MaxSubflow: " << (int)maxSubflows);
          }
        break;
      default:
        break;
        }
    }

  // Stop execution if TCPheader is not ACK at all.
  if (0 == (mptcpHeader.GetFlags() & TcpHeader::ACK))
    { // Ignore if no ACK flag
// This might be the case when a call comes from ProcessSynRcvd() where receiver try to receive data packet...!
//NS_ASSERT(3!=3);
    }
  // Received ACK. Compare the ACK number against highest unacked seqno.
  else if (ack <= sFlow->highestAck + 1)
    {
      NS_LOG_LOGIC ("This acknowlegment" << mptcpHeader.GetAckNumber () << "do not ack the latest data in subflow level");
      list<DSNMapping *>::iterator current = sFlow->mapDSN.begin();
      list<DSNMapping *>::iterator next = sFlow->mapDSN.begin();
      while (current != sFlow->mapDSN.end())
        {
          ++next;
          DSNMapping *ptrDSN = *current;
          // All segments before ackSeqNum should be removed from the mapDSN list.
          if (ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength <= ack)
            { // Optional task ...
              //next = sFlow->mapDSN.erase(current);
              //delete ptrDSN;
            }
          // There is a sent segment with subflowSN equal to ack but the ack is smaller than already receveid acked!
          else if ((ptrDSN->subflowSeqNumber == ack) && (ack < sFlow->highestAck + 1))
            { // Case 1: Old ACK, ignored.
              NS_LOG_WARN ("Ignored ack of " << mptcpHeader.GetAckNumber());
              NS_ASSERT(3!=3);
              break;
            }
          // There is a sent segment with requested SequenceNumber and ack is for first unacked byte!!
          else if ((ptrDSN->subflowSeqNumber == ack) && (ack == sFlow->highestAck + 1))
            { // Case 2: Potentially a duplicated ACK, so ack should be smaller than nextExpectedSN to send.
              if (ack < sFlow->TxSeqNumber)
                {
                  //NS_LOG_ERROR(Simulator::Now().GetSeconds()<< " [" << m_node->GetId()<< "] Duplicated ack received for SeqgNb: " << ack << " DUPACKs: " << sFlow->m_dupAckCount + 1);
                  DupAck(sFlowIdx, ptrDSN);
                  break;
                }
              // otherwise, the ACK is precisely equal to the nextTxSequence
              NS_ASSERT(ack <= sFlow->TxSeqNumber);
              break;
            }
          current = next;
        }
    }
  else if (ack > sFlow->highestAck + 1)
    { // Case 3: New ACK, reset m_dupAckCount and update m_txBuffer (DSNMapping List)
      NS_LOG_WARN ("New ack of " << mptcpHeader.GetAckNumber ());
      NewAckNewReno(sFlowIdx, mptcpHeader, 0);
      sFlow->m_dupAckCount = 0;
    }
  // If there is any data piggybacked, store it into m_rxBuffer
  if (packet->GetSize() > 0)
    {
      //NS_ASSERT(3!=3);
      NS_LOG_WARN(this << " ReceivedAck -> There is data piggybacked, deal with it...");
      ReceivedData(sFlowIdx, packet, mptcpHeader);
    }
  if (!server)
    IsLastAck();

  // Find last data acked ... for generating output file!
  /*
  if ((subflows[0]->state >= FIN_WAIT_1 || subflows[0]->state == CLOSED) && flowCompletionTime)
    {
      // Client only generate output data to file
      if (server)
        return;

      int dataLeft = 0;
      int pktCount = 0;
      for (uint32_t i = 0; i < subflows.size(); i++)
        {
          dataLeft += subflows[i]->mapDSN.size();
          pktCount += subflows[i]->PktCount;
        }

      if (pktCount == 0)
        {
          cerr << "["<< m_node->GetId() << "] (" << (int)sFlowIdx<< ") pktCount is zero!" << endl;
          NS_LOG_WARN("["<< m_node->GetId() << "] (" << (int)sFlowIdx<< ") pktCount is zero!");
        }

      if (dataLeft == 0)
        {
          flowCompletionTime = false;
          GenerateOutPutFile();
        }
    }
    */
}
/** Need to have OPT_DSN and pScatter with true value and correct localToken */
bool
MMpTcpSocketBase::IsPktScattered(const TcpHeader &mptcpHeader)
{
  NS_LOG_FUNCTION(this << mptcpHeader);
  vector<TcpOptions*> mp_options = mptcpHeader.GetOptions();
  TcpOptions *opt;
  for (uint32_t j = 0; j < mp_options.size(); j++)
    {
      opt = mp_options[j];
      if ((opt->optName == OPT_DSN))
        {
          uint8_t pScatter = ((OptDataSeqMapping *) opt)->pScatter;
          if (pScatter > 0)
            {
              uint32_t token = ((OptDataSeqMapping *) opt)->receiverToken;
              NS_ASSERT(token == localToken);
              return true;
            }
        }
    }
  return false;
}

uint8_t
MMpTcpSocketBase::getSubflowToUse()
{
  //NS_LOG_FUNCTION(this);
  uint8_t nextSubflow = 0;
  // PS: Send to subflow 0 when ps mode is active
  if (m_packetScatter)
    {
      NS_ASSERT(subflows.size() == 1);
      return nextSubflow; // return to subflow 0
    }
  else
    {
      nextSubflow = ((lastUsedsFlowIdx + 1) % subflows.size());
    }

  // No data should be sent on subflow 0 if packetScatter is not active
  if (nextSubflow == 0 && !m_packetScatter && subflows.size() > 1)
    {
      nextSubflow++;
      return nextSubflow;
    }
  else
    return nextSubflow;
}

/**
 * Sending data via subflows with available window size.
 * It sends data only to ESTABLISHED subflows.
 * It sends data by calling SendDataPacket() function.
 * Called by functions: SendBufferedData(), ReceveidAck(routeId),
 * NewAck(routeID) & DupAck(routeID), ProcessSynSent(routeId)
 */
bool
MMpTcpSocketBase::SendPendingData(uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION(this);

  /* PS NOTE:
   * PacketScatter had switched to MP-TCP
   * The last data ACK is received at master sub-flow, so...
   * NewACK() -> reTxEvent is re-scheduled -> DiscardUp(ACK) ->
   * However, reTxEvent should be cancel as there is no further data would be sent via this subflows.
   */
  if (sFlowIdx == 0 && !m_packetScatter && subflows[0]->mapDSN.size() == 0 && subflows[0]->state != FIN_WAIT_1
      && subflows[0]->state != CLOSING)
    {
      NS_LOG_UNCOND("["<< m_node->GetId()<< "] SendPendingData() -> !PacketScatter & MasterSubflow & SubflowBuffer = 0 -> State: " << TcpStateName[subflows[0]->state] << " -> Cancel reTxEvent!");
      cout << "["<< m_node->GetId()<< "]{" <<flowId << "} SendPendingData() -> !PacketScatter & MapDSN(0) -> State: " << TcpStateName[subflows[0]->state] << " -> Cancel reTxEvent!" << endl;
      subflows[0]->retxEvent.Cancel();
      return false;
    }

  // DCTCP: If we have received ECN echo in some of the received ACKs, halve the congestion window
  // TODO: Should we reduce the cwnd in the initial phase of MMPTCP??
  if (m_DCTCP && sFlowIdx < maxSubflows && m_eceBit > 0 && subflows[sFlowIdx]->state == ESTABLISHED
      && subflows[sFlowIdx]->dctcp_maxseq < (subflows[sFlowIdx]->highestAck + 1))
    {// @SendPendingData()
      if (m_slowDownEcnLike && sFlowIdx > 0 && !m_packetScatter)
        SlowDownEcnLike (sFlowIdx); // use ecn only after switching; otherwise use dctcp for initial subflow
      else
        SlowDown (sFlowIdx);
    }

  // This condition only valid when sendingBuffer is empty!
  if (sendingBuffer.Empty() && sFlowIdx < maxSubflows)
    {
      uint32_t whileCounter = 0;
      Ptr<MpTcpSubFlow> sF = subflows[sFlowIdx];
      if (sF->mapDSN.size() > 0 && sF->maxSeqNb > sF->TxSeqNumber - 1)
        { // SendingBuffer is empty but subflowBuffer (mapDSN) is not. Also subflow is recovering from timeOut.
          uint32_t window = std::min(AvailableWindow(sFlowIdx), sF->MSS);
          // Send all data packets in subflowBuffer (mapDSN) until subflow's available window is full.
          while (window != 0 && window >= sF->MSS && sF->maxSeqNb > sF->TxSeqNumber - 1 && sF->mapDSN.size() > 0)
            { // In case case more than one packet can be sent, if subflow's window allow
              whileCounter++;
              //cout <<"["<< m_node->GetId() <<"] MainBuffer is empty - subflowBuffer(" << sF->mapDSN.size()<< ") sFlow("<< (int)sFlowIdx << ") AvailableWindow: " << window << " CWND: " << sF->cwnd << " subflow is in timoutRecovery{" << (sF->mapDSN.size() > 0) << "} LoopIter: " << whileCounter << endl;
              int ret = SendDataPacket(sF->routeId, window, false);
              if (ret < 0)
                {
                  NS_LOG_UNCOND(this <<" [" << m_node->GetId() << "]("<< sF->routeId << ")" << " SendDataPacket return -1 -> Return false from SendPendingData()!?");
                  return false; // Return -1 from SendDataPacket means segment match has not find from subflow buffer, so this loop should be stopped and return!!
                }
              NS_ASSERT(ret == 0);
              window = std::min(AvailableWindow(sFlowIdx), sF->MSS);
            }
          return false;  // SendingBuffer is empty so no point to continue further on this function
        }
      else
        { // SendingBuffer & subflowBuffer are empty i.e, nothing to re-send and nothing to send!!
          NS_LOG_LOGIC(Simulator::Now().GetSeconds()<< " [" << m_node->GetId() << "]" << " SendPendingData -> SubflowBuffer and main buffer is empty -> Return!");
          return false; // SendingBuffer is empty so no point to continue further on this function
        }
    }

  // No endPoint -> Can't send any data
  if (m_endPoint == 0)
    {
      NS_LOG_ERROR ("["<< m_node->GetId() <<"] MpTcpSocketBase::SendPendingData:-> No endpoint");
      NS_ASSERT_MSG(m_endPoint != 0, " No endpoint");
      return false; // Is this the right way to handle this condition?
    }

  // Timeout recovery for PS phase after switching
  if (!m_packetScatter && sFlowIdx == 0 && subflows[0]->mapDSN.size() > 0 && subflows[0]->maxSeqNb > subflows[0]->TxSeqNumber - 1)
    {
      uint32_t whileCounter = 0;
      uint32_t window = std::min(AvailableWindow(sFlowIdx), subflows[0]->MSS);
      // Send all data packets in subflowBuffer (mapDSN) until subflow's available window is full or TxSeqNumber got bigger than MaxSeqNumber.
      while (window != 0 && window >= subflows[0]->MSS && subflows[0]->maxSeqNb > subflows[0]->TxSeqNumber - 1
          && subflows[0]->mapDSN.size() > 0)
        { // In case case more than one packet can be sent, if subflow's window allow
          whileCounter++;
          NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "]{" << flowId<< "} SubflowBuffer(" << subflows[0]->mapDSN.size()
              << ") sFlow(" << (int) sFlowIdx << ") AvailableWindow: " << window << " CWND: " << subflows[0]->cwnd
              << " subflow is in timoutRecovery{" << (subflows[0]->mapDSN.size() > 0) << "} LoopIter: " << whileCounter
              << " TxSeqNum: " << subflows[0]->TxSeqNumber << " MaxSeqNum: " << subflows[0]->maxSeqNb);
          int ret = SendDataPacket(subflows[0]->routeId, window, false);
          if (ret < 0)
            {
              exit(15);
              return false; // Return -1 from SendDataPacket means segment match has not find from subflow buffer, so this loop should be stopped and return!!
            }
          window = std::min(AvailableWindow(sFlowIdx), subflows[0]->MSS);
        }
      //return false;  // SendingBuffer is empty so no point to continue further on this function
    }

  uint32_t nOctetsSent = 0;
  Ptr<MpTcpSubFlow> sFlow;

  // PS: PS Mode
  if (m_packetScatter)
    {
      NS_LOG_INFO("SendPendingData() -> PacketScatter Mode!");
      //TODO Write a method to that there is only one ESTABLISHED sub-flow
      bool loop = true;
      while (!sendingBuffer.Empty() && loop)
        {
          uint8_t sFlowIdx = 0;
          uint32_t window = std::min(AvailableWindow(sFlowIdx), sendingBuffer.PendingData());
          if (window == 0)
            {
              loop = false;
              break;
            }
          sFlow = subflows[sFlowIdx];
          NS_LOG_INFO("["<< (int)sFlowIdx << "] Window("<< AvailableWindow(sFlowIdx) << ") DataInBuffer("<< sendingBuffer.PendingData()<< ")");
          if (sFlow->state == ESTABLISHED)
            {
              currentSublow = sFlow->routeId;
              uint32_t toSendSegSize = std::min(window, sFlow->MSS);  // Send no more than window
              // Sub-flow is in timeout process and it needs to retransmit an sent segment, that segment should be equal one MSS.
              // Since sendingBuffer is not empty so all segments sent so far should be equal to one MSS;
              // Only last segment can be less than one MSS!
              if (sFlow->maxSeqNb > sFlow->TxSeqNumber - 1 && sendingBuffer.PendingData() <= sFlow->MSS)
                toSendSegSize = sFlow->MSS;
              // Segment size to send is zero -> noting to send then, break the loop.
              if (toSendSegSize == 0)
                {
                  loop = false;
                  break;
                }
              int amountSent = SendDataPacket(sFlow->routeId, toSendSegSize, false);
              if (amountSent < 0)
                return false;
              else
                nOctetsSent += amountSent;  // Count total bytes sent in this loop
            } // end inner if clause
        } // end while loop
    } // end outer if clause
  else
    {
      NS_LOG_INFO("SendPendingData() -> MPTCP Mode!");
      // Send data as much as possible (it depends on subflows AvailableWindow and data in sending buffer)
      bool loop = true;
      while (!sendingBuffer.Empty() && loop)
        {
          uint32_t window = 0;
          // First if should be removed
          if (lastUsedsFlowIdx == 0 && subflows.size() == 1)
            {
              NS_LOG_UNCOND("MP-TCP mode & lastUsedsFlowIdx is 0 & only master sub-flow exist.");
              // Mode has been switched to MP-TCP, so it is not allowed to send further data packet via master sub-flow
              // Also slave sub-flows still not yet created. Thus noting to send now, return from this function!
              return false;
            }
          if (lastUsedsFlowIdx == 0 && subflows.size() > 1)
            {
              NS_LOG_UNCOND("MP-TCP mode & lastUsedsFlowIdx is 0 & slave sub-flows exist.");
              // Mode has been switched to MP-TCP, so it is not allowed to send further data packet via master sub-flow
              // Luckily at least one slave sub-flows has been created. Thus we should get proper sub-flow to use.
              lastUsedsFlowIdx = getSubflowToUse();
              NS_ASSERT(lastUsedsFlowIdx == 1);
            }

          // Search for a subflow with available windows
          for (uint32_t i = 1; i < subflows.size(); i++)
            {
              if (subflows[lastUsedsFlowIdx]->state != ESTABLISHED)
                continue;
              window = std::min(AvailableWindow(lastUsedsFlowIdx), sendingBuffer.PendingData()); // Get available window size
              if (window == 0)
                {  // No more available window in the current subflow, try with another one
                  NS_LOG_LOGIC("SendPendingData -> No window available on (" << (int)lastUsedsFlowIdx << ") Try next one!");
                  lastUsedsFlowIdx = getSubflowToUse();
                }
              else
                {
                  NS_LOG_LOGIC ("SendPendingData -> Find subflow with spare window PendingData (" << sendingBuffer.PendingData() << ") Available window ("<<AvailableWindow (lastUsedsFlowIdx)<<")");
                  break;
                }
            }

          if (window == 0)
            {
              loop = false;
              break;
            }

          // Take a pointer to the subflow with available window.
          sFlow = subflows[lastUsedsFlowIdx];

          // By this condition only connection initiator can send data need to be change though!
          if (sFlow->state == ESTABLISHED)
            {
              currentSublow = sFlow->routeId;
              uint32_t s = std::min(window, sFlow->MSS);  // Send no more than window
              if (sFlow->maxSeqNb > sFlow->TxSeqNumber - 1 && sendingBuffer.PendingData() <= sFlow->MSS)
                { // When subflow is in timeout recovery and the last segment is not reached yet then segment size should be equal to MSS
                  s = sFlow->MSS;
                }
              int amountSent = SendDataPacket(sFlow->routeId, s, false);
              if (amountSent < 0)
                {
                  NS_LOG_UNCOND(this <<" [" << m_node->GetId() << "]("<< sFlow->routeId << ")" << " SendDataPacket return -1 -> Return false from SendPendingData()!?");
                  return false;
                }
              else
                nOctetsSent += amountSent;  // Count total bytes sent in this loop
            } // end of if statement
          lastUsedsFlowIdx = getSubflowToUse();
        } // end of main while loop
    } // end of else clause
      //NS_LOG_UNCOND ("["<< m_node->GetId() << "] SendPendingData -> amount data sent = " << nOctetsSent << "... Notify application.");
  if (nOctetsSent > 0)
    {
      m_totalBytesSent += nOctetsSent;
      NotifyDataSent(GetTxAvailable());
    }
  return (nOctetsSent > 0);
}

// This function only called by SendPendingData() in our implementation!
int
MMpTcpSocketBase::SendDataPacket(uint8_t sFlowIdx, uint32_t size, bool withAck)
{
  NS_LOG_FUNCTION (this << (uint32_t)sFlowIdx << size << withAck);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  Ptr<Packet> p = 0;
  DSNMapping * ptrDSN = 0;
  uint32_t packetSize = size;
  bool guard = false;

  if (sFlow->maxSeqNb > sFlow->TxSeqNumber - 1)
    {
      uint32_t IterNumber = 0;
      for (list<DSNMapping *>::iterator it = sFlow->mapDSN.begin(); (it != sFlow->mapDSN.end() && guard == false); ++it)
        { // Look for match a segment from subflow's buffer where it is matched with TxSeqNumber
          IterNumber++;
          DSNMapping * ptr = *it;
          if (ptr->subflowSeqNumber == sFlow->TxSeqNumber)
            {
              ptrDSN = ptr;
              //p = Create<Packet>(ptrDSN->packet, ptrDSN->dataLevelLength);
              p = Create<Packet>(ptrDSN->dataLevelLength);
              packetSize = ptrDSN->dataLevelLength;
              guard = true;
              NS_LOG_LOGIC(Simulator::Now().GetSeconds() <<" A segment matched from subflow buffer. Its size is "<< packetSize << " IterNumInMapDSN: " << IterNumber <<" maxSeqNb: " << sFlow->maxSeqNb << " TxSeqNb: " << sFlow->TxSeqNumber << " FastRecovery: " << sFlow->m_inFastRec << " SegNb: " << ptrDSN->subflowSeqNumber); //
              break;
            }
        }
      if (p == 0)
        {
          NS_LOG_UNCOND("*** MaxSeq: "<< sFlow->maxSeqNb << " sFlow->TxSeq: " << sFlow->TxSeqNumber);
          NS_ASSERT_MSG(p != 0, "Subflow is in timeout recovery but there is no match segment in mapDSN - Return -1 ?");
          return -1;
        }
    }
  else
    {
      NS_ASSERT_MSG(sFlow->maxSeqNb == sFlow->TxSeqNumber -1,
          " maxSN: " << sFlow->maxSeqNb << " TxSeqNb-1" << sFlow->TxSeqNumber -1);
    }

  // If no packet has made yet and maxSeqNb is equal to TxSeqNumber -1, then we can safely create a packet from connection buffer (sendingBuffer).
  if (p == 0 && ptrDSN == 0)
    {
      NS_ASSERT(!guard);
      NS_ASSERT(sFlow->maxSeqNb == sFlow->TxSeqNumber -1);
      p = sendingBuffer.CreatePacket(size);
      if (p == 0)
        { // TODO I guess we should not return from here - What do we do then kill ourself?
          NS_LOG_WARN("["<< m_node->GetId() << "] ("<< sFlow->routeId << ") No data is available in SendingBuffer to create a pkt from it! SendingBufferSize: " << sendingBuffer.PendingData());
          NS_ASSERT_MSG(p != 0, "No data is available in SendingBuffer to create a pkt from it!");
          return 0;
        }
    }
  NS_ASSERT(packetSize <= size);
  NS_ASSERT(packetSize == p->GetSize());

  // @SendDataPacket -> Add ECT Tag on every data packet... We do this for ack packets as well
  if (m_DCTCP || AlgoCC == XMP || m_ecn)
    AddPacketTag(p, ECT_TAG); //AddEctTag(p);
  // @SendDataPacket
  if (m_disjoinPath && flowType.compare ("Large") == 0)
    AddEcmpTag(p, sFlow->routeId);

  // This is data packet, so its TCP_Flag should be 0
  uint8_t flags = withAck ? TcpHeader::ACK : 0;

  // Add MPTCP header to the packet
  TcpHeader header;
  header.SetFlags(flags);
  header.SetSequenceNumber(SequenceNumber32(sFlow->TxSeqNumber));
  header.SetAckNumber(SequenceNumber32(sFlow->RxSeqNumber));

  // PS: Add random source port
  if (m_shortFlowTCP && sFlow->routeId == 0 && flowType.compare("Short") == 0)
    {
      header.SetSourcePort(sFlow->sPort);
    }
  else if (sFlow->routeId == 0)
    {
      header.SetSourcePort((rand() % 65530) + 1); // TODO change this to uniform random variable
    }
  else
    header.SetSourcePort(sFlow->sPort);
  //
  header.SetDestinationPort(sFlow->dPort);
  header.SetWindowSize(AdvertisedWindowSize());
  if (!guard)
    { // If packet is made from sendingBuffer, then we got to add the packet and its info to subflow's mapDSN.
      sFlow->AddDSNMapping(sFlowIdx, nextTxSequence, packetSize, sFlow->TxSeqNumber, sFlow->RxSeqNumber/*, p->Copy()*/);
    }

  // PS: PACKET-SCATTER MODE
  if (sFlow->routeId == 0)
    {
      if (!guard)
        { // if packet is made from sendingBuffer, then we use nextTxSequence to OptDSN
          //NS_ASSERT_MSG(remoteToken != 0,  "["<< m_node->GetId()<< "] RemoteToken: " << remoteToken << " sFlow's state: " << TcpStateName[sFlow->state] << " nextTxSeq: " << nextTxSequence);
          if (remoteToken == 0)
            {
              NS_LOG_UNCOND("*** ["<< m_node->GetId()<< "] RemoteToken: " << remoteToken << " sFlow's state: " << TcpStateName[sFlow->state] << " nextTxSeq: " << nextTxSequence << " SendRST <->  sAddr: " << sFlow->sAddr << " dAddr: " << sFlow->dAddr << " sPort: " << sFlow->sPort << " dPort: " << sFlow->dPort);
              sendingBuffer.ClearBuffer();
              cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") {"<< flowId <<"} SendRST(SendDataPacket)" << endl;
              SendRST(sFlowIdx);
              CloseAndNotifyAllSubflows();
            }
          else
            header.AddOptDSN(OPT_DSN, nextTxSequence, packetSize, sFlow->TxSeqNumber, remoteToken, 1);
        }
      else
        { // if packet is made from subflow's Buffer (already sent packets), that packet's dataSeqNumber should be added here!
          NS_ASSERT_MSG(remoteToken != 0, " RemoteToken: " << remoteToken);
          header.AddOptDSN(OPT_DSN, ptrDSN->dataSeqNumber, (uint16_t) packetSize, sFlow->TxSeqNumber, remoteToken, 1);
          NS_ASSERT(packetSize == ptrDSN->dataLevelLength);
        }
    }
  // MPTCP MODE
  else
    {
      if (!guard)
        { // if packet is made from sendingBuffer, then we use nextTxSequence to OptDSN
          header.AddOptDSN(OPT_DSN, nextTxSequence, packetSize, sFlow->TxSeqNumber);
        }
      else
        { // if packet is made from subflow's Buffer (already sent packets), that packet's dataSeqNumber should be added here!
          header.AddOptDSN(OPT_DSN, ptrDSN->dataSeqNumber, (uint16_t) packetSize, sFlow->TxSeqNumber);
          NS_ASSERT(packetSize == ptrDSN->dataLevelLength);
        }
    }

  uint8_t hlen = 5;   // 5 --> 32-bit words = 20 Bytes == TcpHeader Size with out any option
  //uint8_t olen = 15;  // 15 because packet size is 2 bytes in size. 1 + 8 + 2+ 4 = 15
  uint8_t olen = 20;
  uint8_t plen = 0;
  plen = (4 - (olen % 4)) % 4; // (4 - (15 % 4)) 4 => 1
  olen = (olen + plen) / 4;    // (15 + 1) / 4 = 4
  hlen += olen;
  header.SetLength(hlen);
  header.SetOptionsLength(olen);
  header.SetPaddingLength(plen);

  NS_LOG_ERROR("hLen: " << (int)hlen << " oLen: " << (int)olen << " pLen: " << (int)plen);

  // Check RTO, if expired then reschedule it again.
  SetReTxTimeout(sFlowIdx);
  NS_LOG_LOGIC ("Send packet via TcpL4Protocol with flags 0x" << std::hex << static_cast<uint32_t> (flags) << std::dec);

  // simulating loss of acknowledgement in the sender side
  // calculateTotalCWND();

  Ptr<NetDevice> netDevice = FindOutputNetDevice(sFlow->sAddr);
  m_tcp->SendPacket(p, header, sFlow->sAddr, sFlow->dAddr, netDevice);
  if (!guard)
    sFlow->PktCount++;

#ifdef PLOT
  uint32_t tmp = (((sFlow->TxSeqNumber + packetSize) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->DATA.push_back(make_pair(Simulator::Now().GetSeconds(), tmp));
#endif

   NS_LOG_LOGIC(Simulator::Now().GetSeconds() << " ["<< m_node->GetId()<< "] SendDataPacket->  " << header <<" dSize: " << packetSize<< " sFlow: " << sFlow->routeId);

  // Do some updates.....
  sFlow->rtt->SentSeq(SequenceNumber32(sFlow->TxSeqNumber), packetSize); // Notify the RTT of a data packet sent
  sFlow->TxSeqNumber += packetSize; // Update subflow's nextSeqNum to send.
  sFlow->maxSeqNb = std::max(sFlow->maxSeqNb, sFlow->TxSeqNumber - 1);
  sFlow->m_highTxMark = std::max (sFlow->m_highTxMark, sFlow->TxSeqNumber - 1); // cwnd blowup
  if (!guard)
    {
      nextTxSequence += packetSize;  // Update connection sequence number
      //TxBytes += packetSize + 20 + 20 + 20 + 2;
    }
  //NS_LOG_UNCOND( "("<< (int) sFlowIdx<< ") DataPacket -----> " << header << "  " << m_localAddress << ":" << m_localPort<< "->" << m_remoteAddress << ":" << m_remotePort);

  // After data packet has been sent now look at remianing data in sending buffer
  uint32_t remainingData = sendingBuffer.PendingData();
  if (m_closeOnEmpty && (remainingData == 0))
    {
      SendAllSubflowsFIN();
    }

  if (guard)
    return 0;
  else
    return packetSize;
}

Ptr<TcpSocketBase>
MMpTcpSocketBase::Fork(void)
{
  NS_LOG_FUNCTION_NOARGS();
  return CopyObject<MMpTcpSocketBase>(this);
}

void
MMpTcpSocketBase::DoRetransmit(uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // Retransmit SYN packet
  if (sFlow->state == SYN_SENT)
    {
//      if (sFlow->cnCount > 0)
//        {
      //SendEmptyPacket(sFlowIdx, TcpHeader::SYN);
//        }
//      else
//        {
//          NotifyConnectionFailed();
//        }
      return;
    }

  // Retransmit non-data packet: Only if in FIN_WAIT_1 or CLOSING state
  if (sendingBuffer.Empty() && sFlow->mapDSN.size() == 0)
    {
      if (sFlow->state == FIN_WAIT_1 || sFlow->state == CLOSING)
        { // Must have lost FIN, re-send
          NS_LOG_INFO("DoRetransmit -> Resent FIN... TxSeqNumber: " << sFlow->TxSeqNumber);
          SendEmptyPacket(sFlowIdx, TcpHeader::FIN);
        }
      return;
    }

  DSNMapping* ptrDSN = sFlow->GetunAckPkt();
  if (ptrDSN == 0)
    {
      NS_LOG_INFO ("Retransmit -> no Unacked data !! mapDSN size is "<< sFlow->mapDSN.size() << " max Ack seq n�� "<< sFlow->highestAck << " (" << (int)sFlowIdx<< ")");
      NS_ASSERT(3!=3);
      return;
    }

  NS_ASSERT(ptrDSN->subflowSeqNumber == sFlow->highestAck +1);

  // we retransmit only one lost pkt
  //Ptr<Packet> pkt = Create<Packet>(ptrDSN->packet, ptrDSN->dataLevelLength);
  Ptr<Packet> pkt = Create<Packet>(ptrDSN->dataLevelLength);
  TcpHeader header;
  if (m_shortFlowTCP && sFlow->routeId == 0 && flowType.compare("Short") == 0)
    {
      header.SetSourcePort(sFlow->sPort);
    }
  else if (sFlowIdx == 0)
    header.SetSourcePort((rand() % 65530)+1);
  else
    header.SetSourcePort(sFlow->sPort);
  //
  header.SetDestinationPort(sFlow->dPort);
  header.SetFlags(TcpHeader::NONE);  // Change to NONE Flag
  header.SetSequenceNumber(SequenceNumber32(ptrDSN->subflowSeqNumber));
  header.SetAckNumber(SequenceNumber32(sFlow->RxSeqNumber));  // for the acknowledgment, we ACK the sFlow last received data
  header.SetWindowSize(AdvertisedWindowSize());

  // PS: Add remote token and packetScatter flag to true
  if (sFlow->routeId == 0)
    {
      NS_ASSERT_MSG(remoteToken != 0, " RemoteToken: " << remoteToken);
      header.AddOptDSN(OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber, remoteToken, 1);
    }
  else
    header.AddOptDSN(OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber);

  uint8_t hlen = 5;
  uint8_t olen = 20; //uint8_t olen = 15;
  uint8_t plen = 0;
  plen = (4 - (olen % 4)) % 4;
  olen = (olen + plen) / 4;
  hlen += olen;
  header.SetLength(hlen);
  header.SetOptionsLength(olen);
  header.SetPaddingLength(plen);

  // @DoRetransmit(uint8_t sFlowIdx) -> Add ECT Tag on every data packet... We do this for ack packets as well
  if (m_DCTCP || AlgoCC == XMP || m_ecn)
    AddPacketTag(pkt, ECT_TAG);
  // @DoRetransmit
  if (m_disjoinPath && flowType.compare ("Large") == 0)
    AddEcmpTag (pkt, sFlow->routeId);

  m_tcp->SendPacket(pkt, header, sFlow->sAddr, sFlow->dAddr, FindOutputNetDevice(sFlow->sAddr));

  //reset RTO
  SetReTxTimeout(sFlowIdx);

#ifdef PLOT
  uint32_t tmp = (((ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->RETRANSMIT.push_back(make_pair(Simulator::Now().GetSeconds(), tmp));
  if (!sFlow->m_inFastRec)
    {
      timeOutTrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->cwnd));
    }
#endif



  // Update Rtt
  sFlow->rtt->SentSeq(SequenceNumber32(ptrDSN->subflowSeqNumber), ptrDSN->dataLevelLength);

  // In case of RTO, advance m_nextTxSequence
  sFlow->TxSeqNumber = std::max(sFlow->TxSeqNumber, ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength);
  sFlow->maxSeqNb = std::max(sFlow->maxSeqNb, sFlow->TxSeqNumber - 1);
  sFlow->m_highTxMark = std::max (sFlow->m_highTxMark, sFlow->TxSeqNumber - 1);
}

void
MMpTcpSocketBase::DoRetransmit(uint8_t sFlowIdx, DSNMapping* ptrDSN)
{
  NS_LOG_FUNCTION(this);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // This retransmit segment should be the lost segment.
  NS_ASSERT(ptrDSN->subflowSeqNumber >= sFlow->highestAck +1);

  SetReTxTimeout(sFlowIdx); // reset RTO

  // we retransmit only one lost pkt
  //Ptr<Packet> pkt = Create<Packet>(ptrDSN->packet, ptrDSN->dataLevelLength);
  Ptr<Packet> pkt = Create<Packet>(ptrDSN->dataLevelLength);
  if (pkt == 0)
    NS_ASSERT(3!=3);

  TcpHeader header;
  if (m_shortFlowTCP && sFlow->routeId == 0 && flowType.compare("Short") == 0)
    {
      header.SetSourcePort(sFlow->sPort);
    }
  else if (sFlowIdx == 0)
    header.SetSourcePort((rand() % 65530)+1);
  else
    header.SetSourcePort(sFlow->sPort);
  //
  header.SetDestinationPort(sFlow->dPort);
  header.SetFlags(TcpHeader::NONE);  // Change to NONE Flag
  header.SetSequenceNumber(SequenceNumber32(ptrDSN->subflowSeqNumber));
  header.SetAckNumber(SequenceNumber32(sFlow->RxSeqNumber));
  header.SetWindowSize(AdvertisedWindowSize());
  // Make sure info here comes from ptrDSN...

  // Add remote token and packetScatter flag to true  // PS: Add
  if (sFlow->routeId == 0)
    {
      NS_ASSERT_MSG(remoteToken != 0, " RemoteToken: " << remoteToken);
      header.AddOptDSN(OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber, remoteToken, 1);
    }
  else
    header.AddOptDSN(OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber);

  NS_LOG_WARN (Simulator::Now().GetSeconds() <<" RetransmitSegment -> "<< " localToken "<< localToken<<" Subflow "<<(int) sFlowIdx<<" DataSeq "<< ptrDSN->dataSeqNumber <<" SubflowSeq " << ptrDSN->subflowSeqNumber <<" dataLength " << ptrDSN->dataLevelLength << " packet size " << pkt->GetSize() << " 3DupACK");
  uint8_t hlen = 5;
  uint8_t olen = 20; //uint8_t olen = 15;
  uint8_t plen = 0;
  plen = (4 - (olen % 4)) % 4;
  olen = (olen + plen) / 4;
  hlen += olen;
  header.SetLength(hlen);
  header.SetOptionsLength(olen);
  header.SetPaddingLength(plen);

  // @DoRetransmit((uint8_t sFlowIdx, DSNMapping* ptrDSN) -> Add ECT Tag on every data packet... We do this for ack packets as well
  if (m_DCTCP || AlgoCC == XMP || m_ecn)
    AddPacketTag(pkt, ECT_TAG);
  // @DoRetransmit
  if (m_disjoinPath && flowType.compare ("Large") == 0)
    AddEcmpTag(pkt, sFlow->routeId);

  // Send Segment to lower layer
  m_tcp->SendPacket(pkt, header, sFlow->sAddr, sFlow->dAddr, FindOutputNetDevice(sFlow->sAddr));

  //Plotting
#ifdef PLOT
  uint32_t tmp = (((ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->RETRANSMIT.push_back(make_pair(Simulator::Now().GetSeconds(), tmp));
#endif

  // Notify RTT
  sFlow->rtt->SentSeq(SequenceNumber32(ptrDSN->subflowSeqNumber), ptrDSN->dataLevelLength);

  // In case of RTO, advance m_nextTxSequence
  sFlow->TxSeqNumber = std::max(sFlow->TxSeqNumber, ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength);

  // highest sent sequence number should be updated!
  sFlow->maxSeqNb = std::max(sFlow->maxSeqNb, sFlow->TxSeqNumber - 1);
  sFlow->m_highTxMark = std::max (sFlow->m_highTxMark, sFlow->TxSeqNumber - 1); // cwnd blowup

  NS_LOG_INFO("("<<(int) sFlowIdx << ") DoRetransmit -> " << header);
}

/*
 * When dupAckCount reach to the default value of 3 then TCP goes to ack recovery process.
 */
void
MMpTcpSocketBase::DupAck(uint8_t sFlowIdx, DSNMapping* ptrDSN)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  sFlow->m_dupAckCount++;
  ptrDSN->dupAckCount++; // Used for evaluation purposes only
  uint32_t segmentSize = sFlow->MSS;
  //calculateTotalCWND();
#ifdef PLOT
  uint32_t tmp = (((ptrDSN->subflowSeqNumber) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->DUPACK.push_back(make_pair(Simulator::Now().GetSeconds(), tmp));
#endif

  // PS: Tuning DupAckThresh dynamically based on flight packets
//  if (m_isThinStream && sFlowIdx == 0 && !sFlow->m_inFastRec && sFlow->m_dupAckCount < sFlow->m_retxThresh)
//    {
//      uint32_t dupAckThresh = (BytesInFlight(sFlowIdx) / sFlow->MSS) / 2;
//      sFlow->m_retxThresh = dupAckThresh;
//    }

  // congestion control algorithms
  if (sFlow->m_dupAckCount == sFlow->m_retxThresh && !sFlow->m_inFastRec)
    { // FastRetrasmsion
      NS_LOG_WARN (Simulator::Now().GetSeconds() <<" DupAck -> subflow ("<< (int)sFlowIdx <<") 3rd duplicated ACK for segment ("<<ptrDSN->subflowSeqNumber<<")");
      uint32_t oldCwnd = sFlow->cwnd.Get();

      if (m_isThinStream && sFlowIdx == 0)
        Retransmit(sFlowIdx);               // Go to TCP tahoe like loss recovery
      else if (m_mmptcpv3 && m_DCTCP && sFlowIdx == 0 && m_packetScatter)
        { // We do not cut cwnd in half; instead slowing down based on DCTCP-CC
          string sockName = this->GetTypeId ().GetName();
          SlowDownFastReTx(sFlowIdx, ptrDSN, sockName);
        }
      else
        {
          ReduceCWND(sFlowIdx, ptrDSN);       // Cut the window to the half
        }

      // Record dctcp stats for initial subflow only at any setting (mmptcpv3 or normal)
      if (m_dctcpFastReTxRecord && sFlowIdx == 0 && m_packetScatter)
        {
          RecordDctcpFastRetx(sFlowIdx, oldCwnd);
        }

      // PS: Initiate MPTCP subflows - preparing for switching
      if (m_switchingMode == CwndSignal && m_packetScatter && m_subflowInitiation && maxSubflows > 1)
        {
          cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") FlowType(" <<flowType << ") cwnd[" << sFlow->cwnd<< "] SwitchMode(" << "CwndSignal" <<")"<< endl;
          NS_ASSERT(sFlow->routeId == 0);
          m_subflowInitiation = false;
          MpTcpSocketBase::InitiateMultipleSubflows();
          //m_packetScatter = false;
        }
      // Plotting
#ifdef PLOT
      sFlow->_FReTx.push_back(make_pair(Simulator::Now().GetSeconds(), TimeScale));
#endif
      FastReTxs++;
    }
  else if (sFlow->m_inFastRec)
    { // Fast Recovery
      // Increase cwnd for every additional DupACK (RFC2582, sec.3 bullet #3)
      sFlow->cwnd += segmentSize;
      sFlow->m_duplicatesSize += m_segmentSize; // cwnd blowup
      // Plotting
#ifdef PLOT
      DupAcks.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->cwnd));
      sFlow->ssthreshtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->ssthresh));
#endif
      NS_LOG_WARN ("DupAck-> FastRecovery. Increase cwnd by one MSS, from " << sFlow->cwnd.Get() <<" -> " << sFlow->cwnd << " AvailableWindow: " << AvailableWindow(sFlowIdx));
      FastRecoveries++;
      // Send more data into pipe if possible to get ACK clock going
      SendPendingData(sFlow->routeId);
    }
  else if (!sFlow->m_inFastRec && sFlow->m_limitedTx && sendingBuffer.PendingData() > 0)
    { // RFC3042 Limited transmit: Send a new packet for each duplicated ACK before fast retransmit
      NS_LOG_INFO ("Limited transmit");
      // cond1: in timeout recovery && 0 < pending data >  mss  ==>  pktsize = mss
      // cond2: in timeout recovery && 0 < pending data <= mss  ==>  pktsize = mss
      // cond3: not in timeout recovery                         ==>  pktsize = min(mss, pendingData)
      /*
      if (sFlow->m_limitedTxCount % 2 == 0)
          return;
      else
        sFlow->m_limitedTxCount++;
      */
      uint32_t pktSize = std::min(sFlow->MSS, sendingBuffer.PendingData());
      if (sFlow->maxSeqNb > sFlow->TxSeqNumber - 1 && sendingBuffer.PendingData() <= sFlow->MSS)
        { // When subflow is in timeout recovery and the last segment is not reached yet then segment size should be equal to MSS
          pktSize = sFlow->MSS;
        }
      int amountSent = SendDataPacket(sFlow->routeId, pktSize, false);
      if (amountSent < 0)
        {
          NS_LOG_UNCOND(this <<" [" << m_node->GetId() << "]("<< sFlow->routeId << ")" << " SendDataPacket return -1 -> Return false from SendPendingData()!?");
        }
      else if (amountSent > 0)
        NotifyDataSent(GetTxAvailable());
      else
        {
          NS_LOG_UNCOND(this <<" [" << m_node->GetId() << "]("<< sFlow->routeId << ")" << " SendDataPacket return 0 -> Subflow recovering from timeout so it send data from subflow buffer");
        }

    }
}

void
MMpTcpSocketBase::OpenCWND(uint8_t sFlowIdx, uint32_t ackedBytes)
{
  NS_LOG_FUNCTION(this << (int) sFlowIdx << ackedBytes);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  double adder = 0;
  uint32_t cwnd = sFlow->cwnd.Get();
  uint32_t ssthresh = sFlow->ssthresh;
  uint32_t mss = sFlow->MSS;

  // params used only for COUPLED_INC and COUPLED_EPS only
  int tcp_inc = 0, tt = 0;
  int tmp, total_cwnd, tmp2;
  double tmp_float;

  if (AlgoCC >= COUPLED_SCALABLE_TCP)
    {
      if (ackedBytes > mss)
        ackedBytes = mss;
      if (ackedBytes < 0)
        {
          exit(200);
          return;
        }
      tcp_inc = (ackedBytes * mss) / cwnd;
      tt = (ackedBytes * mss) % cwnd;
      if (m_alphaPerAck)
        {
          a = compute_a_scaled(); // Per ACK for COUPLED_INC
          alpha = compute_alfa(); // Per ACK for COUPLED_EPSILON
        }
    }

  calculateTotalCWND();
  if (cwnd < ssthresh)
    { // Slow Start phase
      sFlow->cwnd += sFlow->MSS;
#ifdef PLOT
      sFlow->ssthreshtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->ssthresh));
      sFlow->CWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->cwnd));
      totalCWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), totalCwnd));
      sFlow->_ss.push_back(make_pair(Simulator::Now().GetSeconds(), TimeScale));
#endif
      NS_LOG_WARN ("Congestion Control (Slow Start) increment by one segmentSize");
    }
  else if (sFlow->routeId == 0)
    { // PS: MMPTCP uses TCP congestion control in its initial subflow (pScatter phase) for congestion avoidance.
      adder = static_cast<double>(sFlow->MSS * sFlow->MSS) / cwnd;
      adder = std::max(1.0, adder);
      sFlow->cwnd += static_cast<double>(adder);
      NS_LOG_LOGIC ("Subflow "<<(int)sFlowIdx<<" pScatter Congestion Control (Uncoupled_TCPs) increment is "<<adder<<" ssthresh "<< ssthresh << " cwnd "<<cwnd);
      return;
    }
  else
    {
      switch (AlgoCC)
        {
      case RTT_Compensator:
        calculateAlpha(); // Calculate alpha per drop or RTT...RFC 6356 (Section 4.1)
        adder = std::min(alpha * sFlow->MSS * sFlow->MSS / totalCwnd, static_cast<double>(sFlow->MSS * sFlow->MSS) / cwnd);
        adder = std::max(1.0, adder);
        sFlow->cwnd += static_cast<double>(adder);
#ifdef PLOT
        sFlow->ssthreshtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->ssthresh));
        sFlow->CWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->cwnd));
        totalCWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), totalCwnd));
#endif
        NS_LOG_ERROR ("Congestion Control (RTT_Compensator): alpha "<<alpha<<" ackedBytes (" << ackedBytes << ") totalCwnd ("<< totalCwnd / sFlow->MSS<<" packets) -> increment is "<<adder << " cwnd: " << sFlow->cwnd);
        break;
      case Linked_Increases:
        calculateAlpha();
        adder = alpha * sFlow->MSS * sFlow->MSS / totalCwnd;
        adder = std::max(1.0, adder);
        sFlow->cwnd += static_cast<double>(adder);
#ifdef PLOT
        sFlow->ssthreshtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->ssthresh));
        sFlow->CWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->cwnd));
        totalCWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), totalCwnd));
#endif
        NS_LOG_ERROR ("Subflow "<<(int)sFlowIdx<<" Congestion Control (Linked_Increases): alpha "<<alpha<<" increment is "<<adder<<" ssthresh "<< ssthresh << " cwnd "<<cwnd );
        break;
      case Uncoupled_TCPs:
        adder = static_cast<double>(sFlow->MSS * sFlow->MSS) / cwnd;
        adder = std::max(1.0, adder);
        sFlow->cwnd += static_cast<double>(adder);
#ifdef PLOT
        sFlow->ssthreshtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->ssthresh));
        sFlow->CWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->cwnd));
        totalCWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), totalCwnd));
        NS_LOG_WARN ("Subflow "<<(int)sFlowIdx<<" Congestion Control (Uncoupled_TCPs) increment is "<<adder<<" ssthresh "<< ssthresh << " cwnd "<<cwnd);
#endif
        break;
      case UNCOUPLED:
        sFlow->cwnd += tcp_inc;
        break;
      case Fully_Coupled:
        adder = static_cast<double>(sFlow->MSS * sFlow->MSS) / totalCwnd;
        adder = std::max(1.0, adder);
        sFlow->cwnd += static_cast<double>(adder);
#ifdef PLOT
        sFlow->ssthreshtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->ssthresh));
        sFlow->CWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->cwnd));
        totalCWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), totalCwnd));
#endif
        NS_LOG_ERROR ("Subflow "<<(int)sFlowIdx<<" Congestion Control (Fully_Coupled) increment is "<<adder<<" ssthresh "<< ssthresh << " cwnd "<<cwnd);
        break;

      case COUPLED_INC:
        total_cwnd = compute_total_window();
        tmp2 = (ackedBytes * mss * a) / total_cwnd;

        tmp = tmp2 / A_SCALE;

        if (tmp < 0)
          {
            printf("Negative increase!");
            tmp = 0;
          }

        if (rand() % A_SCALE < tmp2 % A_SCALE)
          tmp++;

        if (tmp > tcp_inc)    //capping
          tmp = tcp_inc;

        if ((cwnd + tmp) / mss != cwnd / mss)
          a = compute_a_scaled();

        sFlow->cwnd = cwnd + tmp;
        break;

      case COUPLED_EPSILON: // RTT_Compensator
        total_cwnd = compute_total_window();
        tmp_float = ((double) ackedBytes * mss * alpha * pow(alpha * cwnd, 1 - _e)) / pow(total_cwnd, 2 - _e);
        tmp = (int) floor(tmp_float);

        if (drand() < tmp_float - tmp)
          tmp++;

        if (tmp > tcp_inc)    //capping
          tmp = tcp_inc;

        if ((cwnd + tmp) / mss != cwnd / mss)
          {
            if (_e > 0 && _e < 2)
              alpha = compute_alfa();
          }

        sFlow->cwnd = cwnd + tmp;
        break;

      case COUPLED_SCALABLE_TCP:
        sFlow->cwnd = cwnd + ackedBytes * 0.01;
        break;

      case COUPLED_FULLY:
          total_cwnd = compute_total_window();
          tt = (int) (ackedBytes * mss * A);
          tmp = tt / total_cwnd;
          if (tmp > tcp_inc)
            tmp = tcp_inc;
          sFlow->cwnd = cwnd + tmp;
          break;
      default:
        NS_ASSERT(3!=3);
        break;
        }
      //Plotting
#ifdef PLOT
      sFlow->_ca.push_back(make_pair(Simulator::Now().GetSeconds(), TimeScale));
#endif
    }

}

/** Retransmit timeout */
void
MMpTcpSocketBase::Retransmit(uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this);  //
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  // Exit From Fast Recovery
//  sFlow->m_inFastRec = false; //cwnd blowup
  // According to RFC2581 sec.3.1, upon RTO, ssthresh is set to half of flight
  // size and cwnd is set to 1*MSS, then the lost packet is retransmitted and
  // TCP back to slow start
  if (m_isThinStream && sFlowIdx == 0)
    {}
  else
    { // cwnd blowup
      if(!sFlow->m_inFastRec)
        {
          // According to RFC2581 sec.3.1, upon RTO, ssthresh is set to half of flight
          // size and cwnd is set to 1*MSS, then the lost packet is retransmitted and
          // TCP back to slow start
          sFlow->ssthresh = std::max(2 * sFlow->MSS, BytesInFlight(sFlowIdx) / 2);
        }
      else
        {
          //If already in fast recovery, halve ssThresh again
          sFlow->ssthresh = sFlow->ssthresh / 2;
          sFlow->m_duplicatesSize = 0;
        }
      sFlow->m_inFastRec = false;
    }

  if (m_isThinStream && sFlowIdx == 0)
    sFlow->cwnd = sFlow->MSS * 2;
  else
    sFlow->cwnd = sFlow->MSS; //  sFlow->cwnd = 1.0;

  sFlow->TxSeqNumber = sFlow->highestAck + 1; // m_nextTxSequence = m_txBuffer.HeadSequence(); // Restart from highest Ack
  sFlow->m_highTxMark = sFlow->TxSeqNumber - 1; //m_highTxMark = m_nextTxSequence - m_segmentSize; //cwnd blowup

  // DCTCP update during Timeout
  if (m_DCTCP || m_dctcpFastReTxRecord)
    { // Determine the next observation window for updating dctcp's alpha
      sFlow->dctcp_alpha_update_seq = sFlow->TxSeqNumber;
      sFlow->dctcp_maxseq = sFlow->dctcp_alpha_update_seq;
    }

  //if (m_isThinStream && sFlowIdx == 0)
  //  {}
  //else
  sFlow->rtt->IncreaseMultiplier();  // Double the next RTO

  if (AlgoCC >= COUPLED_EPSILON)
    window_changed();

  //
  DoRetransmit(sFlowIdx);  // Retransmit the packet
#ifdef PLOT
  sFlow->_TimeOut.push_back(make_pair(Simulator::Now().GetSeconds(), TimeScale));
#endif
  TimeOuts++;
  // rfc 3782 - Recovering from timeOut
  //sFlow->m_recover = SequenceNumber32(sFlow->maxSeqNb + 1);
}


void
MMpTcpSocketBase::SetSwitchingMode(SwitchingMode_t mode)
{
  NS_LOG_FUNCTION(this);
  m_switchingMode = mode;
}

void
MMpTcpSocketBase::SetFlowSizeThresh(uint32_t flowSizeThresh)
{
  NS_LOG_FUNCTION(this);
  m_flowSizeThresh = flowSizeThresh;
}

void
MMpTcpSocketBase::calculateTotalCWND()
{
  totalCwnd = 0;
  for (uint32_t i = 0; i < subflows.size(); i++)
    {
      // PS: bypass pScatter subflow if in MPTCP mode
      if (m_packetScatter == false && i == 0)
        continue;
      // PS: make sure if in MPTCP mode then pScatter subflow is not being part of calculation of total window
      if (m_packetScatter == false)
        NS_ASSERT(subflows[i]->routeId != 0);

      if (subflows[i]->m_inFastRec)
        totalCwnd += subflows[i]->ssthresh;
      else
        totalCwnd += subflows[i]->cwnd.Get();

      // PS: break if in pScatter mode!
      if (m_packetScatter)
        {
          NS_ASSERT(subflows[i]->routeId == 0);
          break;
        }
    }
}

/*
 * This method is called whenever a congestion happen in order to regulate the agressivety of subflows
 * alpha = cwnd_total * MAX(cwnd_i / rtt_i^2) / {SUM(cwnd_i / rtt_i))^2}   //RFC 6356 formula (2)
 */
void
MMpTcpSocketBase::calculateAlpha()
{
  NS_LOG_FUNCTION_NOARGS ();
  alpha = 0;
  double maxi = 0;
  double sumi = 0;
  for (uint32_t i = 0; i < subflows.size(); i++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[i];

      // PS: bypass pScatter subflow if in MPTCP mode
      if (m_packetScatter == false && i == 0)
        continue;
      // PS: make sure if in MPTCP mode then pScatter subflow is not be considered
      if (m_packetScatter == false)
        NS_ASSERT(sFlow->routeId != 0);

      Time time = sFlow->rtt->GetCurrentEstimate();
      double rtt = time.GetSeconds();
      double tmpi = sFlow->cwnd.Get() / (rtt * rtt);
      if (maxi < tmpi)
        maxi = tmpi;

      sumi += sFlow->cwnd.Get() / rtt;

      // PS: break if in pScatter mode!
      if (m_packetScatter)
        {
          NS_ASSERT(sFlow->routeId == 0);
          break;
        }
    }
  alpha = (totalCwnd * maxi) / (sumi * sumi);
}

void
MMpTcpSocketBase::SetDupAckThresh(uint32_t dupack){
  m_dupAckThresh = dupack;
}

void
MMpTcpSocketBase::DoGenerateOutPutFile()
{
  TypeId tid = this->GetTypeId();
  DoDoGenerateOutPutFile(tid);
/*
  goodput = ((nextTxSequence * 8) / (Simulator::Now().GetSeconds() - fLowStartTime));
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(outputFileName, std::ios::out | std::ios::app);
  ostream* os = stream->GetStream();
  *os << "[:" << m_node->GetId()
      << ":][+" << flowId
      << "+][=" << flowType
      << "=][#" << goodput / 1000000
      << "#][*" << (Simulator::Now().GetSeconds() - fLowStartTime)
      << "*][$" << TimeOuts
      << "$][!" << FastReTxs
      << "!][(" << pAck
      << ")][@" << FullAcks
      << "@][^" << FastRecoveries
      << "^][&" << subflows.size()
      << "&][_" << GetEstSubflows()
      << "_][%" << tid.GetName()
      << "%][/" << subflows[(uint32_t) currentSublow]->lastMeasuredRtt.GetSeconds()
      << "/][~" << subflows[0]->m_retxThresh
      << "~][�" << subflows[0]->rtt->GetMinRto().GetMilliSeconds()
      << "�][{" << subflows[0]->m_limitedTx
      << "}][-" << m_isThinStream
      << "-][|" << m_isRTObackoff
      << "|]" << endl;
*/
  NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " ["<< m_node->GetId()<< "] Goodput -> " << goodput / 1000000 << " Mbps");
}

string
MMpTcpSocketBase::GetTypeIdName()
{
  string tmp = this->GetTypeId().GetName();
  if (tmp.compare("ns3::MMpTcpSocketBase") == 0)
    return "MMPTCP";
  else
    return "UnKnown";
}

bool
MMpTcpSocketBase::IsCwndExceedThresh(uint8_t sFlowIdx)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint32_t rate = (sFlow->cwnd.Get() * 8) / (sFlow->rtt->GetCurrentEstimate().GetSeconds());
  uint64_t bandwidth = GetPathBandwidth(0);
  if (rate >= (bandwidth / 50))
    {
      cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") FlowType(" <<flowType << ") cwnd[" << sFlow->cwnd<< "] CwndRate: " << rate << " Interface-Bandwidth: " << bandwidth <<endl;
      return true;
    }
  else
    return false;
}

} // end namespace

