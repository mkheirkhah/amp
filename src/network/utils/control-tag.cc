/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
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
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "control-tag.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE ("ControlTag");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (ControlTag);

TypeId 
ControlTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ControlTag")
    .SetParent<Tag> ()
    .AddConstructor<ControlTag> ()
  ;
  return tid;
}
TypeId 
ControlTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
ControlTag::GetSerializedSize (void) const
{
  NS_LOG_FUNCTION (this);
  return 1;
}
void 
ControlTag::Serialize (TagBuffer buf) const
{
  NS_LOG_FUNCTION (this << &buf);
  buf.WriteU8 (m_controlPkt);
}
void 
ControlTag::Deserialize (TagBuffer buf)
{
  NS_LOG_FUNCTION (this << &buf);
  m_controlPkt = buf.ReadU8();
}
void 
ControlTag::Print (std::ostream &os) const
{
  NS_LOG_FUNCTION (this << &os);
  os << "A Control Packet is Received (SYN/ACK, FIN/ACK) = " << (int)m_controlPkt;
}
ControlTag::ControlTag ()
  : Tag () 
{
  NS_LOG_FUNCTION (this);
}
void
ControlTag::SetControlPkt (uint8_t controlPkt)
{
  NS_LOG_FUNCTION (this << controlPkt);
  m_controlPkt = controlPkt;
}
uint8_t
ControlTag::GetControlPkt (void) const
{
  NS_LOG_FUNCTION (this);
  return m_controlPkt;
}

} // namespace ns3
