/*
 * Morteza-MultiPath-TCP (MMPTCP) implementation.
 * Programmed by Morteza Kheirkhah from University of Sussex.
 * Email: m.kheirkhah@sussex.ac.uk
 * WebPage: http://www.sussex.ac.uk/Users/mk382/
 */
#define NS_LOG_APPEND_CONTEXT \
  if (m_node) { std::clog << Simulator::Now ().GetSeconds () << " [node " << m_node->GetId () << "] "; }

#include "ns3/packet-scatter-socket-base.h"
#include "tcp-l4-protocol.h"
#include "ns3/log.h"
#include "ns3/error-model.h"
#include "ns3/node.h"
//#include "ns3/output-stream-wrapper.h"

NS_LOG_COMPONENT_DEFINE("PacketScatterSocketBase");

using namespace std;
namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(PacketScatterSocketBase);

TypeId
PacketScatterSocketBase::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::PacketScatterSocketBase").SetParent<MpTcpSocketBase>().AddConstructor<PacketScatterSocketBase>()
//      .AddAttribute("DupAckThresh",
//                    "Threshold for fast retransmit",
//                    UintegerValue(0),
//                    MakeUintegerAccessor(&PacketScatterSocketBase::m_dupAckThresh),
//                    MakeUintegerChecker<uint32_t>())
      .AddAttribute("RTOmin", "RTOmin for initial subflow (packet scatter)",
                    UintegerValue(200),
                    MakeUintegerAccessor(&PacketScatterSocketBase::m_rtoMin),
                    MakeUintegerChecker<uint32_t>())
      .AddAttribute("IsLimitedTx", " ", BooleanValue(false),
                    MakeBooleanAccessor(&PacketScatterSocketBase::m_isLimitedTx),
                    MakeBooleanChecker())
      .AddAttribute("IsRTObackoff", " ", BooleanValue(false),
                    MakeBooleanAccessor(&PacketScatterSocketBase::m_isRTObackoff),
                    MakeBooleanChecker())
      .AddAttribute("IsRTOmin", " ", BooleanValue(false),
                    MakeBooleanAccessor(&PacketScatterSocketBase::m_isRTOmin),
                    MakeBooleanChecker())
      .AddAttribute("IsThinStream", " ", BooleanValue(false),
                    MakeBooleanAccessor(&PacketScatterSocketBase::m_isThinStream),
                    MakeBooleanChecker());
  return tid;
}

PacketScatterSocketBase::PacketScatterSocketBase() :
    MpTcpSocketBase(), m_dupAckThresh(0), m_packetScatter(true)
{
  NS_LOG_FUNCTION(this << m_packetScatter << subflows.size());
}

PacketScatterSocketBase::~PacketScatterSocketBase()
{
  NS_LOG_FUNCTION(this);
}

void
PacketScatterSocketBase::DoForwardUp(Ptr<Packet> p, Ipv4Header header, uint16_t port, Ptr<Ipv4Interface> interface)
{
  NS_LOG_FUNCTION(this); //

  if (m_endPoint == 0)
    {
      NS_LOG_UNCOND(this << "[" << m_node->GetId() << "] No endpoint exist");
      return;
    }

  Address fromAddress = InetSocketAddress(header.GetSource(), port);
  Address toAddress = InetSocketAddress(header.GetDestination(), m_endPoint->GetLocalPort());

  m_localAddress = header.GetDestination();
  m_remoteAddress = header.GetSource();
  m_remotePort = port;

  // Peel off TCP header and do validity checking
  TcpHeader mptcpHeader;
  p->RemoveHeader(mptcpHeader);

  //DCTCP
  ExtractPacketTags(p);

  m_localPort = mptcpHeader.GetDestinationPort();

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

  if (sFlowIdx != 0)
    exit(1);

  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

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
        m_tcp->SendPacket(Create<Packet>(), h, header.GetDestination(), header.GetSource(), FindOutputNetDevice(header.GetDestination()));
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
PacketScatterSocketBase::ProcessSynSent(uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader)
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
      if (!m_connected)
        { // This function only excute for initial subflow since when it has established then MPTCP connection is already established!!
          m_connected = true;
          m_endPoint->SetPeer(m_remoteAddress, m_remotePort); // TODO Is this needed at all?
          fLowStartTime = Simulator::Now().GetSeconds();      // It seems to be in right location for FCT!!
        }NS_LOG_INFO("(" << sFlow->routeId << ") "<< TcpStateName[sFlow->state] << " -> ESTABLISHED");
      sFlow->state = ESTABLISHED;
      sFlow->retxEvent.Cancel();
      sFlow->rtt->Init(mptcpHeader.GetAckNumber());
      if ((m_largePlotting && (flowType.compare("Large") == 0)) || (m_shortPlotting && (flowType.compare("Short") == 0)))
        sFlow->StartTracing("cWindow");
      sFlow->initialSequnceNumber = (mptcpHeader.GetAckNumber().GetValue());
      NS_LOG_INFO("(" <<sFlow->routeId << ") InitialSeqNb of data packet should be --->>> " << sFlow->initialSequnceNumber << " Cwnd: " << sFlow->cwnd);
      sFlow->RxSeqNumber = (mptcpHeader.GetSequenceNumber()).GetValue() + 1;
      sFlow->highestAck = std::max(sFlow->highestAck, mptcpHeader.GetAckNumber().GetValue() - 1);
      sFlow->TxSeqNumber = mptcpHeader.GetAckNumber().GetValue();
      sFlow->maxSeqNb = sFlow->TxSeqNumber - 1;

      Time estimate;
      estimate = Seconds(1.5);
      sFlow->rtt->SetCurrentEstimate(estimate);

      SendEmptyPacket(sFlowIdx, TcpHeader::ACK);

      // Advertise available addresses...
      if (addrAdvertised == false)
        {
          switch (pathManager)
            {
          case Default:
            // No address advertisement
            break;
          case FullMesh:
            exit(1);
            // Address need to be advertised
            AdvertiseAvailableAddresses();
            break;
          case NdiffPorts:
            exit(1);
            // New subflow can be initiated based on random source ports
            InitiateMultipleSubflows();
            break;
          default:
            break;
            }
          addrAdvertised = true;
        }

      if (m_state != ESTABLISHED)
        {
          m_state = ESTABLISHED;
          NotifyConnectionSucceeded();
        }

      // Initial window to 2
      //sFlow->cwnd = sFlow->MSS * 2;

      sFlow->m_retxThresh = 3 + m_dupAckThresh;

      if (m_isLimitedTx)
        sFlow->m_limitedTx = true;

      if (m_isRTOmin)
        sFlow->rtt->SetMinRto(MilliSeconds(m_rtoMin));

    } // end of else if (SYN/ACK)
  else
    { // Other in-sequence input
      if (tcpflags != TcpHeader::RST)
        { // When (1) rx of FIN+ACK; (2) rx of FIN; (3) rx of bad flags
          NS_LOG_LOGIC ("Illegal flag " << std::hex << static_cast<uint32_t> (tcpflags) << std::dec << " received. Reset packet is sent.");NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") Bad FtcpFlag received - SendRST");
          SendRST(sFlowIdx);
        }
      CloseAndNotifyAllSubflows();
    }
}

/** Process the newly received ACK */
void
PacketScatterSocketBase::ReceivedAck(uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << sFlowIdx << mptcpHeader);

  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint32_t ack = (mptcpHeader.GetAckNumber()).GetValue();

  if (m_DCTCP)
    {
      CalculateDCTCPAlpha (sFlowIdx, ack);
    }

#ifdef PLOT
  uint32_t tmp = ((ack - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->ACK.push_back(make_pair(Simulator::Now().GetSeconds(), tmp));
#endif

  // Stop execution if TCPheader is not ACK at all.
  if (0 == (mptcpHeader.GetFlags() & TcpHeader::ACK))
    { // Ignore if no ACK flag
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
          NS_LOG_UNCOND("["<< m_node->GetId() << "] (" << (int)sFlowIdx<< ") pktCount is zero!");
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
PacketScatterSocketBase::IsPktScattered(const TcpHeader &mptcpHeader)
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

bool
PacketScatterSocketBase::SendPendingData(uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION(this);
  // DCTCP: If we have received ECN echo in some of the received ACKs, slow down the congestion window
  if (m_DCTCP && sFlowIdx < maxSubflows && m_eceBit > 0 && subflows[sFlowIdx]->state == ESTABLISHED
      && subflows[sFlowIdx]->dctcp_maxseq < (subflows[sFlowIdx]->highestAck + 1))
    {
      SlowDown (sFlowIdx);
    }

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
              NS_LOG_UNCOND("["<< m_node->GetId() <<"] MainBuffer is empty - subflowBuffer(" << sF->mapDSN.size()<< ") sFlow("<< (int)sFlowIdx << ") AvailableWindow: " << window << " CWND: " << sF->cwnd << " subflow is in timoutRecovery{" << (sF->mapDSN.size() > 0) << "} LoopIter: " << whileCounter);
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

  uint32_t nOctetsSent = 0;
  Ptr<MpTcpSubFlow> sFlow;

  if (m_packetScatter)
    {
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
              if (sFlow->maxSeqNb > sFlow->TxSeqNumber - 1 && sendingBuffer.PendingData() <= sFlow->MSS)
                toSendSegSize = sFlow->MSS;
              if (toSendSegSize == 0)
                {
                  loop = false;
                  break;
                }
              int amountSent = SendDataPacket(sFlow->routeId, toSendSegSize, false);
              if (amountSent < 0)
                return false;
              else
                nOctetsSent += amountSent;
            } // end inner if clause
        } // end while loop
    } // end outer if clause
  else
    exit(1);
  if (nOctetsSent > 0)
    NotifyDataSent(GetTxAvailable());
  return (nOctetsSent > 0);
}

// This function only called by SendPendingData() in our implementation!
int
PacketScatterSocketBase::SendDataPacket(uint8_t sFlowIdx, uint32_t size, bool withAck)
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
      NS_ASSERT_MSG(sFlow->maxSeqNb == sFlow->TxSeqNumber -1, " maxSN: " << sFlow->maxSeqNb << " TxSeqNb-1" << sFlow->TxSeqNumber -1);
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

  // This is data packet, so its TCP_Flag should be 0
  uint8_t flags = withAck ? TcpHeader::ACK : 0;

  // Add MPTCP header to the packet
  TcpHeader header;
  header.SetFlags(flags);
  header.SetSequenceNumber(SequenceNumber32(sFlow->TxSeqNumber));
  header.SetAckNumber(SequenceNumber32(sFlow->RxSeqNumber));

  // PS: Add random source port
  if (m_shortFlowTCP && flowType.compare("Short") == 0 && sFlow->routeId == 0)
    {
      header.SetSourcePort(sFlow->sPort);
    }
  else if (sFlow->routeId == 0)
    header.SetSourcePort((rand() % 65530) + 1); // TODO change this to uniform random variable
  else
    exit(1);

  header.SetDestinationPort(sFlow->dPort);
  header.SetWindowSize(AdvertisedWindowSize());
  if (!guard)
    { // If packet is made from sendingBuffer, then we got to add the packet and its info to subflow's mapDSN.
      sFlow->AddDSNMapping(sFlowIdx, nextTxSequence, packetSize, sFlow->TxSeqNumber, sFlow->RxSeqNumber/*, p->Copy()*/);
    }

  if (!guard)
    { // if packet is made from sendingBuffer, then we use nextTxSequence to OptDSN
      //NS_ASSERT_MSG(remoteToken != 0,  "["<< m_node->GetId()<< "] RemoteToken: " << remoteToken << " sFlow's state: " << TcpStateName[sFlow->state] << " nextTxSeq: " << nextTxSequence);
      if (remoteToken == 0)
        {
          NS_LOG_UNCOND("*** ["<< m_node->GetId()<< "] RemoteToken: " << remoteToken << " sFlow's state: " << TcpStateName[sFlow->state] << " nextTxSeq: " << nextTxSequence << " SendRST <->  sAddr: " << sFlow->sAddr << " dAddr: " << sFlow->dAddr << " sPort: " << sFlow->sPort << " dPort: " << sFlow->dPort);
          sendingBuffer.ClearBuffer();
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
  calculateTotalCWND();

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
PacketScatterSocketBase::Fork(void)
{
  NS_LOG_FUNCTION_NOARGS();
  return CopyObject<PacketScatterSocketBase>(this);
}

void
PacketScatterSocketBase::DoRetransmit(uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // Retransmit SYN packet
  if (sFlow->state == SYN_SENT)
    {
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
  Ptr<Packet> pkt = Create<Packet>(ptrDSN->dataLevelLength);
  TcpHeader header;

  if (m_shortFlowTCP && flowType.compare("Short") == 0)
    {
      header.SetSourcePort(sFlow->sPort);
    }
  else
    header.SetSourcePort((rand() % 65530) + 1);

  header.SetDestinationPort(sFlow->dPort);
  header.SetFlags(TcpHeader::NONE);  // Change to NONE Flag
  header.SetSequenceNumber(SequenceNumber32(ptrDSN->subflowSeqNumber));
  header.SetAckNumber(SequenceNumber32(sFlow->RxSeqNumber));  // for the acknowledgment, we ACK the sFlow last received data
  header.SetWindowSize(AdvertisedWindowSize());

  NS_ASSERT_MSG(remoteToken != 0, " RemoteToken: " << remoteToken);
  header.AddOptDSN(OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber, remoteToken, 1);

  uint8_t hlen = 5;
  uint8_t olen = 20; //uint8_t olen = 15;
  uint8_t plen = 0;
  plen = (4 - (olen % 4)) % 4;
  olen = (olen + plen) / 4;
  hlen += olen;
  header.SetLength(hlen);
  header.SetOptionsLength(olen);
  header.SetPaddingLength(plen);

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
}

void
PacketScatterSocketBase::DoRetransmit(uint8_t sFlowIdx, DSNMapping* ptrDSN)
{
  NS_LOG_FUNCTION(this);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // This retransmit segment should be the lost segment.
  NS_ASSERT(ptrDSN->subflowSeqNumber >= sFlow->highestAck +1);

  SetReTxTimeout(sFlowIdx); // reset RTO

  // we retransmit only one lost pkt
  Ptr<Packet> pkt = Create<Packet>(ptrDSN->dataLevelLength);
  if (pkt == 0)
    NS_ASSERT(3!=3);

  TcpHeader header;
  //
  if (m_shortFlowTCP && flowType.compare("Short") == 0)
    {
      header.SetSourcePort(sFlow->sPort);
    }
  else
    header.SetSourcePort((rand() % 65530) + 1);

  //
  header.SetDestinationPort(sFlow->dPort);
  header.SetFlags(TcpHeader::NONE);  // Change to NONE Flag
  header.SetSequenceNumber(SequenceNumber32(ptrDSN->subflowSeqNumber));
  header.SetAckNumber(SequenceNumber32(sFlow->RxSeqNumber));
  header.SetWindowSize(AdvertisedWindowSize());

  // Add remote token and packetScatter flag to true  // PS: Add
  NS_ASSERT_MSG(remoteToken != 0, " RemoteToken: " << remoteToken);
  header.AddOptDSN(OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber, remoteToken, 1);

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

  NS_LOG_INFO("("<<(int) sFlowIdx << ") DoRetransmit -> " << header);
}

void
PacketScatterSocketBase::DupAck(uint8_t sFlowIdx, DSNMapping* ptrDSN)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  sFlow->m_dupAckCount++;
  ptrDSN->dupAckCount++; // Used for evaluation purposes only
  uint32_t segmentSize = sFlow->MSS;

#ifdef PLOT
  uint32_t tmp = (((ptrDSN->subflowSeqNumber) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->DUPACK.push_back(make_pair(Simulator::Now().GetSeconds(), tmp));
#endif

  // PS: Tuning DupAckThresh dynamically based on flight packets
  if (m_isThinStream && sFlowIdx == 0 && !sFlow->m_inFastRec && sFlow->m_dupAckCount < sFlow->m_retxThresh)
    {
      uint32_t dupAckThresh = (BytesInFlight(sFlowIdx) / sFlow->MSS) / 2;
      sFlow->m_retxThresh = dupAckThresh;
    }

  // congestion control algorithms
  if (sFlow->m_dupAckCount == sFlow->m_retxThresh && !sFlow->m_inFastRec)
    { // FastRetrasmsion
      NS_LOG_WARN (Simulator::Now().GetSeconds() <<" DupAck -> subflow ("<< (int)sFlowIdx <<") 3rd duplicated ACK for segment ("<<ptrDSN->subflowSeqNumber<<")");

      // Cut the window to the half
      ReduceCWND(sFlowIdx, ptrDSN);

#ifdef PLOT
      sFlow->_FReTx.push_back(make_pair(Simulator::Now().GetSeconds(), TimeScale));
#endif
      FastReTxs++;
    }
  else if (sFlow->m_inFastRec)
    { // Fast Recovery
      sFlow->cwnd += segmentSize;

#ifdef PLOT
      DupAcks.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->cwnd));
      sFlow->ssthreshtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->ssthresh));
#endif
      NS_LOG_WARN ("DupAck-> FastRecovery. Increase cwnd by one MSS, from " << sFlow->cwnd.Get() <<" -> " << sFlow->cwnd << " AvailableWindow: " << AvailableWindow(sFlowIdx));
      FastRecoveries++;
      SendPendingData(sFlow->routeId);
    }
  else if (!sFlow->m_inFastRec && sFlow->m_limitedTx && sendingBuffer.PendingData() > 0)
    { // RFC3042 Limited transmit: Send a new packet for each duplicated ACK before fast retransmit
      NS_LOG_INFO ("Limited transmit");
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
PacketScatterSocketBase::OpenCWND(uint8_t sFlowIdx, uint32_t ackedBytes)
{
  NS_LOG_FUNCTION(this << (int) sFlowIdx << ackedBytes);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  double adder = 0;
  uint32_t cwnd = sFlow->cwnd.Get();
  uint32_t ssthresh = sFlow->ssthresh;

  if (cwnd < ssthresh)
    { // Slow Start phase
      sFlow->cwnd += sFlow->MSS;
#ifdef PLOT
      sFlow->ssthreshtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->ssthresh));
      sFlow->CWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), sFlow->cwnd));
      totalCWNDtrack.push_back(make_pair(Simulator::Now().GetSeconds(), totalCwnd));
      sFlow->_ss.push_back(make_pair(Simulator::Now().GetSeconds(), TimeScale));
#endif
    }
  else
    {
      adder = static_cast<double>(sFlow->MSS * sFlow->MSS) / cwnd;
      adder = std::max(1.0, adder);
      sFlow->cwnd += static_cast<double>(adder);
      NS_LOG_LOGIC ("Subflow "<<(int)sFlowIdx<<" pScatter Congestion Control (Uncoupled_TCPs) increment is "<<adder<<" ssthresh "<< ssthresh << " cwnd "<<cwnd);
      return;
    }
}

/** Retransmit timeout */
void
PacketScatterSocketBase::Retransmit(uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  sFlow->m_inFastRec = false;
  sFlow->ssthresh = std::max(2 * sFlow->MSS, BytesInFlight(sFlowIdx) / 2);
  sFlow->cwnd = sFlow->MSS;
  sFlow->TxSeqNumber = sFlow->highestAck + 1; // m_nextTxSequence = m_txBuffer.HeadSequence(); // Restart from highest Ack

  if (m_isRTObackoff == false)
    sFlow->rtt->IncreaseMultiplier();  // Double the next RTO

  DoRetransmit(sFlowIdx);  // Retransmit the packet
#ifdef PLOT
      sFlow->_TimeOut.push_back(make_pair(Simulator::Now().GetSeconds(), TimeScale));
#endif
  TimeOuts++;
}

void
PacketScatterSocketBase::SetDupAckThresh(uint32_t dupack){
  m_dupAckThresh = dupack;
}

// [: NodeId :][+FlowId+][= FlowType =][# Throughput #][* FlowComplTime *]
// [$ timeOut $][! fastReTx !][( PartialAck )][@ FullAck @][^ FastRecovery ^]
// [& Subflows &][% TypeId %][-RTT-]
void
PacketScatterSocketBase::DoGenerateOutPutFile()
{
  TypeId tid = this->GetTypeId();
  DoDoGenerateOutPutFile(tid);
  /*
  goodput = ((nextTxSequence * 8) / (Simulator::Now().GetSeconds() - fLowStartTime));
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(outputFileName, std::ios::out | std::ios::app);
  ostream* os = stream->GetStream();
  *os << "[:" << m_node->GetId() << ":][+" << flowId << "+][=" << flowType << "=][#" << goodput / 1000000 << "#][*"
      << (Simulator::Now().GetSeconds() - fLowStartTime) << "*][$" << TimeOuts << "$][!" << FastReTxs << "!][(" << pAck << ")][@"
      << FullAcks << "@][^" << FastRecoveries << "^][&" << subflows.size() << "&][%" << tid.GetName() << "%][/"
      << subflows[(uint32_t)currentSublow]->lastMeasuredRtt.GetSeconds() << "/]""[~" << subflows[0]->m_retxThresh << "~]" << endl;

  NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " ["<< m_node->GetId()<< "] Goodput -> " << goodput / 1000000 << " Mbps");
  */
}

string
PacketScatterSocketBase::GetTypeIdName()
{
  string tmp = this->GetTypeId().GetName();
  if (tmp.compare("ns3::PacketScatterSocketBase") == 0)
    return "PS";
  else
    return "UnKnown";
}

}  // end namespace

