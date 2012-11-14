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

#ifndef __SENDBUF_H__
#define __SENDBUF_H__

typedef struct SendBufPacket SendBufPacket;
typedef struct SendBuf SendBuf;

struct SendBufPacket {
    SendBufPacket *Next;
    SRPacket *srp;
    Time Timeout;
};

//
// Usually there are at most a few packets in the send buffer,
// so we do not need a fancy data structure.
//
typedef struct SendBuf {
    KSPIN_LOCK Lock;

    SendBufPacket *Packets;
    SendBufPacket **Insert;

    SendBufPacket *FreeList;

    uint Size;
    uint HighWater;
    uint MaxSize;
};

extern NDIS_STATUS
SendBufNew(MiniportAdapter *VA, uint MaxSize);

extern void
SendBufFree(MiniportAdapter *VA);

extern void
SendBufInsert(MiniportAdapter *VA, SRPacket *srp);

extern void
SendBufCheck(MiniportAdapter *VA);

extern void
SendBufResetStatistics(MiniportAdapter *VA);

#endif
