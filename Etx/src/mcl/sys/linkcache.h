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

#ifndef __LINKCACHE_H__
#define __LINKCACHE_H__
#include "sr.h"

typedef struct Link Link; 

typedef struct Rtt {
    uint SentProbes;            // Number of probes sent.
    uint LostProbes;            // Count of probes deemed lost.
    uint LateProbes;            // Count of probes received late. 
    uint RawMetric;             // Raw metric. 
    uint LastRTT;               // Last measured RTT.
    LARGE_INTEGER LastProbe;    // When we last probed this link.
    Time ProbeTimeout;          // When does RttSendProbes need to be called?
    Time HysteresisTimeout;     // When does RttUpdateMetric needs to be called?
} Rtt;

// 
// If you change this, remember to change 
// MCL_INFO_MAX_PKTPAIR_HISTORY in ntdlqsr.h
//
#define MAX_PKTPAIR_HISTORY 30 

typedef struct MinHistory {
    uint Min; 
    uint Seq;
} MinHistory; 

typedef struct PktPair {
    //
    // Probe sender maintains the following variables. 
    //
    uint ProbeSeq;              // Probe sequence number.
    uint PairsSent;             // Number of pairs sent.
    uint RepliesSent;           // Number of replies sent on this link.
    uint RepliesRcvd;           // Number of replies received on this link.
    uint LostPairs;             // Count of pairs deemed lost.
    Time ProbeTimeout;          // When does PktPairSendProbes need to be called?
    uint Outstanding;           // Sequence number of last otstanding probe. 
    uint Delta;                 // Last update to metric. 
    uint RTT;                   // The RTT metric - we compute for free. 
    uint LastRTT;               // Last RTT sample.  
    uint LastPktPair;           // Last PacketPair sample.
    //
    // Probe receiver maintains these. 
    //
    Time LastProbeTimestamp;    // Timestamp carried in the last probe.
    Time TimeLastProbeRcvd;     // Time last probe was received.  
    uint LastProbeSeq;          // Sequence number of last probe.
    //
    // Min time history. Each entry is a minimum over last two minutes. 
    // The first buffer maintains current data, the second one is for 
    // reading. Ensures that we will read consistently. 
    //
    uint CurrMin;           // Current min.
    Time NextMinTime;       // Next time we copy the current in in History.
    uint NumSamples;        // Total number of samples so far.
    MinHistory MinHistory[MAX_PKTPAIR_HISTORY];  // History.
} PktPair;

typedef struct {   
    Time RcvdTS;                // When was this probe received. 
} ProbeHistory; 

#define MAX_ETX_PROBE_HISTORY 50 

typedef struct Etx {
    uint TotSentProbes;         // Total number of probes sent.
    uint TotRcvdProbes;         // Total number of probes received.
    uint FwdDeliv;              // Number of probes that we sent
                                // that got through in one LossInterval, 
                                // as per last report by this host. 
    uint ProbeHistorySZ;        // Number of probes we have in the history.
                                // This is also the RevDeliv, i.e. the
                                // number the number of probes that
                                // the other node sent that got
                                // through to us in last LossInterval.
    ProbeHistory PH[MAX_ETX_PROBE_HISTORY];
                                // History of last few probes received on
                                // this link. Maintained as a circular queue.
    uint PHStart; 
    uint PHEnd; 
    uint LastProb;
} Etx;

typedef struct Wcett {
    Etx Etx;
    PktPair PktPair;
    uint MaxBandwidth;  // Max Bandwidth, defined by the physical adapter.
    uint NumPktPairValid;
    uint NumPktPairInvalid;
} Wcett;

typedef union MetricInfo {
    Rtt Rtt;
    PktPair PktPair;
    Etx Etx;
    Wcett Wcett;
} MetricInfo;

struct Link {
    Link *NextOut;              // Next link in a Node's AdjOut list.
    Link **PrevOut;             // Prev link in a Node's AdjOut list.

    Link *NextIn;               // Next link in a Node's AdjIn list.
    Link **PrevIn;              // Prev link in a Node's AdjIn list.

    uint RefCnt;                // How many cached routes use this link.
#if DBG
    uint CheckRefCnt;
#endif

    int sourceIndex;            // Index of source node.
    int targetIndex;            // Index of target node.
    LQSRIf outif;               // Outgoing interface on source node.
    LQSRIf inif;                // Incoming interface on target node.

    Time TimeStamp;             // When was the link last updated.
    union {
        uint Metric;            // Metric to route on.
        WCETTMetric wcett;
    };
    MetricInfo MetricInfo;      // Information that is metric-specific.
    uint Usage;                 // Count of packets traversing this link.
    uint Failures;              // Count of link failures (adj links only).
    uint DropRatio;             // Ratio of packets on link to drop.
    uint ArtificialDrops;       // Count of packets artificially dropped.
    uint QueueDrops;            // Count of packets dropped b/c tx queue full.
};

#if CHANGELOGS
typedef struct RouteUsage RouteUsage;
struct RouteUsage {
    RouteUsage *Next;
    uint Usage;         // Count of packets using this route.
    uint Hops;          // Number of hops in the route. Zero means no route.
    SRAddr Route[];     // Array of nodes in the route, of size Hops.
};
#endif

typedef struct _CacheNode {
    VirtualAddress address;

    Link *AdjOut;               // Links to adjacent nodes.
    Link *AdjIn;                // Links from adjacent nodes.

    //
    // SR can be either a static route or cached dynamic route.
    // Hops & Metric are used for cached dynamic routes.
    // Hops points into the same pool allocation as SR.
    // The Hops array length is SOURCE_ROUTE_HOPS(SR->optDataLen) - 1.
    //
    SourceRoute *SR;            // Non-NULL if there is a cached route.
    uint Metric;                // Path metric of the cached route.
    Link **Hops;                // Links in the cached route.
    Time FirstUsage;            // Time that cached route was first used.
    uint RouteChanges;          // Counts changes to the cached route.

#if CHANGELOGS
    RouteUsage *CurrentRU;      // RouteUsage record for current route.
    RouteUsage *History;        // Records for all routes to this destination.
#endif
} CacheNode;

#if CHANGELOGS
struct LinkChange {
    Time TimeStamp;
    VirtualAddress From;
    VirtualAddress To;
    LQSRIf InIf;
    LQSRIf OutIf;
    uint Metric;
    uint Reason;
};

struct RouteChange {
    Time TimeStamp;
    VirtualAddress Dest;
    uint Metric;        // Metric of this route.
    uint PrevMetric;    // Metric of previous route to this destination.

    //
    // We can't use the SourceRoute type here because it has
    // an embedded zero-length array.
    // An optionType of zero indicates the route does not exist.
    //
    uchar Buffer[sizeof(SourceRoute) + sizeof(SRAddr) * MAX_SR_LEN];
};
#endif

typedef struct _LinkCache {
    KSPIN_LOCK Lock;

    uint nodeCount;
    uint maxSize;
    CacheNode *nodes;

    //
    // Dijkstra's populates these:
    //
    uint *metric;
    uint *hops;
    uint *prev;
    Link **link;
    uint SmallestMetric;        // Smallest metric of any link.
    uint LargestMetric;         // Largest metric used in dijkstra.

    Time timeout;  // When do we need to call dijkstra() again?

    uint CountAddLinkInvalidate;
    uint CountAddLinkInsignificant;
    uint CountRouteFlap;
    uint CountRouteFlapDamp;

#if CHANGELOGS
    uint NextLinkRecord;
    struct LinkChange LinkChangeLog[NUM_LINKCHANGE_RECORDS];

    uint NextRouteRecord;
    struct RouteChange RouteChangeLog[NUM_ROUTECHANGE_RECORDS];
#endif // CHANGELOGS
} LinkCache;

extern LinkCache *
LinkCacheAllocate(uint size, const VirtualAddress myaddr);

extern void
LinkCacheResetStatistics(MiniportAdapter *VA);

extern void
LinkCacheFree(MiniportAdapter *VA);

extern void
LinkCacheFlush(MiniportAdapter *VA);

extern void
LinkCacheAddLink(MiniportAdapter *VA,
                 const VirtualAddress From, const VirtualAddress To,
                 LQSRIf InIf, LQSRIf OutIf, uint Metric,
                 uint Reason);

extern NDIS_STATUS
LinkCacheControlLink(MiniportAdapter *VA,
                     const VirtualAddress From, const VirtualAddress To,
                     LQSRIf InIf, LQSRIf OutIf, uint DropRatio);

extern boolint
LinkCacheCheckForDrop(MiniportAdapter *VA,
                      const VirtualAddress From, const VirtualAddress To,
                      LQSRIf InIf, LQSRIf OutIf);

extern void
LinkCacheCountLinkUse(MiniportAdapter *VA,
                      const VirtualAddress From, const VirtualAddress To,
                      LQSRIf InIf, LQSRIf OutIf);

extern uint
LinkCacheLookupMetric(MiniportAdapter *VA,
                      const VirtualAddress From, const VirtualAddress To,
                      LQSRIf InIf, LQSRIf OutIf);

extern void
LinkCacheUpdateRR(MiniportAdapter *VA, InternalRouteRequest *RR);

extern boolint
LinkCacheUseSR(MiniportAdapter *VA, SRPacket *SRP);

extern NDIS_STATUS
LinkCacheFillSR(MiniportAdapter *VA, const VirtualAddress Dest,
                InternalSourceRoute *SR);

extern NDIS_STATUS
LinkCacheGetSR(MiniportAdapter *VA, const VirtualAddress Dest,
               InternalSourceRoute *SR);

extern InternalLinkInfo *
LinkCacheCreateLI(MiniportAdapter *VA, uint MaxSize);

extern void
LinkCachePenalizeLink(MiniportAdapter *VA,
                      const VirtualAddress to,
                      LQSRIf InIf, LQSRIf OutIf);

extern uint
LinkCacheMyDegree(MiniportAdapter *VA);

extern void
LinkCacheDeleteInterface(MiniportAdapter *VA, LQSRIf IF);

extern NDIS_STATUS
LinkCacheAddRoute(MiniportAdapter *VA, const VirtualAddress Dest,
                  SourceRoute *SR);

#endif
