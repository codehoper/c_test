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

#include "headers.h"

#define CACHE_TIMEOUT   (1 * SECOND)

static int dijkstra(MiniportAdapter *VA, Time Now);
static int findNode(LinkCache *LC, const VirtualAddress addr, boolint Force);

//* LinkCacheLinkChange
//
//  Records a link status change.
//
//  Called while holding LC->Lock.
//  
#if !CHANGELOGS
__inline
#endif
static void
LinkCacheLinkChange(
    LinkCache *LC,
    const VirtualAddress From,
    const VirtualAddress To,
    LQSRIf InIf,
    LQSRIf OutIf,
    uint Metric,
    uint Reason)
{
#if CHANGELOGS
    struct LinkChange *Record = &LC->LinkChangeLog[LC->NextLinkRecord];

    KeQuerySystemTime((LARGE_INTEGER *)&Record->TimeStamp);
    RtlCopyMemory(Record->From, From, sizeof(VirtualAddress));
    RtlCopyMemory(Record->To, To, sizeof(VirtualAddress));
    Record->InIf = InIf;
    Record->OutIf = OutIf;
    Record->Metric = Metric;
    Record->Reason = Reason;

    LC->NextLinkRecord = (LC->NextLinkRecord + 1) % NUM_LINKCHANGE_RECORDS;
#else
    UNREFERENCED_PARAMETER(LC);
    UNREFERENCED_PARAMETER(From);
    UNREFERENCED_PARAMETER(To);
    UNREFERENCED_PARAMETER(InIf);
    UNREFERENCED_PARAMETER(OutIf);
    UNREFERENCED_PARAMETER(Metric);
    UNREFERENCED_PARAMETER(Reason);
#endif
}

//* LinkCacheRouteChange
//
//  Records a route change.
//
//  Called while holding LC->Lock.
//  
#if !CHANGELOGS
__inline
#endif
static void
LinkCacheRouteChange(
    LinkCache *LC,
    const VirtualAddress Dest,
    uint PrevMetric,
    uint Metric,
    SourceRoute *SR,
    Link **Hops)
{
#if CHANGELOGS
    struct RouteChange *Record = &LC->RouteChangeLog[LC->NextRouteRecord];

    KeQuerySystemTime((LARGE_INTEGER *)&Record->TimeStamp);
    RtlCopyMemory(Record->Dest, Dest, sizeof(VirtualAddress));
    Record->Metric = Metric;
    Record->PrevMetric = PrevMetric;
    if (SR != NULL) {
        RtlCopyMemory(Record->Buffer, SR, sizeof(LQSROption) + SR->optDataLen);

        //
        // Copy the metrics too.
        //
        if (Hops != NULL) {
            SourceRoute UNALIGNED *Save =
                (SourceRoute UNALIGNED *) Record->Buffer;
            uint NumHops = SOURCE_ROUTE_HOPS(SR->optDataLen);
            uint hop;

            for (hop = 1; hop < NumHops; hop++)
                Save->hopList[hop].Metric = Hops[NumHops - hop - 1]->Metric;
        }

    }
    else {
        //
        // Use an optionType of zero to indicate the absence of a route.
        //
        ((LQSROption UNALIGNED *)Record->Buffer)->optionType = 0;
    }

    LC->NextRouteRecord = (LC->NextRouteRecord + 1) % NUM_ROUTECHANGE_RECORDS;
#else
    UNREFERENCED_PARAMETER(LC);
    UNREFERENCED_PARAMETER(Dest);
    UNREFERENCED_PARAMETER(Metric);
    UNREFERENCED_PARAMETER(PrevMetric);
    UNREFERENCED_PARAMETER(SR);
    UNREFERENCED_PARAMETER(Hops);
#endif
}

//* LinkCacheRouteUsage
//
//  Records the usage of a route for a packet to the destination.
//
//  Called with the link cache locked.
//
#if !CHANGELOGS
__inline
#endif
static void
LinkCacheRouteUsage(
    LinkCache *LC,
    int dnode,
    InternalSourceRoute *SR)    // May be NULL, meaning no route.
{
#if CHANGELOGS
    CacheNode *Dest = &LC->nodes[dnode];
    RouteUsage *RU;
    uint NumHops;
    uint hop;

    //
    // Do we have a current RouteUsage record?
    //
    RU = Dest->CurrentRU;
    if (RU == NULL) {
        //
        // We need to find or create a RouteUsage record.
        //

        if (SR == NULL)
            NumHops = 0; // NB: We can use SR->opt.hopList.
        else
            NumHops = SOURCE_ROUTE_HOPS(SR->opt.optDataLen);

        //
        // Look for an existing RouteUsage record.
        //
        for (RU = Dest->History; ; RU = RU->Next) {
            if (RU == NULL) {
                //
                // Create a new RouteUsage record.
                //
                RU = ExAllocatePool(NonPagedPool,
                                sizeof *RU + NumHops * sizeof(SRAddr));
                if (RU == NULL)
                    return;

                RU->Next = Dest->History;
                Dest->History = RU;

                RU->Usage = 0;
                RU->Hops = NumHops;
                RtlCopyMemory(RU->Route, SR->opt.hopList,
                              NumHops * sizeof(SRAddr));

                //
                // Copy the current metrics for this route.
                // It is possible for Dest->Hops to be NULL
                // (if the allocation in LinkCacheFillSR failed)
                // but otherwise it should match SR.
                //
                if (Dest->Hops != NULL) {
                    ASSERT(SR == NULL ||
                           SR->opt.optDataLen == Dest->SR->optDataLen);

                    for (hop = 1; hop < NumHops; hop++)
                        RU->Route[hop].Metric =
                            Dest->Hops[NumHops - hop - 1]->Metric;
                }
                break;
            }

            //
            // Do we have an existing RouteUsage record?
            // NB: The RouteUsage record has non-zero metrics,
            // while the source route has zero metrics,
            // so RtlEqualMemory does not work.
            //
            if (RU->Hops == NumHops) {
                boolint HaveExisting = TRUE;

                for (hop = 0; hop < NumHops; hop++) {
                    if (! VirtualAddressEqual(RU->Route[hop].addr,
                                              SR->opt.hopList[hop].addr) ||
                        (RU->Route[hop].inif != SR->opt.hopList[hop].inif) ||
                        (RU->Route[hop].outif != SR->opt.hopList[hop].outif)) {
                        HaveExisting = FALSE;
                        break;
                    }
                }

                if (HaveExisting)
                    break;
            }
        }

        //
        // Remember this record for next time.
        //
        Dest->CurrentRU = RU;
    }

    //
    // Record the usage of this route.
    //
    RU->Usage++;

#else
    UNREFERENCED_PARAMETER(LC);
    UNREFERENCED_PARAMETER(dnode);
    UNREFERENCED_PARAMETER(SR);
#endif
}

//* LinkCacheAllocate
//
//  Allocates a new link cache data structure.
//
LinkCache *
LinkCacheAllocate(
    uint size,
    const uchar myaddr[SR_ADDR_LEN])
{
    LinkCache *ret;
    Time Now;

    ret = ExAllocatePool(NonPagedPool, sizeof(LinkCache));
    if (ret == NULL)
        goto fail;

    KeInitializeSpinLock(&ret->Lock);

    ASSERT(size > 1);

    ret->nodeCount = 1;
    ret->maxSize = size;

    ret->metric = ExAllocatePool(NonPagedPool, sizeof(uint)*size);
    ret->hops = ExAllocatePool(NonPagedPool, sizeof(uint)*size);
    ret->link = ExAllocatePool(NonPagedPool, sizeof(Link*)*size);
    ret->prev = ExAllocatePool(NonPagedPool, sizeof(uint)*size);
    ret->nodes = ExAllocatePool(NonPagedPool, sizeof(CacheNode)*size);

    if ((ret->metric == NULL) ||
        (ret->hops == NULL) ||
        (ret->link == NULL) ||
        (ret->prev == NULL) ||
        (ret->nodes == NULL))
        goto fail;

    ret->SmallestMetric = 0;
    ret->LargestMetric = (uint)-1;
    Now = KeQueryInterruptTime();
    ret->timeout = Now;

    // INVARIANT: nodes[0] MUST always refer to me.
    RtlCopyMemory(ret->nodes[0].address, myaddr, SR_ADDR_LEN);
    ret->nodes[0].AdjOut = NULL;
    ret->nodes[0].AdjIn = NULL;
    ret->nodes[0].SR = NULL;
    ret->nodes[0].Hops = NULL;
    ret->nodes[0].Metric = 0;
    ret->nodes[0].RouteChanges = 0;
#if CHANGELOGS
    ret->nodes[0].CurrentRU = NULL;
    ret->nodes[0].History = NULL;
#endif
    ret->metric[0] = 0;
    ret->hops[0] = 0;
    ret->prev[0] = MAXULONG;

    ret->CountAddLinkInvalidate = 0;
    ret->CountAddLinkInsignificant = 0;
    ret->CountRouteFlapDamp = 0;
    ret->CountRouteFlap = 0;

#if CHANGELOGS
    ret->NextLinkRecord = 0;
    RtlZeroMemory(ret->LinkChangeLog, sizeof ret->LinkChangeLog);

    ret->NextRouteRecord = 0;
    RtlZeroMemory(ret->RouteChangeLog, sizeof ret->RouteChangeLog);
#endif

    return ret;

fail:
    if (ret != NULL) {
        if (ret->metric != NULL)
            ExFreePool(ret->metric);
        if (ret->hops != NULL)
            ExFreePool(ret->hops);
        if (ret->prev != NULL)
            ExFreePool(ret->prev);
        if (ret->link != NULL)
            ExFreePool(ret->link);
        if (ret->nodes != NULL)
            ExFreePool(ret->nodes);
        ExFreePool(ret);
    }
    return NULL;
}

//* LinkCacheFree
//
//  Frees the link cache data structure for unload.
//
void
LinkCacheFree(MiniportAdapter *VA)
{
    LinkCache *LC = VA->LC;
    Link *tmp;
#if CHANGELOGS
    RouteUsage *RU;
#endif
    uint i;

    //
    // Free all the links and cached routes.
    //
    for (i = 0; i < LC->nodeCount; i++) {
        while (LC->nodes[i].AdjOut != NULL) {
            tmp = LC->nodes[i].AdjOut;
            LC->nodes[i].AdjOut = LC->nodes[i].AdjOut->NextOut;
            ExFreePool(tmp);
        }

        if (LC->nodes[i].SR != NULL) {
            // This also frees LC->nodes[i].Hops.
            ExFreePool(LC->nodes[i].SR);
        }

#if CHANGELOGS
        while ((RU = LC->nodes[i].History) != NULL) {
            LC->nodes[i].History = RU->Next;
            ExFreePool(RU);
        }
#endif
    }

    ExFreePool(LC->metric);
    ExFreePool(LC->hops);
    ExFreePool(LC->prev);
    ExFreePool(LC->link);
    ExFreePool(LC->nodes);
    ExFreePool(LC);
}

//* LinkCacheResetStatistics
//
//  Resets all counters and statistics gathering for the link cache.
//
void
LinkCacheResetStatistics(MiniportAdapter *VA)
{
    LinkCache *LC = VA->LC;
    Link *Adj;
#if CHANGELOGS
    RouteUsage *RU;
    uint i;
#endif
    KIRQL OldIrql;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    LC->CountRouteFlapDamp = 0;
    LC->CountRouteFlap = 0;
    LC->CountAddLinkInvalidate = 0;
    LC->CountAddLinkInsignificant = 0;
    for (Adj = LC->nodes[0].AdjOut; Adj != NULL; Adj = Adj->NextOut)
        Adj->Failures = 0;
    for (i = 0; i < LC->nodeCount; i++) {
        LC->nodes[i].RouteChanges = 0;
#if CHANGELOGS
        while ((RU = LC->nodes[i].History) != NULL) {
            LC->nodes[i].History = RU->Next;
            ExFreePool(RU);
        }
        LC->nodes[i].CurrentRU = NULL;
#endif
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);
}

//* LinkCacheFlush
//
//  Deletes all cached link information.
//
void
LinkCacheFlush(MiniportAdapter *VA)
{
    LinkCache *LC = VA->LC;
    Link *tmp;
#if CHANGELOGS
    RouteUsage *RU;
#endif
    uint i;
    KIRQL OldIrql;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);

    //
    // Free all the links and cached routes.
    //
    for (i = 0; i < LC->nodeCount; i++) {
        while (LC->nodes[i].AdjOut != NULL) {
            tmp = LC->nodes[i].AdjOut;
            LinkCacheLinkChange(LC,
                                LC->nodes[i].address,
                                LC->nodes[tmp->targetIndex].address,
                                tmp->inif, tmp->outif,
                                (uint)-1,
                                LINK_STATE_CHANGE_DELETE_MANUAL);
            LC->nodes[i].AdjOut = LC->nodes[i].AdjOut->NextOut;
            ExFreePool(tmp);
        }

        if (LC->nodes[i].SR != NULL) {
            // This also frees LC->nodes[i].Hops.
            ExFreePool(LC->nodes[i].SR);
            LinkCacheRouteChange(LC, LC->nodes[i].address,
                                 LC->nodes[i].Metric, (uint)-1,
                                 NULL, NULL);
        }

#if CHANGELOGS
        while ((RU = LC->nodes[i].History) != NULL) {
            LC->nodes[i].History = RU->Next;
            ExFreePool(RU);
        }
#endif
    }

    //
    // Reset to just me.
    //
    LC->nodes[0].AdjIn = NULL;
    LC->timeout = KeQueryInterruptTime();
    LC->nodeCount = 1;
    LC->SmallestMetric = 0;
    LC->LargestMetric = (uint)-1;

    KeReleaseSpinLock(&LC->Lock, OldIrql);
}

//* LinkCacheMyDegree
//
//  Returns our own link degree. That is, the number of links
//  from node 0 to other nodes.
//
uint
LinkCacheMyDegree(MiniportAdapter *VA)
{
    LinkCache *LC = VA->LC;
    Link *link;
    uint Degree = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    for (link = LC->nodes[0].AdjOut; link != NULL; link = link->NextOut)
        Degree++;
    KeReleaseSpinLock(&LC->Lock, OldIrql);

    return Degree;
}

//* LinkCacheUpdateRR
//
//  Called when processing a route request.
//  Updates the route request option in the packet with the metric
//  we have for the link the previous node used to send this to us.
//
//  REVIEW - Should more of the Route Request handling code in ReceiveSRPacket
//  be moved over here?  Vice-versa?
//
void
LinkCacheUpdateRR(
    MiniportAdapter *VA,
    InternalRouteRequest *RR)
{
    LinkCache *LC = VA->LC;
    uint Hops;
    int PrevNode;
    Link *Link;
    uint Metric;
    KIRQL OldIrql;

    Hops = ROUTE_REQUEST_HOPS(RR->opt.optDataLen);
    ASSERT((Hops <= MAX_SR_LEN) && (Hops > 1));

    ASSERT(VirtualAddressEqual(RR->opt.hopList[Hops - 1].addr,
                               LC->nodes[0].address));

    //
    // If we fail to find the link used to reach us, we'll mark the option
    // with a zero metric. The link obviously does exist because it
    // is in the Route Request, but we do not know the metric.
    //
    Metric = 0;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);

    //
    // Find the node which sent us this.
    //
    PrevNode = findNode(LC, RR->opt.hopList[Hops - 2].addr, FALSE);
    if (PrevNode > 0) {
        //
        // Find the link from that node which got the packet here.
        //
        for (Link = LC->nodes[PrevNode].AdjOut; Link != NULL; Link = Link->NextOut) {
            if ((Link->targetIndex == 0) &&
                (Link->outif == RR->opt.hopList[Hops - 2].outif) &&
                (Link->inif == RR->opt.hopList[Hops - 1].inif)) {
                //
                // We'll update the option with the metric of this link.
                // Unless we think the link is broken, which is stale info.
                //
                if (! (*VA->IsInfinite)(Link->Metric))
                    Metric = Link->Metric;
                break;
            }
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);

    //
    // Update the option with what we know about the reverse link.
    //
    RR->opt.hopList[Hops - 1].Metric = Metric;
}

//* LinkCacheUseSR
//
//  Called when sending/forwarding a packet with a source route.
//  Updates our usage counter for the next link AND
//  updates the Metric in the source route for the next link.
//
//  Also checks if sending this packet would overflow
//  the transmit queue of the outgoing interface.
//  Returns FALSE to indicate the packet should be dropped.
//
boolint
LinkCacheUseSR(
    MiniportAdapter *VA,
    SRPacket *SRP)
{
    LinkCache *LC = VA->LC;
    InternalSourceRoute *SR = SRP->sr;
    Link *Link;
    int DNode;
    KIRQL OldIrql;
    uint NumHops;
    uint Hop;
    boolint QueueFull;

    NumHops = SOURCE_ROUTE_HOPS(SR->opt.optDataLen);
    ASSERT(NumHops <= MAX_SR_LEN);

    Hop = NumHops - SR->opt.segmentsLeft;
    ASSERT((Hop < NumHops) && (Hop != 0));

    ASSERT(VirtualAddressEqual(SR->opt.hopList[Hop - 1].addr,
                               VA->LC->nodes[0].address));

    //
    // We only want to check for packets that are carrying real payload.
    //
    if (SRP->PacketLength != SRP->PayloadOffset)
        QueueFull = ProtocolQueueFull(VA, SR->opt.hopList[Hop - 1].outif);
    else
        QueueFull = FALSE;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    DNode = findNode(LC, SR->opt.hopList[Hop].addr, FALSE);
    if (DNode > 0) {

        //
        // Search our link cache for this link.
        //
        for (Link = LC->nodes[0].AdjOut; Link != NULL; Link = Link->NextOut) {
            if ((Link->targetIndex == DNode) &&
                (Link->outif == SR->opt.hopList[Hop - 1].outif) &&
                (Link->inif == SR->opt.hopList[Hop].inif)) {
                //
                // Found link (should match at most one).
                //

                if (! SR->opt.staticRoute)
                    Link->Usage++;

                if (QueueFull)
                    Link->QueueDrops++;

                //
                // We'll update the option with the metric of this link.
                // Unless we think the link is broken, which is stale info.
                //
                if (! (*VA->IsInfinite)(Link->Metric))
                    SR->opt.hopList[Hop].Metric = Link->Metric;
                break;
            }
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);

    return !QueueFull;
}

//* LinkCacheRememberLinks
//
//  Stores references to the links in a route to the destination.
//
//  NB: The links are stored backwards, from last to first.
//
//  Called with the link cache locked.
//
void
LinkCacheRememberLinks(
    LinkCache *LC,
    int dnode,
    Link **Hops)
{
    Link *Hop;

    while (dnode != 0) {
        Hop = LC->link[dnode];
        Hop->RefCnt++;
        *Hops++ = Hop;
        dnode = LC->prev[dnode];
        ASSERT(dnode != (uint)-1);
    }
}

//* LinkCacheReleaseLinks
//
//  Releases references to the links in the array.
//
//  Called with the link cache locked.
//
void
LinkCacheReleaseLinks(
    Link **Hops,
    uint NumHops)
{
    uint i;

    for (i = 0; i < NumHops; i++)
        Hops[i]->RefCnt--;
}

//* LinkCacheFlapDamp
//
//  Returns TRUE if the old route is good enough.
//
boolint
LinkCacheFlapDamp(
    uint OldMetric,     // Current metric of the old route.
    uint NewMetric,     // Metric of the current best route.
    Time Age,           // Lifetime of the old route.
    uint DampingFactor) // Damping Factor. 
{
    uint FudgeFactor;
    uint FudgedMetric;

    //
    // If the old route is broken, we should not use it.
    //
    if (OldMetric == (uint)-1)
        return FALSE;

    //
    // The old route can actually look better, if its link metrics
    // have improved since dijkstra was run.
    //
    if (OldMetric <= NewMetric)
        return TRUE;

    //
    // Calculate a "fudge factor". Note that if DampingFactor
    // is 0, we do not do any damping.
    // NB: We check for overflow below.
    //
    if (Age < 100 * MILLISECOND)
        FudgeFactor = (DampingFactor * NewMetric) / 8;
    else if (Age < SECOND)
        FudgeFactor = (DampingFactor * NewMetric) / 16;
    else if (Age < 10 * SECOND)
        FudgeFactor = (DampingFactor * NewMetric) / 32;
    else if (Age < 100 * SECOND)
        FudgeFactor = (DampingFactor * NewMetric) / 64;
    else
        FudgeFactor = 0; 

    FudgedMetric = NewMetric + FudgeFactor; 
    
    //
    // Switch to the new route if it is sufficiently better.
    // The first condition checks for overflow. 
    //
    if ((NewMetric <= FudgedMetric) && (FudgedMetric <= OldMetric))
        return FALSE;

    //
    // The new route is better, but not enough better
    // to warrant switching to it.
    //
    KdPrint(("MCL!LinkCacheFlapDamp: old %u new %u\n",
             OldMetric, NewMetric));
    return TRUE;
}

//* LinkCacheFillSR
//
//  Initializes SR with a route to the destination.
//  Always initializes SR->free to NULL.
//  The remaining SR fields are only modified if we succeed.
//
//  Return codes:
//      NDIS_STATUS_NO_ROUTE_TO_DESTINATION     No usable route.
//      NDIS_STATUS_RESOURCES                   Resource shortage.
//      NDIS_STATUS_SUCCESS
//
NDIS_STATUS
LinkCacheFillSR(
    MiniportAdapter *VA,
    const VirtualAddress Dest,
    InternalSourceRoute *SR)
{
    LinkCache *LC = VA->LC;
    SourceRoute *CachedSR;
    int dnode;
    uint NumHops;
    int curr;
    KIRQL OldIrql;
    Time Now;
    NDIS_STATUS ret;

    //
    // We must update SR->next even when we fail, for SRPacketFree.
    //
    SR->next = NULL;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    Now = KeQueryInterruptTime();

    dnode = findNode(LC, Dest, FALSE);

    //
    // We do not route to ourself.
    // Loopback is handled by layer 3.
    //
    if (dnode <= 0) {
        ret = NDIS_STATUS_NO_ROUTE_TO_DESTINATION;
        goto Return; // No route change log: CachedSR is not initialized.
    }

    //
    // Ensure that our route computation is current.
    // NB: This must be <= not < because
    // KeQueryInterruptTime might return the same value twice.
    //
    if (LC->timeout <= Now) {
        //
        // Recompute shortest path routes.
        //
        if (! dijkstra(VA, Now)) {
            ret = NDIS_STATUS_RESOURCES;
            goto Return; // No route change log: CachedSR is not initialized.
        }
    }

    //
    // Check for a cached route.
    //
    CachedSR = LC->nodes[dnode].SR;
    if (CachedSR != NULL) {
        //
        // We can use the cached route if it is static
        // or if its path metric is current.
        //
        if (CachedSR->staticRoute ||
            (LC->nodes[dnode].Metric != 0)) {

        UseCachedRoute:
            RtlCopyMemory(&SR->opt, CachedSR,
                          sizeof(LQSROption) + CachedSR->optDataLen);
            LinkCacheRouteUsage(LC, dnode, SR);
            ret = NDIS_STATUS_SUCCESS;
            goto Return; // No route change log in this case.
        }

        //
        // Calculate a current path metric
        // and check if we should still use this route.
        //
        LC->nodes[dnode].Metric =
            (*VA->PathMetric)(VA, LC->nodes[dnode].Hops,
                              SOURCE_ROUTE_HOPS(CachedSR->optDataLen) - 1);

        if (LinkCacheFlapDamp(LC->nodes[dnode].Metric,
                              LC->metric[dnode],
                              Now - LC->nodes[dnode].FirstUsage,
                              VA->RouteFlapDampingFactor)) {
            LC->CountRouteFlapDamp++;
            goto UseCachedRoute;
        }
        LC->CountRouteFlap++;
    }

    //
    // NB: The hop count will be (uint)-1 if there is no route.
    // We also reject routes that are too long.
    //
    if (LC->hops[dnode] >= MAX_SR_LEN) {
        //
        // We have no usable route to the destination.
        //
        LinkCacheRouteUsage(LC, dnode, NULL);
        if (CachedSR != NULL) {
            //
            // We have an existing source route.
            // Delete it and log the change.
            //
            LinkCacheReleaseLinks(LC->nodes[dnode].Hops,
                                  SOURCE_ROUTE_HOPS(CachedSR->optDataLen) - 1);
            ExFreePool(CachedSR);
            LC->nodes[dnode].SR = NULL;
            LC->nodes[dnode].Hops = NULL;
            LinkCacheRouteChange(LC, Dest,
                                 LC->nodes[dnode].Metric, (uint)-1,
                                 NULL, NULL);
            LC->nodes[dnode].RouteChanges++;
        }
        LC->nodes[dnode].Metric = (uint)-1;
        ret = NDIS_STATUS_NO_ROUTE_TO_DESTINATION;
        goto Return;
    }
    NumHops = LC->hops[dnode] + 1; // Add one to get number of nodes.

    SR->opt.optionType = LQSR_OPTION_TYPE_SOURCERT;
    SR->opt.optDataLen = SOURCE_ROUTE_LEN(NumHops);
    SR->opt.reservedField = 0;
    SR->opt.staticRoute = 0;
    SR->opt.salvageCount = 0;
    SR->opt.segmentsLeft = (uchar) (NumHops - 1);

    SR->opt.hopList[NumHops-1].outif = 0;
    SR->opt.hopList[0].inif = 0;

    curr = -1;
    do {
        if (curr == -1)
            curr = dnode;
        else
            curr = LC->prev[curr];
        
        ASSERT(curr != (uint)-1); 

        NumHops--;
        RtlCopyMemory(SR->opt.hopList[NumHops].addr, 
                      LC->nodes[curr].address, SR_ADDR_LEN);
        SR->opt.hopList[NumHops].Metric = 0;
        if (NumHops != 0) {
            SR->opt.hopList[NumHops].inif = LC->link[curr]->inif;
            SR->opt.hopList[NumHops-1].outif = LC->link[curr]->outif;
        }
    } while (curr);

    if (CachedSR != NULL) {
        //
        // We have an existing source route.
        // If this one is different, cache it and log it.
        //
        if ((CachedSR->optDataLen != SR->opt.optDataLen) ||
            ! RtlEqualMemory(CachedSR, &SR->opt,
                             sizeof(LQSROption) + SR->opt.optDataLen)) {

            LinkCacheReleaseLinks(LC->nodes[dnode].Hops,
                                  SOURCE_ROUTE_HOPS(CachedSR->optDataLen) - 1);
            // This also frees LC->nodes[dnode].Hops.
            ExFreePool(CachedSR);
            goto CacheAndLogSourceRoute;
        }
    }
    else {
        uint SRSize;
        Link **Hops;

        //
        // We do not have an existing source route.
        // We cache this new source route and log it.
        //
    CacheAndLogSourceRoute:
        NumHops = LC->hops[dnode]; // We want number of links, not nodes.
        SRSize = ROUND_UP_COUNT(sizeof(LQSROption) + SR->opt.optDataLen,
                                sizeof *Hops);
        CachedSR = ExAllocatePool(NonPagedPool,
                                  SRSize + NumHops * sizeof *Hops);
        if (CachedSR != NULL) {
            RtlCopyMemory(CachedSR, &SR->opt,
                          sizeof(LQSROption) + SR->opt.optDataLen);

            Hops = (Link **) ((uchar *)CachedSR + SRSize);
            LinkCacheRememberLinks(LC, dnode, Hops);
        }
        else
            Hops = NULL;

        LC->nodes[dnode].SR = CachedSR;
        LC->nodes[dnode].Hops = Hops;
        LC->nodes[dnode].FirstUsage = Now;
        LinkCacheRouteChange(LC, Dest,
                             LC->nodes[dnode].Metric, LC->metric[dnode],
                             &SR->opt, Hops);
        LC->nodes[dnode].Metric = LC->metric[dnode];
        LC->nodes[dnode].RouteChanges++;
    }

    LinkCacheRouteUsage(LC, dnode, SR);
    ret = NDIS_STATUS_SUCCESS;

Return:
    KeReleaseSpinLock(&LC->Lock, OldIrql);
    return ret;
}

//* LinkCacheGetSR
//
//  Initializes SR with a route to the destination,
//  including the link metrics. This is for IoQuerySourceRoute only,
//  not for actually sending packets.
//
//  Return codes:
//      NDIS_STATUS_NO_ROUTE_TO_DESTINATION     No usable route.
//      NDIS_STATUS_RESOURCES                   Resource shortage.
//      NDIS_STATUS_SUCCESS
//
NDIS_STATUS
LinkCacheGetSR(
    MiniportAdapter *VA,
    const VirtualAddress Dest,
    InternalSourceRoute *SR)
{
    LinkCache *LC = VA->LC;
    SourceRoute *CachedSR;
    int dnode;
    uint NumHops;
    int curr;
    KIRQL OldIrql;
    Time Now;
    NDIS_STATUS ret;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    Now = KeQueryInterruptTime();

    dnode = findNode(LC, Dest, FALSE);

    //
    // We do not route to ourself.
    // Loopback is handled by layer 3.
    //
    if (dnode <= 0) {
        ret = NDIS_STATUS_NO_ROUTE_TO_DESTINATION;
        goto Return;
    }

    //
    // Ensure that our route computation is current.
    // NB: This must be <= not < because
    // KeQueryInterruptTime might return the same value twice.
    //
    if (LC->timeout <= Now) {
        //
        // Recompute shortest path routes.
        //
        if (! dijkstra(VA, Now)) {
            ret = NDIS_STATUS_RESOURCES;
            goto Return;
        }
    }

    //
    // Check for a cached route.
    //
    CachedSR = LC->nodes[dnode].SR;
    if (CachedSR != NULL) {
        //
        // We can use the cached route if it is static
        // or if its path metric is current.
        //
        if (CachedSR->staticRoute ||
            (LC->nodes[dnode].Metric != 0)) {

            RtlCopyMemory(&SR->opt, CachedSR,
                          sizeof(LQSROption) + CachedSR->optDataLen);

            if (LC->nodes[dnode].Hops != NULL) {
                //
                // Get the metric values for the cached route.
                //
                NumHops = SOURCE_ROUTE_HOPS(CachedSR->optDataLen);
                for (curr = 1; curr < (int)NumHops; curr++)
                    SR->opt.hopList[curr].Metric =
                        LC->nodes[dnode].Hops[NumHops - curr - 1]->Metric;
            }

            ret = NDIS_STATUS_SUCCESS;
            goto Return;
        }
    }

    //
    // NB: The hop count will be (uint)-1 if there is no route.
    // We also reject routes that are too long.
    //
    if (LC->hops[dnode] >= MAX_SR_LEN) {
        //
        // We have no usable route to the destination.
        //
        ret = NDIS_STATUS_NO_ROUTE_TO_DESTINATION;
        goto Return;
    }

    NumHops = LC->hops[dnode] + 1; // Add one to get number of nodes.

    SR->opt.optionType = LQSR_OPTION_TYPE_SOURCERT;
    SR->opt.optDataLen = SOURCE_ROUTE_LEN(NumHops);
    SR->opt.reservedField = 0;
    SR->opt.staticRoute = 0;
    SR->opt.salvageCount = 0;
    SR->opt.segmentsLeft = (uchar) (NumHops - 1);

    SR->opt.hopList[NumHops-1].outif = 0;
    SR->opt.hopList[0].inif = 0;

    curr = -1;
    do {
        if (curr == -1)
            curr = dnode;
        else
            curr = LC->prev[curr];

        NumHops--;
        RtlCopyMemory(SR->opt.hopList[NumHops].addr, 
                      LC->nodes[curr].address, SR_ADDR_LEN);
        if (NumHops != 0) {
            SR->opt.hopList[NumHops].Metric = LC->link[curr]->Metric;
            SR->opt.hopList[NumHops].inif = LC->link[curr]->inif;
            SR->opt.hopList[NumHops-1].outif = LC->link[curr]->outif;
        }
    } while (curr);
    ret = NDIS_STATUS_SUCCESS;

Return:
    KeReleaseSpinLock(&LC->Lock, OldIrql);
    return ret;
}

//* LinkCacheAddRoute
//
//  Adds a static route to the destination.
//
NDIS_STATUS
LinkCacheAddRoute(
    MiniportAdapter *VA,
    const VirtualAddress Dest,
    SourceRoute *SR)
{
    LinkCache *LC = VA->LC;
    KIRQL OldIrql;
    NDIS_STATUS Status;
    int dnode;

    ASSERT(SR->optionType == LQSR_OPTION_TYPE_SOURCERT);
    ASSERT(SR->staticRoute);
    ASSERT(SOURCE_ROUTE_HOPS(SR->optDataLen) <= MAX_SR_LEN);

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    dnode = findNode(LC, Dest, TRUE);
    if (dnode < 0) {
        //
        // Could not expand the link cache.
        //
        Status = NDIS_STATUS_RESOURCES;
    }
    else if (dnode == 0) {
        //
        // Can not specify a route to ourself.
        //
        Status = NDIS_STATUS_INVALID_ADDRESS;
    }
    else {
        CacheNode *node = &LC->nodes[dnode];

        //
        // Insert this source route.
        //
        if (node->SR != NULL) {
            if (node->Hops != NULL) {
                LinkCacheReleaseLinks(node->Hops,
                                      SOURCE_ROUTE_HOPS(node->SR->optDataLen) - 1);
                node->Hops = NULL;
            }
            // This also frees the old node->Hops.
            ExFreePool(node->SR);
        }
        node->SR = SR;
#if CHANGELOGS
        node->CurrentRU = NULL;
#endif
        Status = NDIS_STATUS_SUCCESS;

        //
        // Log the change.
        //
        LinkCacheRouteChange(LC, Dest, node->Metric, 0, SR, NULL);
        node->Metric = 0;
        node->RouteChanges++;
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);

    return Status;
}

//* LinkCacheCreateLI
//
//  Creates a LinkInfo option that represents the links from this node.
//  The option size can not exceed the specified MaxSize.
//
InternalLinkInfo *
LinkCacheCreateLI(
    MiniportAdapter *VA,
    uint MaxSize)
{
    LinkCache *LC = VA->LC;
    InternalLinkInfo *LI = NULL;
    Link *Link;
    uint NumLinks;
    KIRQL OldIrql;

    ASSERT(VA->MetricType != METRIC_TYPE_HOP);

    KeAcquireSpinLock(&LC->Lock, &OldIrql);

    //
    // Count the number of links from this node.
    // Stop counting if we'd exceed the amount of data that can be
    // represented in a linkinfo option.
    //
    NumLinks = 0;
    for (Link = LC->nodes[0].AdjOut; Link != NULL; Link = Link->NextOut) {
        if (LINKINFO_HOPS(LINKINFO_LEN(NumLinks + 1)) != NumLinks + 1) {
            //
            // We can not return all the links in one option.
            //
            InterlockedIncrement((PLONG)&VA->CountLinkInfoTooManyLinks);
            break;
        }
        NumLinks++;
    }

    //
    // Don't create a linkinfo option if we don't have any links.
    //
    if (NumLinks == 0)
        goto ErrorReturn;

    //
    // Is there room for all of these links in the packet?
    //
    if (sizeof(LQSROption) + LINKINFO_LEN(NumLinks) > MaxSize) {
        //
        // No, we can not return a complete option.
        //
        InterlockedIncrement((PLONG)&VA->CountLinkInfoTooLarge);

        //
        // Give up now if there isn't room for even one link.
        //
        if (MaxSize < sizeof(LQSROption) + LINKINFO_LEN(1))
            goto ErrorReturn;

        //
        // How many links will fit?
        //
        NumLinks = LINKINFO_HOPS(MaxSize - sizeof(LQSROption));
    }

    //
    // Allocate the Link Info option.
    //
    LI = ExAllocatePool(NonPagedPool, sizeof *LI + NumLinks * sizeof(SRAddr));
    if (LI != NULL) {
        SRAddr *SRA = LI->Opt.Links;

        //
        // Initialize the Link Info option.
        //
        LI->Next = NULL;
        LI->Opt.optionType = LQSR_OPTION_TYPE_LINKINFO;
        LI->Opt.optDataLen = LINKINFO_LEN(NumLinks);
        RtlCopyMemory(LI->Opt.From, LC->nodes[0].address, SR_ADDR_LEN);

        //
        // Traverse the links again, adding them to the option.
        // If they don't all fit, we just stop early.
        //
        for (Link = LC->nodes[0].AdjOut; NumLinks != 0; Link = Link->NextOut) {
            RtlCopyMemory(SRA->addr,
                      LC->nodes[Link->targetIndex].address, SR_ADDR_LEN);
            SRA->inif = Link->inif;
            SRA->outif = Link->outif;
            SRA->Metric = Link->Metric;

            NumLinks--;
            SRA++;
        }
    }

  ErrorReturn:
    KeReleaseSpinLock(&LC->Lock, OldIrql);
    return LI;
}

//* LinkCacheDeleteInterface
//
//  Deletes all links that use the interface.
//
void
LinkCacheDeleteInterface(
    MiniportAdapter *VA,
    LQSRIf IF)
{
    LinkCache *LC = VA->LC;
    Link **Prev, *This;
    uint i;
    KIRQL OldIrql;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    LC->timeout = KeQueryInterruptTime(); // Invalidate dijkstra.

    //
    // Delete links from this node.
    // Also resets the links' reference count.
    //
    Prev = &LC->nodes[0].AdjOut;
    while ((This = *Prev) != NULL) {
        //
        // Delete this link if originates from the interface.
        //
        if (This->outif == IF) {
            LinkCacheLinkChange(LC,
                                LC->nodes[0].address,
                                LC->nodes[This->targetIndex].address,
                                This->inif, This->outif,
                                (uint)-1,
                                LINK_STATE_CHANGE_DELETE_INTERFACE);

            *Prev = This->NextOut;
            if (This->NextOut != NULL)
                This->NextOut->PrevOut = Prev;

            *This->PrevIn = This->NextIn;
            if (This->NextIn != NULL)
                This->NextIn->PrevIn = This->PrevIn;

            ExFreePool(This);
        }
        else {
            This->RefCnt = 0;
            Prev = &This->NextOut;
        }
    }

    //
    // Delete links to this node.
    // Also resets the links' reference count.
    //
    for (i = 1; i < LC->nodeCount; i++) {
        CacheNode *node = &LC->nodes[i];

        Prev = &node->AdjOut;
        while ((This = *Prev) != NULL) {
            //
            // Delete this link if terminates at the interface.
            //
            if (This->inif == IF) {
                LinkCacheLinkChange(LC,
                                    node->address,
                                    LC->nodes[This->targetIndex].address,
                                    This->inif, This->outif,
                                    (uint)-1,
                                    LINK_STATE_CHANGE_DELETE_INTERFACE);

                *Prev = This->NextOut;
                if (This->NextOut != NULL)
                    This->NextOut->PrevOut = Prev;

                *This->PrevIn = This->NextIn;
                if (This->NextIn != NULL)
                    This->NextIn->PrevIn = This->PrevIn;

                ExFreePool(This);
            }
            else {
                This->RefCnt = 0;
                Prev = &This->NextOut;
            }
        }
    }

    //
    // Delete all cached routes.
    // NB: We already reset the link reference counts,
    // so LinkCacheReleaseLinks is not needed.
    //
    for (i = 1; i < LC->nodeCount; i++) {
        CacheNode *node = &LC->nodes[i];

        if (node->Hops != NULL) {
            ASSERT((node->SR != NULL) && ! node->SR->staticRoute);
            ExFreePool(node->SR); // This also frees node->Hops.
            node->Hops = NULL;
            node->SR = NULL;
        }
    }

    KeReleaseSpinLock(&LC->Lock, OldIrql);
}

//* LinkCacheAddLink
//
//  Add a link to the link cache, or if the link already exists,
//  updates the metric.
//
//  An infinite Metric value means the link is actually non-functional.
//  We keep state for non-working links, if they were recently working.
//
void
LinkCacheAddLink(
    MiniportAdapter *VA,
    const VirtualAddress From,
    const VirtualAddress To,
    LQSRIf InIf,
    LQSRIf OutIf,
    uint Metric,
    uint Reason)
{
    LinkCache *LC = VA->LC;
    uint OldMetric;
    uint NewMetric;
    uint MetricDiff;
    int SNode, DNode;
    Link *Link;
    Time Now;
    KIRQL OldIrql;

    if (! VA->Snooping &&
        (Reason != LINK_STATE_CHANGE_ADD_REPLY))
        return;

    //
    // First find the source and destination nodes in the cache.
    //
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    if (((SNode = findNode(LC, From, TRUE)) >= 0) &&
        ((DNode = findNode(LC, To, TRUE)) >= 0)) {
        Now = KeQueryInterruptTime();

        //
        // Search our link cache to see if we already have this link.
        //
        for (Link = LC->nodes[SNode].AdjOut;
             Link != NULL;
             Link = Link->NextOut) {
            if ((Link->targetIndex == DNode) &&
                (Link->outif == OutIf) &&
                (Link->inif == InIf)) {

                OldMetric = (*VA->ConvMetric)(Link->Metric);
                goto UpdateExisting;
            }
        }

        //
        // Create a cache entry for this new link.
        //

        Link = ExAllocatePool(NonPagedPool, sizeof *Link);
        if (Link != NULL) {

            Link->RefCnt = 0;

            Link->sourceIndex = SNode;
            Link->targetIndex = DNode;
            Link->outif = OutIf;
            Link->inif = InIf;

            Link->Usage = 0;
            Link->Failures = 0;
            Link->DropRatio = 0;
            Link->ArtificialDrops = 0;
            Link->QueueDrops = 0;
            (*VA->InitLinkMetric)(VA, SNode, Link, Now);
            Link->TimeStamp = Now;

            Link->PrevOut = &LC->nodes[SNode].AdjOut;
            Link->NextOut = LC->nodes[SNode].AdjOut;
            if (Link->NextOut != NULL)
                Link->NextOut->PrevOut = &Link->NextOut;
            LC->nodes[SNode].AdjOut = Link;

            Link->PrevIn = &LC->nodes[DNode].AdjIn;
            Link->NextIn = LC->nodes[DNode].AdjIn;
            if (Link->NextIn != NULL)
                Link->NextIn->PrevIn = &Link->NextIn;
            LC->nodes[DNode].AdjIn = Link;

            //
            // The link previously did not exist.
            //
            OldMetric = (uint)-1;

        UpdateExisting:
            if (Metric != 0) {
                if ((SNode == 0) && (VA->MetricType != METRIC_TYPE_HOP)) {
                    //
                    // This link is directly adjacent. We are directly
                    // monitoring the quality of this link, via probing
                    // and LinkCachePenalizeLink. Any other information
                    // will be stale so ignore it.
                    //
                }
                else {
                    //
                    // We have metric information, so update the metric.
                    //
                    Link->Metric = Metric;
                }
            }
            else {
                //
                // We do not have a metric, but we know the link exists.
                // If we previously believed the link did not exist,
                // then reinitialize to a default metric.
                //
                if ((*VA->IsInfinite)(Link->Metric)) {
                    (*VA->InitLinkMetric)(VA, SNode, Link, Now);
                }
            }

            //
            // This is a quick heuristic check of whether this
            // change is likely to affect dijkstra.
            // We assume the metric change affects dijskstra
            // if the change is large enough to be meaningful AND
            // this link is likely to be part of either the old
            // or new shortest-path tree.
            // NB: It is critical that for the HOP metric,
            // a change from -1 to 1 or vice-versa invalidates dijkstra.
            //
            NewMetric = (*VA->ConvMetric)(Link->Metric);
            if (OldMetric < NewMetric)
                MetricDiff = NewMetric - OldMetric;
            else
                MetricDiff = OldMetric - NewMetric;

            if ((MetricDiff > LC->SmallestMetric) &&
                ((NewMetric <= LC->LargestMetric) ||
                 (OldMetric <= LC->LargestMetric))) {

                LC->CountAddLinkInvalidate++;

                //
                // Invalidate the dijkstra computation, because
                // we've significantly changed the metric of a link.
                //
                LC->timeout = Now;

                //
                // Record the change.
                //
                LinkCacheLinkChange(LC,
                                    From, To,
                                    InIf, OutIf,
                                    Metric, Reason);
            }
            else {
                //
                // This was not a significant change.
                // However the dijkstra has a max timeout
                // of CACHE_TIMEOUT (one second) so
                // it will run again fairly soon anyway.
                //
                LC->CountAddLinkInsignificant++;
            }

            //
            // If Metric is not infinite (so the link exists)
            // update the TimeStamp to keep this link structure in existence.
            //
            if (! (*VA->IsInfinite)(Link->Metric))
                Link->TimeStamp = Now;
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);
}


//* LinkCacheControlLink
//
//  Adjust link attributes.
//
NDIS_STATUS
LinkCacheControlLink(
    MiniportAdapter *VA,
    const VirtualAddress From,
    const VirtualAddress To,
    LQSRIf InIf,
    LQSRIf OutIf,
    uint DropRatio)
{
    LinkCache *LC = VA->LC;
    int SNode, DNode;
    Link *Link;
    NDIS_STATUS Status = STATUS_INVALID_PARAMETER;
    KIRQL OldIrql;

    //
    // First find the source and destination nodes in the cache.
    //
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    if (((SNode = findNode(LC, From, TRUE)) >= 0) &&
        ((DNode = findNode(LC, To, TRUE)) >= 0)) {

        //
        // Search our link cache for this link.
        //
        for (Link = LC->nodes[SNode].AdjOut;
             Link != NULL;
             Link = Link->NextOut) {
            if ((Link->targetIndex == DNode) &&
                (Link->outif == OutIf) &&
                (Link->inif == InIf)) {

                //
                // Found the link.
                //
                Link->DropRatio = DropRatio;
                Status = STATUS_SUCCESS;
                break;
            }
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);
    return Status;
}

//* LinkCachePenalizeLink
//
//  Penalizes a link's metric because the link failed to send a packet.
//
void
LinkCachePenalizeLink(
    MiniportAdapter *VA,
    const VirtualAddress To,
    LQSRIf InIf,
    LQSRIf OutIf)
{
    LinkCache *LC = VA->LC;
    int DNode;
    Link *Adj;
    KIRQL OldIrql;

    //
    // First find the destination node in the cache.
    //
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    if ((DNode = findNode(LC, To, FALSE)) >= 0) {

        //
        // Next find the link from us to the destination.
        // It might not exist.
        //
        for (Adj = LC->nodes[0].AdjOut; Adj != NULL; Adj = Adj->NextOut) {
            if ((Adj->targetIndex == DNode) &&
                (Adj->outif == OutIf) &&
                (Adj->inif == InIf)) {

                //
                // Penalize the link.
                //
                switch (VA->MetricType) {
                case METRIC_TYPE_HOP:
                    Adj->Metric = (uint)-1;
                    break;
                case METRIC_TYPE_RTT:
                    RttPenalize(VA, Adj);
                    break;
                case METRIC_TYPE_PKTPAIR:
                    PktPairPenalize(VA, Adj);
                    break;
                case METRIC_TYPE_ETX:
                    EtxPenalize(VA, Adj);
                    break;
                case METRIC_TYPE_WCETT:
                    WcettPenalize(VA, Adj);
                    break;
                default:
                    ASSERT(!"invalid MetricType");
                }

                //
                // Log the penalty to this link.
                //
                Adj->Failures++;
                LinkCacheLinkChange(LC,
                                    VA->Address, To,
                                    InIf, OutIf,
                                    (uint)-1,
                                    LINK_STATE_CHANGE_PENALIZED);

                //
                // Ensure that dijkstra runs again immediately
                // to recalculate routes.
                //
                LC->timeout = KeQueryInterruptTime();
                break;
            }
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);
}


//* LinkCacheCountLinkUse
//
//  Increments the Usage value on a given link.
//
void
LinkCacheCountLinkUse(
    MiniportAdapter *VA,
    const VirtualAddress From,
    const VirtualAddress To,
    LQSRIf InIf,
    LQSRIf OutIf)
{
    LinkCache *LC = VA->LC;
    int SNode, DNode;
    Link *Link;
    KIRQL OldIrql;

    //
    // Find indices of source and destination 
    // in our link cache. 
    //
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    if (((SNode = findNode(LC, From, FALSE)) >= 0) &&
        ((DNode = findNode(LC, To, FALSE)) >= 0)) {

        //
        // Search our link cache for this link.
        //
        for (Link = LC->nodes[SNode].AdjOut; Link != NULL; Link = Link->NextOut) {
            if ((Link->targetIndex == DNode) &&
                (Link->outif == OutIf) &&
                (Link->inif == InIf)) {

                //
                // Found link (should match at most one).
                // Increment usage count.
                //
                Link->Usage++;
                break;
            }
        }
    }

    KeReleaseSpinLock(&LC->Lock, OldIrql);
}


//* LinkCacheCheckForDrop
//
//  See whether we should artificially drop a packet traversing this link.
//
boolint
LinkCacheCheckForDrop(
    MiniportAdapter *VA,
    const VirtualAddress From,
    const VirtualAddress To,
    LQSRIf InIf,
    LQSRIf OutIf)
{
    LinkCache *LC = VA->LC;
    int SNode, DNode;
    Link *Link;
    boolint Drop = FALSE;
    KIRQL OldIrql;

    //
    // Find indices of source and destination 
    // in our link cache. 
    //
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    if (((SNode = findNode(LC, From, FALSE)) >= 0) &&
        ((DNode = findNode(LC, To, FALSE)) >= 0)) {

        //
        // Search our link cache for this link.
        //
        for (Link = LC->nodes[SNode].AdjOut; Link != NULL; Link = Link->NextOut) {
            if ((Link->targetIndex == DNode) &&
                (Link->outif == OutIf) &&
                (Link->inif == InIf)) {
                uint Random;

                //
                // Found link (should match at most one).
                // Check against random drop ratio.
                //
                if (Link->DropRatio) {
                    GetRandom((uchar *)&Random, sizeof Random);
                    if (Link->DropRatio > Random) {
                        Drop = TRUE;
                        Link->ArtificialDrops++;
                    }
                }
                break;
            }
        }
    }

    KeReleaseSpinLock(&LC->Lock, OldIrql);

    return Drop;
}


//* LinkCacheLookupMetric
//
//  Returns the current metric value for the specified link,
//  or zero if we do not have that information.
//
uint
LinkCacheLookupMetric(
    MiniportAdapter *VA,
    const VirtualAddress From,
    const VirtualAddress To,
    LQSRIf InIf,
    LQSRIf OutIf)
{
    LinkCache *LC = VA->LC;
    int SNode, DNode;
    Link *Link;
    KIRQL OldIrql;
    uint Metric = 0;

    //
    // Find indices of source and destination 
    // in our link cache. 
    //
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    if (((SNode = findNode(LC, From, FALSE)) >= 0) &&
        ((DNode = findNode(LC, To, FALSE)) >= 0)) {

        //
        // Search our link cache for this link.
        //
        for (Link = LC->nodes[SNode].AdjOut; Link != NULL; Link = Link->NextOut) {
            if ((Link->targetIndex == DNode) &&
                (Link->outif == OutIf) &&
                (Link->inif == InIf)) {

                //
                // Found link (should match at most one).
                //
                Metric = Link->Metric;
                break;
            }
        }
    }

    KeReleaseSpinLock(&LC->Lock, OldIrql);
    return Metric;
}


//////////////////////////////////////////////////////////////////////////////
//                 HELPER FUNCTIONS FOR LINKCACHE FUNCTIONS                 //
//////////////////////////////////////////////////////////////////////////////

//* findNode
//
//  Find a node index in the link cache.
//  Returns the index if found, or -1 if not.
//  If Force, attempts to create a new node.
//
//  Must be called while holding LC->Lock.
//
static int
findNode(
    LinkCache *LC,
    const VirtualAddress addr,
    boolint Force)
{
    uint i;

    for (i = 0; i < LC->nodeCount; i++)
        if (VirtualAddressEqual(LC->nodes[i].address, addr))
            return i;

    if (! Force)
        return -1;

    if (i == LC->maxSize) {
        uint maxSize;
        uint *metric;
        uint *hops;
        Link **link;
        uint *prev;
        CacheNode *nodes;
        uint i;

        //
        // Calculate a new size for the link cache.
        // Growing by a fixed percentage means we can
        // reach steady-state in log time instead of linear time.
        //
        maxSize = (LC->maxSize * 5) / 4;
        maxSize = max(maxSize, LC->maxSize + 1);

        //
        // Allocate larger arrays.
        //
        metric = ExAllocatePool(NonPagedPool, sizeof *metric * maxSize);
        if (metric == NULL)
            return -1;
        hops = ExAllocatePool(NonPagedPool, sizeof *hops * maxSize);
        if (hops == NULL)
            return -1;
        link = ExAllocatePool(NonPagedPool, sizeof *link * maxSize);
        if (link == NULL)
            return -1;
        prev = ExAllocatePool(NonPagedPool, sizeof *prev * maxSize);
        if (prev == NULL)
            return -1;
        nodes = ExAllocatePool(NonPagedPool, sizeof *nodes * maxSize);
        if (nodes == NULL)
            return -1;

        //
        // Copy to the new arrays.
        //
        RtlCopyMemory(metric, LC->metric, sizeof *metric * LC->maxSize);
        RtlCopyMemory(hops, LC->hops, sizeof *hops * LC->maxSize);
        RtlCopyMemory(link, LC->link, sizeof *link * LC->maxSize);
        RtlCopyMemory(prev, LC->prev, sizeof *prev * LC->maxSize);
        RtlCopyMemory(nodes, LC->nodes, sizeof *nodes * LC->maxSize);

        //
        // Fix the PrevOut/PrevIn links.
        //
        for (i = 0; i < LC->nodeCount; i++) {
            if (nodes[i].AdjOut != NULL)
                nodes[i].AdjOut->PrevOut = &nodes[i].AdjOut;
            if (nodes[i].AdjIn != NULL)
                nodes[i].AdjIn->PrevIn = &nodes[i].AdjIn;
        }

        //
        // Delete the old arrays.
        //
        ExFreePool(LC->metric);
        ExFreePool(LC->hops);
        ExFreePool(LC->link);
        ExFreePool(LC->prev);
        ExFreePool(LC->nodes);

        //
        // And update the link cache.
        //
        LC->metric = metric;
        LC->hops = hops;
        LC->link = link;
        LC->prev = prev;
        LC->nodes = nodes;
        LC->maxSize = maxSize;
    }

    //
    // Add a new node.
    //
    LC->nodeCount++;
    LC->nodes[i].AdjOut = NULL;
    LC->nodes[i].AdjIn = NULL;
    LC->nodes[i].SR = NULL;
    LC->nodes[i].Hops = NULL;
    LC->nodes[i].Metric = 0;
    LC->nodes[i].RouteChanges = 0;
#if CHANGELOGS
    LC->nodes[i].CurrentRU = NULL;
    LC->nodes[i].History = NULL;
#endif
    LC->metric[i] = MAXULONG;
    LC->hops[i] = MAXULONG;
    LC->prev[i] = MAXULONG;

    RtlCopyMemory(LC->nodes[i].address, addr, SR_ADDR_LEN);
    return i;
}

//////////////////////////////////////////////////////////////////////////////
//                DIJKSTRA'S ALGORITHM AND HELPER FUNCTIONS                 //
//////////////////////////////////////////////////////////////////////////////

#define HEAP_LEFT(x)   (2*(x)+1)
#define HEAP_RIGHT(x)  (2*(x)+2)
#define HEAP_PARENT(x) (((x)-1)>>1)

//* LinkCacheClean
//
//  Ensure efficient memory usage by freeing Links that have expired.
//  For debugging/testing purposes, if LinkTimeout is configured to zero
//  then we never remove stale links.
//
//  Called while holding LC->Lock.
//
static void
LinkCacheClean(MiniportAdapter *VA, Time Now)
{
    if (VA->LinkTimeout != 0) {
        LinkCache *LC = VA->LC;
        Link **Prev;
        Link *This;
        uint Node;

        for (Node = 0; Node < LC->nodeCount; Node++) {
            Prev = &LC->nodes[Node].AdjOut;
            while ((This = *Prev) != NULL) {
                if ((This->TimeStamp + VA->LinkTimeout < Now) &&
                    (This->DropRatio == 0) &&
                    (This->RefCnt == 0)) {
                    //
                    // This link has not been updated for a long time,
                    // and it's not set for artificial drop, so delete it.
                    //
                    LinkCacheLinkChange(LC,
                                        LC->nodes[Node].address,
                                        LC->nodes[This->targetIndex].address,
                                        This->inif, This->outif,
                                        (uint)-1,
                                        LINK_STATE_CHANGE_DELETE_TIMEOUT);

                    *Prev = This->NextOut;
                    if (This->NextOut != NULL)
                        This->NextOut->PrevOut = Prev;

                    *This->PrevIn = This->NextIn;
                    if (This->NextIn != NULL)
                        This->NextIn->PrevIn = This->PrevIn;

                    ExFreePool(This);
                }
                else {
                    Prev = &This->NextOut;
                }
            }
        }
    }
}

//* relax
//
//  See if the shortest distance to v can be improved by going through u.
//  Takes the cache LC, node indices for v and u,
//  and the link that allows you to go from u to v.
//
static boolint
relax(MiniportAdapter *VA, int v, int u, Link *uv)
{
    LinkCache *LC = VA->LC;
    Link **Hops, **Hop;
    int node;
    uint Metric;

    //
    // Make sure that we did not call relax
    // with a link from a node to which we have 
    // no route.
    //
    ASSERT(LC->hops[u] != (uint)-1);

    //
    // Two quick sanity checks.
    //
    if ((*VA->IsInfinite)(uv->Metric) || (LC->hops[u] + 1 >= MAX_SR_LEN))
        return FALSE; // Can not use the link.

    //
    // Create an array of links for the path to v via u.
    //
    Hop = Hops = alloca(sizeof *Hops * (LC->hops[u] + 1));
    *Hop++ = uv;
    node = u;
    while (node != 0) {
        *Hop++ = LC->link[node];
        node = LC->prev[node];
    }

    //
    // Calculate the metric of the new path.
    // This will return (uint)-1 in case of overflow,
    // which will never be an improvement.
    //
    Metric = (*VA->PathMetric)(VA, Hops, LC->hops[u] + 1);
    
    //
    // Verify that this path metric is non-decreasing.
    // Note that this ASSERT is different from the
    // "if" statement below - here, we check against
    // node u, while in the "if" statement, we check
    // against node v. 
    //
    ASSERT(Metric >= LC->metric[u]);

    //
    // Is this an improvement?
    //
    if (Metric < LC->metric[v]) {
        LC->metric[v] = Metric;
        LC->hops[v] = LC->hops[u] + 1;

        //
        // Will we end up creating a 2-node loop?
        //
        ASSERT(LC->prev[u] != (uint)v);

        LC->link[v] = uv;
        LC->prev[v] = u;
        return TRUE;
    }

    return FALSE;
}

//* node_trich
//
//  Trichotomy function for nodes. Useful for bubble_up.
//  Returns +1 if a is better, -1 if b is better, 0 if equal.
//
static int
node_trich(int a, int b, const uint *metric)
{
    if (metric[a] < metric[b])
        return 1; // a is better

    if (metric[a] > metric[b])
        return -1; // b is betetr

    return 0;
}

//* swap
//
//  Swap the heap positions.
//
static void
swap(int a, int b, uint *myHeap, uint *heapLoc)
{
    uint tmp = myHeap[a];
    heapLoc[myHeap[a]] = b;
    heapLoc[myHeap[b]] = a;
    myHeap[a]          = myHeap[b];
    myHeap[b]          = tmp;
}

//* bubble_down
//
// Helper for delete_min.
//
static void
bubble_down(uint *myHeap, uint *heapLoc, int heapsz, uint *metric)
{
    int pos = 0;
    int bestkid;

    while (pos < heapsz) {
        if (HEAP_RIGHT(pos) > heapsz)
            return;
        if (HEAP_RIGHT(pos) == heapsz) {
            // only have a left child. swap if the left child is better
            if (node_trich(myHeap[HEAP_LEFT(pos)], myHeap[pos],
                           metric) > 0)
                swap(pos, HEAP_LEFT(pos), myHeap, heapLoc);
            return;
        }
        // have two kids. figure out which one is better.
        if (node_trich(myHeap[HEAP_LEFT(pos)], myHeap[HEAP_RIGHT(pos)],
                       metric) > 0)
            bestkid = HEAP_LEFT(pos);
        else
            bestkid = HEAP_RIGHT(pos);
        
        if (node_trich(myHeap[bestkid], myHeap[pos],
                       metric) > 0) {
            swap(pos, bestkid, myHeap, heapLoc);
            pos = bestkid;
        } else {
            return;
        }
    }
}

static void
bubble_up(int pos, uint *myHeap, uint *heapLoc, int heapsz,
          uint *metric)
{
    UNREFERENCED_PARAMETER(heapsz);

    while (pos) {
        if (node_trich(myHeap[pos], myHeap[HEAP_PARENT(pos)], 
                       metric) > 0) {
            swap(pos, HEAP_PARENT(pos), myHeap, heapLoc);
            pos = HEAP_PARENT(pos);
        } else {
            return;
        }
    }
}

static boolint
check_heap(uint nodeCount,
           const uint *myHeap, const uint *heapLoc, uint heapsz,
           const uint *metric)
{
    uint i;

    //
    // Check the consistency of myHeap/heapLoc.
    //
    for (i = 0; i < nodeCount; i++)
        if (myHeap[heapLoc[i]] != i)
            return FALSE;

    //
    // Check the consistency of the heap.
    //
    for (i = 0; i < heapsz; i++) {
        //
        // The left child, if present, must be >= the parent.
        //
        if ((HEAP_LEFT(i) < heapsz) &&
            (node_trich(myHeap[i], myHeap[HEAP_LEFT(i)],
                        metric) < 0)) {
            KdPrint(("MCL!check_heap: The left child, if present, must be >= the parent.\n"));
            return FALSE;
        }

        //
        // The right child, if present, must be >= the parent.
        //
        if ((HEAP_RIGHT(i) < heapsz) &&
            (node_trich(myHeap[i], myHeap[HEAP_RIGHT(i)],
                        metric) < 0)) {
            KdPrint(("MCL!check_heap: The right child, if present, must be >= the parent."));
            return FALSE;
        }
    }

    if (heapsz < nodeCount) {
        //
        // The smallest node in the heap is not smaller than the removed nodes.
        //
        if (node_trich(myHeap[heapsz], myHeap[0],
                       metric) < 0) {
            KdPrint(("MCL!check_heap: The smallest node in the heap is not smaller than the removed nodes.\n"));
            return FALSE;
        }

        //
        // The nodes that were removed have non-decreasing metric,
        // which since they are in reverse order, means non-increasing metric.
        //
        for (i = heapsz; i < nodeCount - 1; i++) {
            if (node_trich(myHeap[i + 1], myHeap[i],
                           metric) < 0) {
                KdPrint(("MCL!check_heap: The nodes that were removed have non-decreasing metric.\n"));
                return FALSE;
            }
        }
    }

    return TRUE;
}

#if DBG
//* LinkCacheCheckLinks
//
//  Verifies that the link reference counts are correct.
//
void
LinkCacheCheckLinks(LinkCache *LC)
{
    uint i;
    uint hop;
    Link *walk;
    Link **prev;
    
    //
    // Check the consistency of the AdjOut/AdjIn lists.
    //
    for (i = 0; i < LC->nodeCount; i++) {

        for (prev = &LC->nodes[i].AdjOut;
             (walk = *prev) != NULL;
             prev = &walk->NextOut) {

            ASSERT(walk->PrevOut == prev);
            ASSERT(walk->sourceIndex == (int)i);
        }

        for (prev = &LC->nodes[i].AdjIn;
             (walk = *prev) != NULL;
             prev = &walk->NextIn) {

            ASSERT(walk->PrevIn == prev);
            ASSERT(walk->targetIndex == (int)i);
        }
    }

    //
    // Reset the check counts.
    //
    for (i = 0; i < LC->nodeCount; i++) {
        for (walk = LC->nodes[i].AdjOut; walk != NULL; walk = walk->NextOut)
            walk->CheckRefCnt = 0;
    }

    //
    // Calculate the check counts.
    //
    for (i = 0; i < LC->nodeCount; i++) {
        if (LC->nodes[i].Hops != NULL) {
            for (hop = 0; hop < SOURCE_ROUTE_HOPS(LC->nodes[i].SR->optDataLen) - 1; hop++)
                LC->nodes[i].Hops[hop]->CheckRefCnt++;
        }
    }

    //
    // Check against the real counts.
    //
    for (i = 0; i < LC->nodeCount; i++) {
        for (walk = LC->nodes[i].AdjOut; walk != NULL; walk = walk->NextOut)
            ASSERT(walk->RefCnt == walk->CheckRefCnt);
    }
}
#endif // DBG

//* dijkstra
//
//  Implements the Dijkstra algorithm to compute shortest path routes.
//  Returns FALSE to indicate failure (resource shortage).
//
static int
dijkstra(MiniportAdapter *VA, Time Now)
{
    LinkCache *LC = VA->LC;
    uint *myHeap;
    uint *heapLoc;
    uint heapsz;
    uint i;
    uint curr;
    Link *walk;

    //
    // Remove expired links.
    //
    LinkCacheClean(VA, Now);

#if DBG
    LinkCacheCheckLinks(LC);
#endif

    //
    // Calculate a new value of SmallestMetric.
    // If there are no links, then leave SmallestMetric as zero.
    //
    LC->SmallestMetric = (uint)-1;
    for (i = 0; i < LC->nodeCount; i++) {
        for (walk = LC->nodes[i].AdjOut; walk != NULL; walk = walk->NextOut) {
            uint Metric = (*VA->ConvMetric)(walk->Metric);
            if (Metric < LC->SmallestMetric)
                LC->SmallestMetric = Metric;
        }

        //
        // Also reset the path metrics of any cached routes.
        //
        LC->nodes[i].Metric = 0;

#if CHANGELOGS
        //
        // Along the way, clear the cached RouteUsage records.
        // We want to do this whether dijkstra succeeds or fails.
        //
        LC->nodes[i].CurrentRU = NULL;
#endif
    }
    if (LC->SmallestMetric == (uint)-1)
        LC->SmallestMetric = 0;

    //
    // myHeap is the heap data structure thingy that we de-q from.
    // heapLoc is the inverse table, so in theory myHeap[heapLoc[i]] == i,
    // when i is still in the heap.
    //
    heapsz  = LC->nodeCount;

    myHeap = ExAllocatePool(NonPagedPool, sizeof(int) * heapsz);
    heapLoc = ExAllocatePool(NonPagedPool, sizeof(int) * heapsz);

    if ((myHeap == NULL) || (heapLoc == NULL))
        goto lowmem;

    //
    // Initialize the heap and the metric arrays.
    // Initially all nodes have infinite metric values,
    // except node 0 has metric zero.
    //
    for (i = 0; i < LC->nodeCount; i++) {
        myHeap[i] = i;
        heapLoc[i] = i;
        LC->metric[i] = MAXULONG;
        LC->hops[i] = MAXULONG;
    }
    LC->metric[0] = 0;
    LC->hops[0] = 0;

    while (heapsz && LC->hops[myHeap[0]] < MAX_SR_LEN) {
        ASSERT(check_heap(LC->nodeCount, myHeap, heapLoc, heapsz, LC->metric));

        //
        // Remove the smallest node from the heap.
        //
        curr = myHeap[0];
        swap(0, heapsz-1, myHeap, heapLoc);
        heapsz--;
        bubble_down(myHeap, heapLoc, heapsz, LC->metric);

        //
        // Update the value of other nodes,
        // using the links from the smallest node.
        //
        for (walk = LC->nodes[curr].AdjOut; walk; walk = walk->NextOut) {
            if (relax(VA, walk->targetIndex, curr, walk))
                bubble_up(heapLoc[walk->targetIndex],
                          myHeap, heapLoc, heapsz, LC->metric);
        }
    }

    //
    // So we don't return unitialized memory in IoQueryCacheNode.
    //
    LC->prev[0] = MAXULONG;

    //
    // Calculate a new value of LargestMetric.
    // This is the maximum of the metrics of the links
    // that are actually used in the shortest-path tree.
    // But leave it at infinite if there are no links.
    //
    ASSERT(myHeap[LC->nodeCount - 1] == 0);
    LC->LargestMetric = 0;
    for (i = heapsz; i < LC->nodeCount - 1; i++) {
        Link *link = LC->link[myHeap[i]];
        uint Metric = (*VA->ConvMetric)(link->Metric);
        if (Metric > LC->LargestMetric)
            LC->LargestMetric = Metric;
    }
    if (LC->LargestMetric == 0)
        LC->LargestMetric = (uint)-1;

    //
    // Don't wait very long to re-run dijkstra
    // because link metric values are always changing.
    //
    LC->timeout = Now + CACHE_TIMEOUT;

    ExFreePool(myHeap);
    ExFreePool(heapLoc);

    return TRUE;

lowmem:
    if (myHeap)
        ExFreePool(myHeap);
    if (heapLoc)
        ExFreePool(heapLoc);

    return FALSE;
}
