#ifndef MMP_TCP_SOCKET_BASE_H
#define MMP_TCP_SOCKET_BASE_H

#include "mp-tcp-socket-base.h"

using namespace std;

namespace ns3
{

typedef enum
{
  FlowSize,
  CwndSize,
  CwndSignal
} SwitchingMode_t;

class MMpTcpSocketBase : public MpTcpSocketBase
{
public:
  static TypeId GetTypeId(void);
  MMpTcpSocketBase();
  virtual ~MMpTcpSocketBase();

protected:
  virtual Ptr<TcpSocketBase> Fork(void);
  virtual void DoForwardUp(Ptr<Packet> p, Ipv4Header header, uint16_t port, Ptr<Ipv4Interface> interface);
  virtual bool SendPendingData(uint8_t sFlowIdx);
  virtual int SendDataPacket(uint8_t sFlowIdx, uint32_t pktSize, bool withAck);
  virtual void DoRetransmit   (uint8_t sFlowIdx);
  virtual void DoRetransmit   (uint8_t sFlowIdx, DSNMapping* ptrDSN);
  virtual void ReceivedAck    (uint8_t sFlowIdx, Ptr<Packet>, const TcpHeader&);
  virtual void DupAck         (uint8_t sFlowIdx, DSNMapping * ptrDSN);
  virtual void ProcessSynSent (uint8_t sFlowIdx, Ptr<Packet>, const TcpHeader&);
  virtual void OpenCWND(uint8_t sFlowIdx, uint32_t ackedBytes);
  virtual uint8_t getSubflowToUse();
  virtual void Retransmit(uint8_t sFlowIdx);

  void SetSwitchingMode(SwitchingMode_t);
  void SetFlowSizeThresh(uint32_t);
  void SetDupAckThresh(uint32_t);
  void DoGenerateOutPutFile();
  string GetTypeIdName(void);
  bool IsPktScattered(const TcpHeader &mptcpHeader);
  bool IsCwndExceedThresh(uint8_t sFlowIdx);

  // Congestion control stuff
  virtual void calculateAlpha();
  virtual void calculateTotalCWND();

private:
  bool m_packetScatter;
  bool m_subflowInitiation;
  uint64_t m_totalBytesSent;
  uint32_t m_flowSizeThresh;
  uint32_t m_cwndSizeThresh;
  uint32_t m_switchingMode;
  uint32_t m_dupAckThresh;
  uint32_t m_rtoMin;

public:
  bool m_isRTObackoff;
  bool m_isLimitedTx;
  bool m_isRTOmin;
  bool m_isThinStream;
  bool m_mmptcpv2;
  bool m_mmptcpv3;
  bool m_switchingCondition;

};

} // end namespace


#endif /* MMP_TCP_SOCKET_BASE_H */
