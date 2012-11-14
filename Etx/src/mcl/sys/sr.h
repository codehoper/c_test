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

#ifndef __SR_H__
#define __SR_H__

//
// Generic form of internal option representation.
// All internal option formats must match these initial fields.
//
typedef struct _InternalOption InternalOption;
typedef struct _InternalOption {
    InternalOption *Next;
    LQSROption Opt;
};

typedef struct _InternalRouteReply InternalRouteReply;
struct _InternalRouteReply {
    InternalRouteReply *next;
    RouteReply opt;
    // Remember to allocate sizeof(SRAddr) * MAX_SR_LEN extra bytes!
};

typedef struct _InternalRouteRequest InternalRouteRequest;
struct _InternalRouteRequest {
    InternalRouteRequest *next;
    RouteRequest opt;
    // Remember to allocate sizeof(SRAddr) * MAX_SR_LEN extra bytes!
};

typedef struct _InternalSourceRoute InternalSourceRoute;
struct _InternalSourceRoute {
    InternalSourceRoute *next;
    SourceRoute opt;
    // Remember to allocate sizeof(SRAddr) * MAX_SR_LEN extra bytes!
};

typedef struct _InternalRouteError InternalRouteError;
struct _InternalRouteError {
    InternalRouteError *next;
    RouteError opt;
};

typedef struct _InternalAcknowledgementRequest InternalAcknowledgementRequest;
struct _InternalAcknowledgementRequest {
    InternalAcknowledgementRequest *next;
    AcknowledgementRequest opt;
};

typedef struct _InternalAcknowledgement InternalAcknowledgement;
struct _InternalAcknowledgement {
    InternalAcknowledgement *next;
    Acknowledgement opt;
};

typedef struct _InternalInfoRequest InternalInfoRequest;
struct _InternalInfoRequest {
    InternalInfoRequest *next;
    InfoRequest opt;
};

typedef struct _InternalInfoReply InternalInfoReply;
struct _InternalInfoReply {
    InternalInfoReply *next;
    InfoReply opt;
};

typedef struct _InternalProbe InternalProbe;
struct _InternalProbe {
    InternalProbe *Next;
    Probe Opt;
};

typedef struct _InternalProbeReply InternalProbeReply;
struct _InternalProbeReply {
    InternalProbeReply *Next;
    ProbeReply Opt;
};

typedef struct _InternalLinkInfo InternalLinkInfo;
struct _InternalLinkInfo {
    InternalLinkInfo *Next;
    LinkInfo Opt;
};

typedef struct _SRPacket SRPacket;
struct _SRPacket {
    SRPacket *Next;

    PhysicalAddress EtherDest;
    PhysicalAddress EtherSource;
    VirtualAddress Dest;
    VirtualAddress Source;
    uchar IV[LQSR_IV_LENGTH];

    //
    // If you add a new option type, be sure to update
    // SROptionLength and PbackPacketSize as well as
    // SRPacketToPkt, SRPacketFromPkt etc.
    //
    InternalSourceRoute *sr;
    InternalRouteRequest *req;
    InternalRouteReply *rep;
    InternalRouteError *err;
    InternalAcknowledgementRequest *ackreq;
    InternalAcknowledgement *ack;
    InternalInfoRequest *inforeq;
    InternalInfoReply *inforep;
    InternalProbe *Probe;
    InternalProbeReply *ProbeReply;
    InternalLinkInfo *LinkInfo;

    //
    // The encrypted payload carried in this SRPacket.
    // Packet may be NULL, for example for an Ack or a Route Request,
    // in which case PayloadOffset should be zero.
    //
    NDIS_PACKET *Packet;
    uint PacketLength;          // Valid on the receive path.
    uint PayloadOffset;

    ProtocolAdapter *PA;
    void (*FreePacket)(ProtocolAdapter *PA, NDIS_PACKET *Packet);

    //
    // Used by ProtocolForwardRequest.
    //
    void (*TransmitComplete)(MiniportAdapter *VA, SRPacket *Srp, NDIS_STATUS Status);
    uint ForwardCount;
    NDIS_STATUS ForwardStatus;
};


extern void 
SRPacketFree(SRPacket *Srp);

extern void
SRFreeOptionList(InternalOption *List);

extern uint
SROptionListLength(InternalOption *List);

extern NDIS_STATUS
SRPacketFromPkt(MiniportAdapter *VA, NDIS_PACKET *Packet, SRPacket **pSRP);

extern NDIS_STATUS
SRPacketToPkt(MiniportAdapter *VA, const SRPacket *Srp, NDIS_PACKET **pPacket);

extern NDIS_STATUS
CheckPacket(MiniportAdapter *VA, NDIS_PACKET *Packet);

#endif /* __SR_H__ */
