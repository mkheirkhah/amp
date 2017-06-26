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
#include "ece-tag.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE ("EceTag");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (EceTag);

TypeId 
EceTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::EceTag")
    .SetParent<Tag> ()
    .AddConstructor<EceTag> ()
  ;
  return tid;
}
TypeId 
EceTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
EceTag::GetSerializedSize (void) const
{
  NS_LOG_FUNCTION (this);
  return 1;
}
void 
EceTag::Serialize (TagBuffer buf) const
{
  NS_LOG_FUNCTION (this << &buf);
  buf.WriteU8 (m_ece);
}
void 
EceTag::Deserialize (TagBuffer buf)
{
  NS_LOG_FUNCTION (this << &buf);
  m_ece = buf.ReadU8();
}
void 
EceTag::Print (std::ostream &os) const
{
  NS_LOG_FUNCTION (this << &os);
  os << "ECN Echo  = " << (int)m_ece;
}
EceTag::EceTag ()
  : Tag () 
{
  NS_LOG_FUNCTION (this);
}
void
EceTag::SetEce (uint8_t ece)
{
  NS_LOG_FUNCTION (this << ece);
  m_ece = ece;
}
uint8_t
EceTag::GetEce (void) const
{
  NS_LOG_FUNCTION (this);
  return m_ece;
}

} // namespace ns3
