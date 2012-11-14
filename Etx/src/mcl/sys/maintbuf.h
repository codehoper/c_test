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

typedef struct MaintBuf MaintBuf;
typedef struct MaintBufPacket MaintBufPacket;
typedef struct MaintBufNode MaintBufNode;

struct MaintBufPacket {
    MaintBufPacket *Next;

    uint RefCnt;
    SRPacket *srp;
    LQSRAckId AckNum;
};

struct MaintBufNode {
    MaintBufNode *Next;

    VirtualAddress Address;
    LQSRIf OutIf;
    LQSRIf InIf;

    //
    // Fields used for transmitting Acknowledgement Requests.
    //

    LQSRAckId NextAckNum;        // Sequence number for next ack request.
    LQSRAckId LastAckNum;        // Last (highest) acknowledged sequence number.
    Time LastAckRcv;            // Time that LackAckNum was received.
    Time FirstAckReq;           // Time that first ack request was sent.
    Time LastAckReq;            // Time that most recent ack request was sent.

    uint NumPackets;
    uint HighWater;

    uint NumAckReqs;            // Number of ack requests sent.
    uint NumFastReqs;           // Number of fast-path ack requests.
    uint NumValidAcks;          // Number of acks received.
    uint NumInvalidAcks;        // Number of invalid acks received.

    MaintBufPacket *MBP;
};

struct MaintBuf {
    KSPIN_LOCK Lock;
    MaintBufNode *MBN;
    uint NumPackets;    // Across all MaintBufNodes.
    uint HighWater;     // Across all MaintBufNodes.
};

extern MaintBuf *
MaintBufNew(void);

extern void
MaintBufFree(MiniportAdapter *VA);

extern Time
MaintBufTimer(MiniportAdapter *VA, Time Now);

extern void 
MaintBufRecvAck(MiniportAdapter *VA, const VirtualAddress Address,
                LQSRIf InIf, LQSRIf OutIf, LQSRAckId AckNum);

extern void
MaintBufSendPacket(MiniportAdapter *VA, SRPacket *srp,
                   void (*Complete)(MiniportAdapter *VA, SRPacket *srp,
                                    NDIS_STATUS Status));

extern void
MaintBufSendAck(MiniportAdapter *VA, SRPacket *srp);

extern void
MaintBufResetStatistics(MiniportAdapter *VA);
