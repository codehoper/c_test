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

#ifndef __LQSR_H__
#define __LQSR_H__
#include <packon.h>

//
// Generic LQSR definitions.
//

//
// This EtherType is assigned to Microsoft.
// It is also used by Network Load Balancing for their heartbeat messages.
// The first four bytes following the ethernet header distinguish
// different uses of the EtherType. The EtherType and Code values
// below are in wire byte-order.
//
#define ETYPE_MSFT              0x6f88          // 886f, byteswapped.

#define LQSR_CODE               0xc0de8af7      // LQSR packet.
#define MAIN_FRAME_CODE         0xc0de01bf      // NLB ping.
#define MAIN_FRAME_EX_CODE      0xc0de01c0      // NLB identity heartbeat.

#define VIRTUAL_ADDR_LENGTH 6
typedef uchar VirtualAddress[VIRTUAL_ADDR_LENGTH];

__inline boolint
VirtualAddressEqual(const VirtualAddress A, const VirtualAddress B)
{
    return RtlEqualMemory(A, B, sizeof(VirtualAddress));
}

__inline boolint
IsUnspecified(const VirtualAddress A)
{
    return ((A[0] == 0) && (A[1] == 0) && (A[2] == 0) &&
            (A[3] == 0) && (A[4] == 0) && (A[5] == 0));
}

//
// Defines various metrics that LQSR might use to measure
// the link quality. 
// Do NOT add anything *after* LAST. It is a guard value. 
//
typedef uint MetricType;
#define METRIC_TYPE_HOP     0
#define METRIC_TYPE_RTT     1
#define METRIC_TYPE_PKTPAIR 2
#define METRIC_TYPE_ETX     3
#define METRIC_TYPE_WCETT   4
#define METRIC_TYPE_LAST    5

typedef struct {
    uint LossProb : 12;
    uint Bandwidth : 12;
    uint Channel : 8;
} WCETTMetric;

#define SR_ADDR_LEN     VIRTUAL_ADDR_LENGTH
#define MAX_SR_LEN      8       // Maximum diameter of the network.
#define MAX_ETX_ENTRIES 40      // Max number of entries in an EtxProbe. 

#define LQSR_OPTION_TYPE_PAD1           0
#define LQSR_OPTION_TYPE_PADN           1
#define LQSR_OPTION_TYPE_REQUEST        2
#define LQSR_OPTION_TYPE_REPLY          3
#define LQSR_OPTION_TYPE_ERROR          4
#define LQSR_OPTION_TYPE_ACKREQ         5
#define LQSR_OPTION_TYPE_ACK            6
#define LQSR_OPTION_TYPE_SOURCERT       7
#define LQSR_OPTION_TYPE_INFOREQ        8
#define LQSR_OPTION_TYPE_INFO           9
#define LQSR_OPTION_TYPE_PROBE          10
#define LQSR_OPTION_TYPE_PROBEREPLY     11
#define LQSR_OPTION_TYPE_LINKINFO       12

//
// An LQSR frame contains:
//      Fixed-size header (LQSRHeader)
//      Variable number of options (LQSROption)
//      Fixed-size trailer (LQSRTrailer)
//      Payload
//      Variable number of padding bytes
// All bytes following the MAC field in the header are verified
// using HMAC-SHA1. From the trailer on, the bytes are (optionally)
// encrypted using AES128. The trailer and payload
// are not present in a "pure" LQSR control packet.
//

#define LQSR_MAC_LENGTH         16      // We truncate the HMAC-SHA1 output.
#define LQSR_IV_LENGTH          16      // The AES block size.
#define LQSR_MAC_SKIP           20      // First byte following MAC.

typedef struct {
    uint Code;                          // Demux different EtherType uses.
    uchar MAC[LQSR_MAC_LENGTH];         // Prior bytes not covered by the MAC.
    uchar IV[LQSR_IV_LENGTH];           // Random.
    ushort HeaderLength;                // Bytes of options following.
} LQSRHeader;

//
// We make optDataLen a ushort instead of a uchar to accommodate
// an ETX probe option with 40 neighbors.
// TODO: This should be done differently.
//
typedef struct {
    uchar optionType;
    ushort optDataLen;
    uchar payload[];
} LQSROption;

typedef struct {
    ushort NextHeader;
} LQSRTrailer;

//
// Route Request.
//

typedef uchar LQSRIf;

typedef struct {
    VirtualAddress addr;
    LQSRIf inif;
    LQSRIf outif;
    uint Metric;
} SRAddr;

#define ROUTE_REQUEST_HOPS(len) \
(((len) - (sizeof(RouteRequest) - sizeof(LQSROption))) / sizeof(SRAddr))
#define ROUTE_REQUEST_LEN(hops) \
((uchar)(sizeof(RouteRequest) - sizeof(LQSROption) + (hops) * sizeof(SRAddr)))

typedef uint LQSRReqId;

typedef struct {
    uchar optionType;
    ushort optDataLen;
    LQSRReqId identification;
    VirtualAddress targetAddress;
    SRAddr hopList[];
} RouteRequest;

//
// Route Reply.
//

#define ROUTE_REPLY_HOPS(len) \
(((len) - (sizeof(RouteReply) - sizeof(LQSROption))) / sizeof(SRAddr))
#define ROUTE_REPLY_LEN(hops) \
((uchar)(sizeof(RouteReply) - sizeof(LQSROption) + (hops) * sizeof(SRAddr)))

typedef struct {
    uchar optionType;
    ushort optDataLen;
    ushort Reserved;
    SRAddr hopList[];
} RouteReply;

//
// Route Error.
//

#define ROUTE_ERROR_LENGTH       (sizeof(RouteError) - sizeof(LQSROption))

typedef struct {
    uchar optionType;
    ushort optDataLen;
    VirtualAddress errorSrc;
    VirtualAddress errorDst;
    VirtualAddress unreachNode;
    LQSRIf inIf;
    LQSRIf outIf;
    uint Metric;
} RouteError;

//
// Acknowledgement Request.
//

#define ACK_REQUEST_LEN (sizeof(AcknowledgementRequest) - sizeof(LQSROption))

typedef ushort LQSRAckId;

typedef struct {
    uchar optionType;
    ushort optDataLen;
    LQSRAckId identification;
} AcknowledgementRequest;

//
// Acknowledgement.
//

#define ACKNOWLEDGEMENT_LEN      (sizeof(Acknowledgement) - sizeof(LQSROption))

typedef struct {
    uchar optionType;
    ushort optDataLen;
    LQSRAckId identification;
    VirtualAddress from;
    VirtualAddress to;
    LQSRIf inif;
    LQSRIf outif;
} Acknowledgement;

//
// Source Route.
//

#define SOURCE_ROUTE_HOPS(len) \
(((len) - (sizeof(SourceRoute) - sizeof(LQSROption))) / sizeof(SRAddr))
#define SOURCE_ROUTE_LEN(hops) \
((uchar)(sizeof(SourceRoute) - sizeof(LQSROption) + (hops) * sizeof(SRAddr)))

typedef struct {
    uchar optionType;
    ushort optDataLen;
    union {
        struct {
            ushort reservedField : 5;
            ushort staticRoute : 1;
            ushort salvageCount : 4;
            ushort segmentsLeft : 6;
        };
        ushort misc;
    };
    SRAddr hopList[];
} SourceRoute;

//
// Information Request.
//

#define INFO_REQUEST_LEN        (sizeof(InfoRequest) - sizeof(LQSROption))

typedef struct {
    uchar optionType;
    ushort optDataLen;
    LQSRReqId identification;
    VirtualAddress sourceAddress;
} InfoRequest;

//
// Information.
//

#define INFO_REPLY_LEN          (sizeof(InfoReply) - sizeof(LQSROption))

typedef struct {
    uchar optionType;
    ushort optDataLen;
    LQSRReqId identification;
    uint version;
    uchar info[];
} InfoReply;

//
// Probe.
//

#define PROBE_LEN (sizeof(Probe) - sizeof(LQSROption))

typedef struct {
    //
    // Generic fields. 
    //
    uchar OptionType;
    ushort OptDataLen;
    MetricType MetricType;
    MetricType ProbeType;
    uint Seq;
    Time Timestamp;
    VirtualAddress From;
    VirtualAddress To;
    LQSRIf InIf;
    LQSRIf OutIf;
    uint Metric;
    //
    // Metric-specific details go here. 
    // Since there is no common format, 
    // it's just a blob of bytes, which is 
    // interpreted according to MetricType.
    //
    char Special[];
} Probe;

//
// Metric Specific data. 
//
typedef struct {
    uint NumEntries;                // How many valid entries.
    struct Entry {
        VirtualAddress From;        // The sender's address.
        LQSRIf OutIf;               // Interface on which packets were sent. 
        LQSRIf InIf;                // Interface which received the packets.
        ushort Rcvd;                // Number of packets received from this host and interface.
    } Entry[MAX_ETX_ENTRIES];       // The option is always full-size.
} EtxProbe;

//
// Probe Reply.
//
#define PROBE_REPLY_LEN (sizeof(ProbeReply) - sizeof(LQSROption))

typedef struct {
    //
    // Generic fields.
    //
    uchar OptionType;
    ushort OptDataLen;
    MetricType MetricType;
    MetricType ProbeType;
    uint Seq;
    Time Timestamp;
    VirtualAddress From;
    VirtualAddress To;
    LQSRIf InIf;
    LQSRIf OutIf;
    //
    // Metric-specific details go here. 
    // Since there is no common format, 
    // it's just a blob of bytes, which is 
    // interpreted according to MetricType.
    //
    char Special[];
} ProbeReply;

//
// Metric-specific fields 
// for probe reply.
//
typedef struct {
    Time OutDelta; 
} PRPktPair;

//
// LinkInfo option. Rather like Route Reply, but instead of carrying
// a succession of links (A -> B -> C), the Link Info option carries
// multiple links from a single source (S -> A, S -> B, S -> C).
//

#define LINKINFO_HOPS(len) \
(((len) - (sizeof(LinkInfo) - sizeof(LQSROption))) / sizeof(SRAddr))
#define LINKINFO_LEN(hops) \
((uchar)(sizeof(LinkInfo) - sizeof(LQSROption) + (hops) * sizeof(SRAddr)))

typedef struct {
    uchar optionType;
    ushort optDataLen;
    VirtualAddress From;
    SRAddr Links[];
} LinkInfo;

#include <packoff.h>
#endif // __LQSR_H__
