/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 University of Washington
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "drop-tail-queue.h"
#include "ns3/ce-tag.h" //Morteza
#include "ns3/control-tag.h" //Morteza
#include "ns3/ect-tag.h" //Morteza


NS_LOG_COMPONENT_DEFINE ("DropTailQueue");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (DropTailQueue)
  ;

TypeId DropTailQueue::GetTypeId (void) 
{
  static TypeId tid = TypeId ("ns3::DropTailQueue")
    .SetParent<Queue> ()
    .AddConstructor<DropTailQueue> ()
    .AddAttribute ("Mode", 
                   "Whether to use bytes (see MaxBytes) or packets (see MaxPackets) as the maximum queue size metric.",
                   EnumValue (QUEUE_MODE_PACKETS),
                   MakeEnumAccessor (&DropTailQueue::SetMode),
                   MakeEnumChecker (QUEUE_MODE_BYTES, "QUEUE_MODE_BYTES",
                                    QUEUE_MODE_PACKETS, "QUEUE_MODE_PACKETS"))
    .AddAttribute ("MaxPackets", 
                   "The maximum number of packets accepted by this DropTailQueue.",
                   UintegerValue (100),
                   MakeUintegerAccessor (&DropTailQueue::m_maxPackets),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxBytes", 
                   "The maximum number of bytes accepted by this DropTailQueue.",
                   UintegerValue (100 * 65535),
                   MakeUintegerAccessor (&DropTailQueue::m_maxBytes),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MarkingTh",
                   "The DCTCP Marking Threshold.",
                   UintegerValue (100),
                   MakeUintegerAccessor (&DropTailQueue::m_markingTh),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Marking",
                   "Enable Marking Capability to this Queue",
                   BooleanValue (false),
                   MakeBooleanAccessor (&DropTailQueue::m_isMarking),
                   MakeBooleanChecker ())
  ;

  return tid;
}

DropTailQueue::DropTailQueue () :
  Queue (),
  m_packets (),
  m_bytesInQueue (0)
{
  NS_LOG_FUNCTION (this);
}

DropTailQueue::~DropTailQueue ()
{
  NS_LOG_FUNCTION (this);
}

void
DropTailQueue::SetMode (DropTailQueue::QueueMode mode)
{
  NS_LOG_FUNCTION (this << mode);
  m_mode = mode;
}

DropTailQueue::QueueMode
DropTailQueue::GetMode (void)
{
  NS_LOG_FUNCTION (this);
  return m_mode;
}

bool 
DropTailQueue::DoEnqueue (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this << p);

  ControlTag controlTag;
  bool isControlPkt = p->PeekPacketTag(controlTag);
  EctTag ectTag;
  bool isEct = p->PeekPacketTag(ectTag);

  if (m_mode == QUEUE_MODE_PACKETS && (m_packets.size () >= m_maxPackets) && !isControlPkt)
    {
      NS_LOG_LOGIC ("Queue full (at max packets) -- droppping pkt");
      Drop (p);
      return false;
    }

  if (m_mode == QUEUE_MODE_BYTES && (m_bytesInQueue + p->GetSize () >= m_maxBytes) && !isControlPkt)
    {
      NS_LOG_LOGIC ("Queue full (packet would exceed max bytes) -- droppping pkt");
      Drop (p);
      return false;
    }

  if (m_isMarking)
    {
      uint32_t queueSize;
      if (m_mode == QUEUE_MODE_BYTES)
        queueSize = m_bytesInQueue;
      else if (m_mode == QUEUE_MODE_PACKETS)
        queueSize = m_packets.size ();

      if (queueSize >= m_markingTh)
        { // We do not mark control packets && packet should be ECN capable (ECT)
          if (isEct)
            {
              CeTag ceTag;
              bool exist = p->PeekPacketTag (ceTag);
              if (!exist && !isControlPkt)
                {
                  ceTag.SetCe (1);
                  p->AddPacketTag (ceTag);
                }
            }
          else if (!isEct && isControlPkt)
            {
              // Do not drop control packets, when marking threshold has been reached!
            }
          else
            {
              NS_LOG_LOGIC ("Queue non-ECT packet need to be drop when Red reaches it marking threshold!");
              Drop (p);
              return false;
            }
        }
    }

  if (!isControlPkt)
    m_bytesInQueue += p->GetSize ();
  m_packets.push (p);

  NS_LOG_LOGIC ("Number packets " << m_packets.size ());
  NS_LOG_LOGIC ("Number bytes " << m_bytesInQueue);

  return true;
}

Ptr<Packet>
DropTailQueue::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  if (m_packets.empty ())
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }

  Ptr<Packet> p = m_packets.front ();
  ControlTag controlTag;
  bool isControlPkt = p->PeekPacketTag(controlTag);
  m_packets.pop ();
  if (!isControlPkt)
    m_bytesInQueue -= p->GetSize ();

  NS_LOG_LOGIC ("Popped " << p);

  NS_LOG_LOGIC ("Number packets " << m_packets.size ());
  NS_LOG_LOGIC ("Number bytes " << m_bytesInQueue);

  return p;
}

Ptr<const Packet>
DropTailQueue::DoPeek (void) const
{
  NS_LOG_FUNCTION (this);

  if (m_packets.empty ())
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }

  Ptr<Packet> p = m_packets.front ();

  NS_LOG_LOGIC ("Number packets " << m_packets.size ());
  NS_LOG_LOGIC ("Number bytes " << m_bytesInQueue);

  return p;
}

void
DropTailQueue::SetMarking(bool marking)
{
  m_isMarking = marking;
}
} // namespace ns3

