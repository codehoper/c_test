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

// 
// The basic algorithm is as follows: each node sends 
// a broadcast packet every so often. The packet containts
// a list of nodes, and the seq number of the last broadcast
// packet received from them. Each node
// also keeps track of broadcast packets it receives.
// It can then compute the loss rate on the forward and reverse
// paths. 
//

//
// 1 second probe period. 
//
#define DEFAULT_ETX_PROBE_PERIOD (1 * SECOND)

//
// 30 second loss interval.
//
#define DEFAULT_ETX_LOSS_INTERVAL (30 * SECOND)

//
// When a data packet is dropped, we take an additional
// penalty.
//
#define DEFAULT_ETX_PENALTY_FACTOR 3            

//
// Scaled by MAXALPHA of 10, this becomes 0.1.
//
#define DEFAULT_ETX_ALPHA 1

//
// Loss probabilities greater than this indicate that the link is broken.
//
#define DEFAULT_ETX_BROKEN    4055 // About 99% loss probability.

//
// The initial loss probability for a link, absent any real information.
//
#define DEFAULT_ETX_INITIAL   0    // zero loss probability.

//* EtxIsInfinite
//
//  Returns TRUE if the link metric indicates that the link
//  is effectively broken.
//
boolint
EtxIsInfinite(uint Metric)
{
    WCETTMetric *wcett = (WCETTMetric *)&Metric;

    return (wcett->LossProb > DEFAULT_ETX_BROKEN);
}

//* EtxConvETX
//
//  Converts a link metric (loss probability) to ETX.
//
uint
EtxConvETX(uint LinkMetric)
{
    WCETTMetric *wcett = (WCETTMetric *)&LinkMetric;

    //
    // Convert loss probability (which is scaled by 4096)
    // to ETX (also scaled by 4096).
    // NB: LossProb ranges from 0 to 4095,
    // so we will not divide by zero.
    //
    return (4096 * 4096) / (4096 - wcett->LossProb);
}

//* EtxInitLinkMetric
//
// Init metric information for a new link.
//
void 
EtxInitLinkMetric(
    MiniportAdapter *VA,
    int SNode,
    Link *Link, 
    Time Now)
{
    ProtocolAdapter *PA;

    UNREFERENCED_PARAMETER(Now);

    //
    // Start with an initial loss probability.
    //
    Link->wcett.LossProb = DEFAULT_ETX_INITIAL;

    if ((SNode == 0) &&
        ((PA = FindPhysicalAdapterFromIndex(VA, Link->outif)) != NULL)) {
        //
        // Get the bandwidth and channel from our physical interface.
        //
        Link->wcett.Bandwidth = PA->Bandwidth;
        Link->wcett.Channel = PA->Channel;
    }
    else {
        //
        // We don't know the bandwidth and channel yet.
        //
        Link->wcett.Bandwidth = 0;
        Link->wcett.Channel = 0;
    }

    Link->MetricInfo.Etx.TotSentProbes = 0;
    Link->MetricInfo.Etx.TotRcvdProbes = 0;
    Link->MetricInfo.Etx.FwdDeliv = 0;
    Link->MetricInfo.Etx.ProbeHistorySZ = 0;
    Link->MetricInfo.Etx.PHStart = 0;
    Link->MetricInfo.Etx.PHEnd = 0;
}

//* EtxInit
// 
//  Called by MiniportInitialize.
//
void EtxInit(
    MiniportAdapter *VA) 
{
    Time Now = KeQueryInterruptTime();

    VA->IsInfinite = EtxIsInfinite;
    VA->ConvMetric = EtxConvETX;
    VA->InitLinkMetric = EtxInitLinkMetric;
    VA->PathMetric = MiniportPathMetric;

    VA->MetricParams.Etx.ProbePeriod = DEFAULT_ETX_PROBE_PERIOD; 

    //
    // Broadcast metric is special. The packets are not sent 
    // on a per-link basis, but on a per-node basis. 
    //
    VA->MetricParams.Etx.ProbeTimeout = Now + VA->MetricParams.Etx.ProbePeriod;

    //
    // We measure loss interval over this time period.
    //
    VA->MetricParams.Etx.LossInterval = DEFAULT_ETX_LOSS_INTERVAL;

    VA->MetricParams.Etx.Alpha = DEFAULT_ETX_ALPHA;
    VA->MetricParams.Etx.PenaltyFactor = DEFAULT_ETX_PENALTY_FACTOR;
}

//* EtxSendProbeComplete
//
//  Called after sending a probe.  Cleans up after EtxCreateProbePacket.
//
static void
EtxSendProbeComplete(
    MiniportAdapter *VA,
    SRPacket *srp,
    NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(VA);
    UNREFERENCED_PARAMETER(Status);

    SRPacketFree(srp);
}

//* EtxFillProbeData
//  
//  Fills data in broadcast probe about
//  how many broadcast packets have been
//  received on each of our links. 
//
static void 
EtxFillProbeData(
    MiniportAdapter *VA,
    SRPacket *SRP)
{
    EtxProbe *BP; 
    LinkCache *LC = VA->LC; 
    Link *Adj;
    KIRQL OldIrql;

    BP = (EtxProbe *)SRP->Probe->Opt.Special; 
    BP->NumEntries = 0;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);

    for (Adj = LC->nodes[0].AdjIn; 
         (Adj != NULL) && (BP->NumEntries < MAX_ETX_ENTRIES);
         Adj = Adj->NextIn) {

        // 
        // Copy the address of the node and the link's inif/outif.
        // Then copy the number of probes we received in the 
        // last loss interval - i.e. the probe history size.
        //
        RtlCopyMemory(BP->Entry[BP->NumEntries].From, 
                      LC->nodes[Adj->sourceIndex].address, 
                      sizeof(VirtualAddress));
        BP->Entry[BP->NumEntries].OutIf = Adj->outif;
        BP->Entry[BP->NumEntries].InIf = Adj->inif;
        BP->Entry[BP->NumEntries].Rcvd = (ushort)Adj->MetricInfo.Etx.ProbeHistorySZ;
        BP->NumEntries++;
    }

    KeReleaseSpinLock(&LC->Lock, OldIrql);
}

//* EtxCreateProbePacket
//
//  Creates a packet to send a probe.
//
static NDIS_STATUS
EtxCreateProbePacket(
    MiniportAdapter *VA,
    Time Timestamp,
    SRPacket **ReturnPacket,
    uint Seq)
{
    SRPacket *SRP;
    NDIS_STATUS Status;
    const static VirtualAddress BcastDest = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint ProbeSize;

    InterlockedIncrement((PLONG)&VA->CountXmitProbe);

    //
    // Initialize an SRPacket for the Probe.
    // The Probe carries no data so it does not need an IV.
    //
    SRP = ExAllocatePool(NonPagedPool, sizeof *SRP);
    if (SRP == NULL) {
        return NDIS_STATUS_RESOURCES;
    }
    RtlZeroMemory(SRP, sizeof *SRP);

    //
    // Initialize the Probe option. The probe carries
    // info about broadcast packets received, so allocate 
    // extra space. 
    //
    ProbeSize = sizeof *SRP->Probe + sizeof (EtxProbe);
    SRP->Probe = ExAllocatePool(NonPagedPool, ProbeSize);
    if (SRP->Probe == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto FreeSRPAndExit;
    }
    RtlZeroMemory(SRP->Probe, ProbeSize);

    SRP->Probe->Opt.OptionType = LQSR_OPTION_TYPE_PROBE;
    SRP->Probe->Opt.OptDataLen = PROBE_LEN + sizeof (EtxProbe);
    SRP->Probe->Opt.MetricType = VA->MetricType;
    SRP->Probe->Opt.ProbeType = METRIC_TYPE_ETX;
    SRP->Probe->Opt.Seq = Seq;
    SRP->Probe->Opt.Timestamp = Timestamp;
    ASSERT(SRP->Probe->Opt.Metric == 0);
    ASSERT(SRP->Probe->Opt.InIf == 0);
    // SRP->Probe->Opt.OutIf initialized in EtxForwardProbe.

    //
    // This is a broadcast packet, so we just fill our address.
    // Dst addr is set to etx addr. We can't set InIf and OutIf. 
    //
    RtlCopyMemory(SRP->Probe->Opt.From, VA->Address, sizeof(VirtualAddress));
    RtlCopyMemory(SRP->Probe->Opt.To, BcastDest, sizeof(VirtualAddress));

    //
    // Initialize the source & destination of this packet.
    //
    RtlCopyMemory(SRP->Source, VA->Address, sizeof(VirtualAddress));
   
    //
    // Fill in the broadcast probe receive data. 
    //
    EtxFillProbeData(VA, SRP);
    
    //
    // Set the cleanup function. 
    //
    SRP->TransmitComplete = EtxSendProbeComplete;

    *ReturnPacket = SRP;
    return NDIS_STATUS_SUCCESS;

FreeSRPAndExit:
    SRPacketFree(SRP);
    return Status;
}

//* EtxProbeComplete
//
//  Completes an individual packet transmission for ProtocolForwardRequest.
//
void
EtxProbeComplete(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet,
    NDIS_STATUS Status)
{
    SRPacket *srp = PC(Packet)->srp;

    //
    // Free the packet structure.
    //
    NdisFreePacketClone(Packet);

    //
    // Update the current cumulative status.
    // If the current status is NDIS_STATUS_SUCCESS,
    // we replace it with our new status.
    //
    InterlockedCompareExchange((PLONG)&srp->ForwardStatus,
                               Status,
                               NDIS_STATUS_SUCCESS);

    if (InterlockedDecrement((PLONG)&srp->ForwardCount) == 0) {
        //
        // Complete the ProtocolForwardRequest operation.
        //
        (*srp->TransmitComplete)(VA, srp, srp->ForwardStatus);
    }
}

//* EtxForwardProbe 
//
//  Sends a probe packet via every physical adapter simultaneously.
//  The operation completes when the last underlying transmit completes.
//  The operation completes successfully only if every
//  underlying transmit is successful.
//
//  Our caller supplies srp->TransmitComplete, which always called
//  to consume the SRPacket.
//
void
EtxForwardProbe(
    MiniportAdapter *VA,
    SRPacket *srp)
{
    ProtocolAdapter *PA;
    NDIS_PACKET *PacketList = NULL;
    NDIS_PACKET *Packet;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    //
    // We start with one reference (our own) for the SRPacket.
    //
    srp->ForwardCount = 1;
    srp->ForwardStatus = NDIS_STATUS_SUCCESS;

    //
    // First we build a temporary list of packet structures
    // and corresponding physical adapters. While doing this,
    // we also initialize the transmission count.
    // It starts with one, for our own reference for OrigPacket.
    //
    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    for (PA = VA->PhysicalAdapters;
         PA != NULL;
         PA = PA->Next) {

        srp->Probe->Opt.OutIf = (LQSRIf) PA->Index;

        Status = SRPacketToPkt(VA, srp, &Packet);
        if (Status != NDIS_STATUS_SUCCESS) {
            srp->ForwardStatus = Status;
            break;
        }

        //
        // Remember the packet and corresponding physical adapter.
        // We temporarily use the OrigPacket field to build our list.
        //
        PC(Packet)->PA = PA;
        PC(Packet)->OrigPacket = PacketList;
        PacketList = Packet;
        srp->ForwardCount++;
    }
    KeReleaseSpinLock(&VA->Lock, OldIrql);

    //
    // Now we can transmit the packet via each physical adapter,
    // using the new packet structures.
    //
    while ((Packet = PacketList) != NULL) {
        PacketList = PC(Packet)->OrigPacket;
        PA = PC(Packet)->PA;

        //
        // Send the packet via a physical adapter.
        //
        PC(Packet)->srp = srp;
        PC(Packet)->TransmitComplete = EtxProbeComplete;
        ProtocolTransmit(PA, Packet);
    }

    //
    // Release our reference for the original packet.
    //
    if (InterlockedDecrement((PLONG)&srp->ForwardCount) == 0) {
        //
        // Complete the ProtocolForwardRequest operation.
        //
        (*srp->TransmitComplete)(VA, srp, srp->ForwardStatus);
    }
}

//* EtxRemoveOldProbes
//  
//  Removes old probes.
//
//  Called with LinkCache locked.
//
void
EtxRemoveOldProbes(
    MiniportAdapter *VA, 
    Link *Link) 
{
    Etx *Etx = &(Link->MetricInfo.Etx);
    uint MEPH = MAX_ETX_PROBE_HISTORY; 
    uint LI = VA->MetricParams.Etx.LossInterval;
    Time Now = KeQueryInterruptTime();
   
    while (((Now - Etx->PH[Etx->PHStart].RcvdTS) >= LI) && (Link->MetricInfo.Etx.ProbeHistorySZ > 0)) {
        Etx->PHStart = (Etx->PHStart + 1) % MEPH;
        Link->MetricInfo.Etx.ProbeHistorySZ--;
    }
}

//* EtxAddProbe
//
//  Adds a probe to probe history of this link.
//  And while we are at it, also remove old probes
//  from the history.
//
//  Called with LinkCache locked.
//
NDIS_STATUS
EtxAddProbe(
    MiniportAdapter *VA, 
    Link *Link) 
{
    Time Now = KeQueryInterruptTime();
    Etx *Etx = &(Link->MetricInfo.Etx);
    uint MEPH = MAX_ETX_PROBE_HISTORY; 
    NDIS_STATUS Status; 

    if (Etx->ProbeHistorySZ == MEPH) {
        KdPrint(("MCL!EtxAddProbe:Too many probes.\n"));
        Status = NDIS_STATUS_RESOURCES;
    }
    else { 
        Etx->PH[Etx->PHEnd].RcvdTS = Now; 
        Etx->PHEnd = (Etx->PHEnd + 1) % MEPH;
        Etx->ProbeHistorySZ++;
        Status = NDIS_STATUS_SUCCESS; 
    }

    //
    // Remove old probes. We have to do this here
    // in case the corresponding outgoing link
    // does not exist and so EtxFindDeliveryCounts will not be called.
    //
    EtxRemoveOldProbes(VA, Link);
    return Status;
}

//* EtxFindDeliveryCounts
//
//  Given a link from this node, finds the forward and reverse delivery counts.
//  These numbers are kept in the reverse link.
//
//  Called with the LinkCache locked.
//
void
EtxFindDeliveryCounts(
    MiniportAdapter *VA,
    Link *FwdLink,
    OUT uint *FwdDeliv,
    OUT uint *RevDeliv)
{
    LinkCache *LC = VA->LC; 
    Link *RevLink;

    ASSERT(FwdLink->sourceIndex == 0);

    for (RevLink = LC->nodes[0].AdjIn; ; RevLink = RevLink->NextIn) {
        if (RevLink == NULL) {
            //
            // The reverse link does not exist.
            //
            *FwdDeliv = *RevDeliv = 0;
            break;
        }

        ASSERT(RevLink->targetIndex == 0);

        if ((RevLink->sourceIndex == FwdLink->targetIndex) &&
            (RevLink->inif == FwdLink->outif) &&
            (RevLink->outif == FwdLink->inif)) {
            //
            // This is the reverse link. We have to remove old probes here,
            // in case we have stopped receiving probes via the reverse link
            // and so EtxAddProbe has not been called recently.
            //
            EtxRemoveOldProbes(VA, RevLink);
            *FwdDeliv = RevLink->MetricInfo.Etx.FwdDeliv;
            *RevDeliv = RevLink->MetricInfo.Etx.ProbeHistorySZ;
            break;
        }
    }
}

//* EtxUpdateMetric
//  
//  Update the metric for a given link.
//
//  Only called on adjacent links, meaning the source of the link is node 0.
//
//  Called with the LinkCache locked.
//
void
EtxUpdateMetric(
    MiniportAdapter *VA, 
    Link *Link,
    boolint Penalty)
{
    if (Penalty) {
        uint SuccessProb;

        //
        // We penalize the link delivery probability.
        //
        SuccessProb = 4096 - Link->wcett.LossProb;
        SuccessProb /= VA->MetricParams.Etx.PenaltyFactor;
        if (SuccessProb == 0)
            SuccessProb = 1;
        Link->wcett.LossProb = 4096 - SuccessProb;
    }
    else {
        //
        // We calculate the probability of successful packet delivery.
        // This is the product of forward delivery and reverse delivery.
        //          FwdDeliv * RevDeliv.
        // 
        // Let's say that this is link is from us to node X.
        //
        // We know the fwd delivery ratio, i.e. number of probes that
        // we sent that got through to X, from the information carried
        // in the last probe we received from X. This information
        // might be stale, if we haven't heard from X in a while.
        //
        // The rev delivery ratio is the number of probes that we
        // received from X. We know this - this is the ProbeHistorySZ.
        //
        // Note that the FwdDeliv and RevDeliv are computed over
        // LossInterval.  We assume that both nodes have the same
        // sending frequency (ProbePeriod).
        //
        uint Prob;

        if (Link->MetricInfo.Etx.TotSentProbes == 0) {
            //
            // We are just getting started and have no data.
            //
            Prob = DEFAULT_ETX_INITIAL;
        }
        else {
            uint NumProbes;
            uint FwdDeliv, RevDeliv;

            //
            // Recall that we randomize probe send times by adding 0-25%
            // of the probe interval.  So, we need to account for that by
            // adding expected number of probes - 12.5%.
            //
            NumProbes = VA->MetricParams.Etx.LossInterval / (VA->MetricParams.Etx.ProbePeriod + (VA->MetricParams.Etx.ProbePeriod >> 3)) ;
            ASSERT(NumProbes != 0);

            //
            // When the link is first created and  ProbeHistorySZ/FwdDeliv
            // are ramping up, we must adjust NumProbes appropriately.
            //
            if (Link->MetricInfo.Etx.TotSentProbes < NumProbes)
                NumProbes = Link->MetricInfo.Etx.TotSentProbes;

            //
            // Find the forward and reverse delivery counts,
            // from the reverse link.
            //
            EtxFindDeliveryCounts(VA, Link, &FwdDeliv, &RevDeliv);

            //
            // Probability is scaled by 4096.
            //
            Prob = (4096 * FwdDeliv * RevDeliv) / (NumProbes * NumProbes);

            //
            // Ensure that the success probability is between 0 and 1,
            // in case ProbeHistorySZ and/or FwdDeliv are too large.
            //
            if (Prob > 4096)
                Prob = 4096;

            //
            // Convert to loss probability.
            //
            Prob = 4096 - Prob;
        }

        //
        // Average loss probability into the old value.
        // LossProb = Alpha * New + (1 - Alpha) * Old
        // Alpha is interpreted as Alpha/MAXALPHA.
        //
        Prob = (VA->MetricParams.Etx.Alpha * Prob +
                ((MAXALPHA - VA->MetricParams.Etx.Alpha) * Link->wcett.LossProb))
            / MAXALPHA;

        //
        // The 12-bit field can not hold a loss probability of 1.0 (4096).
        //
        if (Prob >= 4096)
            Prob = 4095;
        Link->wcett.LossProb = Prob;
    }
}

//* EtxPenalize
//  
//  Takes Penalty due to data packet drop. 
//
//  Only called on adjacent links, meaning the source of the link is node 0.
//
//  Called with the LinkCache locked.
//
void
EtxPenalize(
    MiniportAdapter *VA, 
    Link *Adj) 
{
    EtxUpdateMetric(VA, Adj, TRUE);
}

//* EtxUpdateFwdDeliv
// 
//  Update the forward delivery ratio using received info. 
//
//  Called with LinkCache locked.
//
void
EtxUpdateFwdDeliv(
    MiniportAdapter *VA,
    Link *Adj, 
    InternalProbe *Probe) 
{
    EtxProbe *BP = (EtxProbe *)Probe->Opt.Special;
    uint i;

    for (i = 0 ; (i < BP->NumEntries) && (i < MAX_ETX_ENTRIES); i++) {
        //
        // Find the entry for this link.
        //
        if (VirtualAddressEqual(BP->Entry[i].From, VA->Address) &&
            (BP->Entry[i].InIf == Adj->outif) &&
            (BP->Entry[i].OutIf == Adj->inif)) {

            //
            // Store the number of probes the other node received from us.
            //
            Adj->MetricInfo.Etx.FwdDeliv = BP->Entry[i].Rcvd;
        }
    }
}

//* EtxSendProbe
//
//  Sends a broadcast probe. 
//
Time
EtxSendProbes(
    MiniportAdapter *VA,
    Time Now)
{
    SRPacket *Packet;
    NDIS_STATUS Status;
    LARGE_INTEGER Timestamp;
    LARGE_INTEGER Frequency;
    LinkCache *LC = VA->LC; 
    Link *Adjacent;
    
    //
    // Create a probe packet, and send it. It is a broadcast packet,
    // so no need to loop through links individually. It will go out
    // on all links.
    // 
    if (VA->MetricParams.Etx.ProbeTimeout <= Now) {
        Timestamp = KeQueryPerformanceCounter(&Frequency);
        Status = EtxCreateProbePacket(VA, Timestamp.QuadPart, &Packet, 0);
        if (Status == NDIS_STATUS_SUCCESS) {
            //
            // Send the packet.
            //
            EtxForwardProbe(VA, Packet);

            //
            // Recompute the metric on all links from us.
            //
            for (Adjacent = LC->nodes[0].AdjOut; 
                 Adjacent != NULL; 
                 Adjacent = Adjacent->NextOut) {

                EtxUpdateMetric(VA, Adjacent, FALSE);
                Adjacent->MetricInfo.Etx.TotSentProbes++;
            }
        }

        //
        // Calculate next probe timeout. Randomize by adding 25% delay. 
        //
        VA->MetricParams.Etx.ProbeTimeout = Now + 
            VA->MetricParams.Etx.ProbePeriod +
            GetRandomNumber(VA->MetricParams.Etx.ProbePeriod >> 2);

    }
    return VA->MetricParams.Etx.ProbeTimeout;
}

//* EtxReceiveProbe
//
//  Receive a Probe. Called from
//  ReceiveSRPacket. We have already verified that the 
//  MetricType is ETX.
//
void
EtxReceiveProbe(
    MiniportAdapter *VA,
    InternalProbe *Probe,
    LQSRIf InIf)
{
    LinkCache *LC = VA->LC; 
    Link *Adj;
    KIRQL OldIrql;
   
    ASSERT(Probe->Opt.MetricType == METRIC_TYPE_ETX);

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    //
    // First find the link on which it showed up.
    // The link should exist because ReceiveSRPacket calls
    // LinkCacheAddLink before EtxReceiveProbe.
    //
    for (Adj = LC->nodes[0].AdjIn; ; Adj = Adj->NextIn) {

        if (Adj == NULL) {
            KdPrint(("MCL!EtxReceiveProbe: link not found\n"));
            break;
        }

        ASSERT(Adj->targetIndex == 0);

        if (VirtualAddressEqual(Probe->Opt.From,
                                LC->nodes[Adj->sourceIndex].address) &&
            (Adj->outif == Probe->Opt.OutIf) &&
            (Adj->inif == InIf)) {
            //
            // We found the link, so add the probe to our history
            // and retrieve the forward delivery count from the probe.
            //
            Adj->MetricInfo.Etx.TotRcvdProbes++;
            EtxAddProbe(VA, Adj);
            EtxUpdateFwdDeliv(VA, Adj, Probe);
            break;
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);
}    
