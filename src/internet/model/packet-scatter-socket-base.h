#ifndef PACKET_SCATTER_SOCKET_BASE_H
#define PACKET_SCATTER_SOCKET_BASE_H

#include "ns3/mp-tcp-socket-base.h"

using namespace std;

namespace ns3
{

class PacketScatterSocketBase : public MpTcpSocketBase
{
public:
  static TypeId GetTypeId(void);
  PacketScatterSocketBase();
  virtual ~PacketScatterSocketBase();

  bool m_isRTObackoff;
  bool m_isLimitedTx;
  bool m_isRTOmin;
  bool m_isThinStream;

protected:
  virtual Ptr<TcpSocketBase> Fork(void);
  virtual void DoForwardUp(Ptr<Packet> p, Ipv4Header header, uint16_t port, Ptr<Ipv4Interface> interface);
  virtual bool SendPendingData(uint8_t sFlowIdx);
  virtual int  SendDataPacket(uint8_t sFlowIdx, uint32_t pktSize, bool withAck);
  virtual void DoRetransmit   (uint8_t sFlowIdx);
  virtual void DoRetransmit   (uint8_t sFlowIdx, DSNMapping* ptrDSN);
  virtual void ReceivedAck    (uint8_t sFlowIdx, Ptr<Packet>, const TcpHeader&);
  virtual void DupAck         (uint8_t sFlowIdx, DSNMapping * ptrDSN);
  virtual void ProcessSynSent (uint8_t sFlowIdx, Ptr<Packet>, const TcpHeader&);
  virtual void OpenCWND(uint8_t sFlowIdx, uint32_t ackedBytes);
  virtual void Retransmit(uint8_t sFlowIdx);
  void DoGenerateOutPutFile();
  bool IsPktScattered(const TcpHeader &mptcpHeader);
  void SetDupAckThresh(uint32_t);
  string GetTypeIdName();


private:
  uint32_t m_dupAckThresh;
  uint32_t m_rtoMin;
  bool     m_packetScatter;

};

} // end namespace


#endif /* PACKET_SCATTER_SOCKET_BASE_H */
