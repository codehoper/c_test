// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil -*- (for GNU Emacs)
//
// Copyright (c) Microsoft Corporation
//
// This file is part of the Microsoft Research Mesh Connectivity Layer.
// You should have received a copy of the Microsoft Research Shared Source
// license agreement (MSR-SSLA) for this software; see the file "license.txt".
// If not, please see http://research.microsoft.com/mesh/license.htm,
// or write to Microsoft Research, One Microsoft Way, Redmond, WA 98052-6399.
//

#define IEEE_802_ADDR_LENGTH 6              // Length of an IEEE 802 address.

typedef uchar PhysicalAddress[IEEE_802_ADDR_LENGTH];

typedef struct EtherHeader {
    PhysicalAddress Dest;
    PhysicalAddress Source;
    ushort Type;
} EtherHeader;

__inline boolint
IEEE_802_ADDR_IS_BROADCAST(const PhysicalAddress Addr)
{
    return Addr[0] & (uchar)0x01;
}

__inline void
IEEE_802_ADDR_SET_GROUP(PhysicalAddress Addr)
{
    Addr[0] |= (uchar)0x01;
}

__inline void
IEEE_802_ADDR_CLEAR_GROUP(PhysicalAddress Addr)
{
    Addr[0] &= (uchar)~0x01;
}

__inline void
IEEE_802_ADDR_SET_LOCAL(PhysicalAddress Addr)
{
    Addr[0] |= (uchar)0x02;
}

__inline void
IEEE_802_ADDR_CLEAR_LOCAL(PhysicalAddress Addr)
{
    Addr[0] &= (uchar)~0x02;
}
