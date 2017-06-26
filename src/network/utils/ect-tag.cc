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
#include "ect-tag.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE ("EctTag");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (EctTag);

TypeId 
EctTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::EctTag")
    .SetParent<Tag> ()
    .AddConstructor<EctTag> ()
  ;
  return tid;
}
TypeId 
EctTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
EctTag::GetSerializedSize (void) const
{
  NS_LOG_FUNCTION (this);
  return 1;
}
void 
EctTag::Serialize (TagBuffer buf) const
{
  NS_LOG_FUNCTION (this << &buf);
  buf.WriteU8 (m_ect);
}
void 
EctTag::Deserialize (TagBuffer buf)
{
  NS_LOG_FUNCTION (this << &buf);
  m_ect = buf.ReadU8();
}
void 
EctTag::Print (std::ostream &os) const
{
  NS_LOG_FUNCTION (this << &os);
  os << "ECN Echo  = " << (int)m_ect;
}
EctTag::EctTag ()
  : Tag () 
{
  NS_LOG_FUNCTION (this);
}
void
EctTag::SetEct (uint8_t ect)
{
  NS_LOG_FUNCTION (this << ect);
  m_ect = ect;
}
uint8_t
EctTag::GetEct (void) const
{
  NS_LOG_FUNCTION (this);
  return m_ect;
}

} // namespace ns3
