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
#include "ecmp-tag.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE ("EcmpTag");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (EcmpTag);

TypeId 
EcmpTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::EcmpTag")
    .SetParent<Tag> ()
    .AddConstructor<EcmpTag> ()
  ;
  return tid;
}
TypeId 
EcmpTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
EcmpTag::GetSerializedSize (void) const
{
  NS_LOG_FUNCTION (this);
  return 1;
}
void 
EcmpTag::Serialize (TagBuffer buf) const
{
  NS_LOG_FUNCTION (this << &buf);
  buf.WriteU8 (m_ecmp);
}
void 
EcmpTag::Deserialize (TagBuffer buf)
{
  NS_LOG_FUNCTION (this << &buf);
  m_ecmp = buf.ReadU8();
}
void 
EcmpTag::Print (std::ostream &os) const
{
  NS_LOG_FUNCTION (this << &os);
  os << "ECMP forwarding path id  = " << (int)m_ecmp;
}
EcmpTag::EcmpTag ()
  : Tag () 
{
  NS_LOG_FUNCTION (this);
}
void
EcmpTag::SetEcmp (uint8_t ecmp)
{
  NS_LOG_FUNCTION (this << ecmp);
  m_ecmp = ecmp;
}
uint8_t
EcmpTag::GetEcmp (void) const
{
  NS_LOG_FUNCTION (this);
  return m_ecmp;
}

} // namespace ns3
