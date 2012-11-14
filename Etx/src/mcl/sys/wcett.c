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
// 1 second probe period. 
//
#define DEFAULT_WCETT_PROBE_PERIOD (1 * SECOND)

//
// 30 second loss interval.
//
#define DEFAULT_WCETT_LOSS_INTERVAL (30 * SECOND)

//
// When a data packet is dropped, we take an additional
// penalty.
//
#define DEFAULT_WCETT_PENALTY_FACTOR 3            

//
// This is the weight given to the bottleneck-channel ETT,
// versus the overall path ETT. Scaled by MAXALPHA of 10.
//
#define DEFAULT_WCETT_BETA      1

//
// This is the smoothing factor used for loss rate calulations.
// Scaled by MAXALPHA of 10.
//
#define DEFAULT_WCETT_ALPHA     1

//
// Loss probabilities greater than this indicate that the link is broken.
//
#define DEFAULT_WCETT_BROKEN    4055 // About 99% loss probability.

//
// The initial loss probability for a link, absent any real information.
//
#define DEFAULT_WCETT_INITIAL   2048 // 50% loss probability.

//
// The minimum congestion window, in time units.
//
#define WCETT_CWMIN     (320 * MICROSECOND)

//
// How often to probe for bandwidth measurement.
//
#define DEFAULT_WCETT_PKTPAIR_PROBE_PERIOD (60 * SECOND)
#define DEFAULT_WCETT_PKTPAIR_MIN_OVER_PROBES 10

//* WcettDefaultBandwidth
//
//  Given the maximum bandwidth of a link in bps,
//  calculates a default bandwidth in bps.
//
__inline uint
WcettDefaultBandwidth(uint Bandwidth)
{
    return Bandwidth / 2;
}

//* WcettEncodeBandwidth
//
//  Converts bandwidth in bps to our 12-bit encoded format.
//  Bandwidth is encoded with a 10-bit mantissa and a 2-bit exponent:
//      bps = Bmant * (1000 ^ (Bexp + 1))
//  In other words, Bexp = 0 means Kbps, Bexp = 1 means Mbps, etc.
//
uint
WcettEncodeBandwidth(uint Bandwidth)
{
    uint Bexp;

    for (Bexp = 0; Bandwidth >= 1000; Bexp++)
        Bandwidth /= 1000;
    ASSERT(Bexp <= 4);

    if (Bexp == 0)
        return 0;
    else
        return (Bandwidth << 2) | (Bexp - 1);
}

//* WcettDecodeBandwidth
//
//  Converts bandwidth in our 12-bit encoded format to bps.
//  Bandwidth is encoded with a 10-bit mantissa and a 2-bit exponent:
//      bps = Bmant * (1000 ^ (Bexp + 1))
//  In other words, Bexp = 0 means Kbps, Bexp = 1 means Mbps, etc.
//
uint
WcettDecodeBandwidth(uint Bandwidth)
{
    uint Bexp = Bandwidth & 3;
    uint Bmant = Bandwidth >> 2;

    Bandwidth = Bmant * 1000;
    while (Bexp != 0) {
        Bandwidth *= 1000;
        Bexp--;
    }

    return Bandwidth;
}

//* WcettConvertPktPairDelayToBandwidth
//
//  Converts packet-pair delay in 100ns units to bandwidth in bps.
//
__inline uint
WcettConvertPktPairDelayToBandwidth(uint Delay)
{
    //
    // Delay is in 100ns units and we measure a 1088-byte packet.
    // Calculate Bandwidth in bps.
    //
    return ((1088 * 8 * 100000) / Delay) * 100;
}

//* WcettIsInfinite
//
//  Returns TRUE if the link metric indicates that the link
//  is effectively broken.
//
boolint
WcettIsInfinite(uint Metric)
{
    WCETTMetric *wcett = (WCETTMetric *)&Metric;

    return (wcett->LossProb > DEFAULT_WCETT_BROKEN);
}

//* WcettChannel
//
//  Extracts the channel from a link metric.
//
__inline uint
WcettChannel(uint LinkMetric)
{
    WCETTMetric *wcett = (WCETTMetric *)&LinkMetric;
    return wcett->Channel;
}

//* WcettConvETT
//
//  Converts a link metric (loss probability & bandwidth) to ETT.
//  The ETT value uses 100-ns time units.
//
uint
WcettConvETT(uint LinkMetric)
{
    WCETTMetric *wcett = (WCETTMetric *)&LinkMetric;
    uint Temp;
    uint Backoff;
    uint Bandwidth;
    uint Transmit;

    //
    // First calculate a temp value (scaled by 4096)
    // from the loss probability (which is scaled by 4096):
    //   Temp = 1 + p + 2p^2 + 4p^3 + 8p^4 + 16p^5 + 32p^6 + 64p^7
    //
    Temp = 4096 + 2 * wcett->LossProb;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + wcett->LossProb * Temp) / 4096;
    //
    // Now finish the backoff calculation, converting to time units:
    //  Backoff = (CWmin / 2) * (Temp / (1 - p))
    //
    Backoff = (WCETT_CWMIN * Temp) / (2 * (4096 - wcett->LossProb));

    //
    // Calculate the transmission time for a 1024-byte packet,
    // converting to time units:
    //  Transmit = (S / B) * (1 / (1 - p))
    // We use S = 1024 bytes.
    // So we want to calculate
    //  Transmit = (8 * 1024 * SECOND * 4096) / (B * (4096 - wcett->LossProb))
    // We divide both numerator & denominator by 1024 * 1024.
    //
    Bandwidth = WcettDecodeBandwidth(wcett->Bandwidth);
    if (Bandwidth >= 1024 * 1024)
        Temp = ((Bandwidth / 1024) * (4096 - wcett->LossProb)) / 1024;
    else
        Temp = (Bandwidth * (4096 - wcett->LossProb)) / (1024 * 1024);
    if (Temp == 0)
        return (uint)-1;
    else
        Transmit = (4 * 8 * SECOND) / Temp;

    if (Backoff + Transmit < Transmit)
        return (uint)-1;

    return Backoff + Transmit;
}

//* WcettCalcWCETT
//
//  Calculates the WCETT metric of an array of links.
//
//  Called with the link cache locked.
//
uint
WcettCalcWCETT(
    MiniportAdapter *VA,
    Link **Hops,
    uint NumHops)
{
    uint ETT, MCETT;
    uint WCETT;
    uint i, j;
    
    ASSERT(NumHops <= MAX_SR_LEN);

    //
    // Calculate sum of ETT for each link.
    //
    ETT = 0;
    for (i = 0; i < NumHops; i++) {
        uint New;

        //
        // Check for a broken link.
        //
        if (WcettIsInfinite(Hops[i]->Metric))
            return (uint)-1;

        //
        // Check for overflow.
        //
        New = ETT + WcettConvETT(Hops[i]->Metric);
        if (New < ETT)
            return (uint)-1;
        ETT = New;
    }
    
    //
    // Calculate maximum of ETTs for each channel.
    // We do not have to worry about overflow or broken links,
    // because of the prior checks.
    //
    MCETT = 0;
    for (i = 0; i < NumHops; i++) {
        uint CETT = 0;
        for (j = 0; j < NumHops; j++) {
            if (WcettChannel(Hops[i]->Metric) == WcettChannel(Hops[j]->Metric))
                CETT += WcettConvETT(Hops[j]->Metric);
        }
        if (CETT > MCETT)
            MCETT = CETT;
    }

    ASSERT(MCETT <= ETT);
    WCETT = ((ETT * (MAXALPHA - VA->MetricParams.Wcett.Beta)) + (MCETT * VA->MetricParams.Wcett.Beta)) / MAXALPHA;

    //
    // If WCETT is not between ETT and MCETT, we overflowed.
    //
    if ((MCETT <= WCETT) && (WCETT <= ETT))
        return WCETT;
    else
        return (uint)-1;
}

//* WcettInitLinkMetric
//
// Init metric information for a new link. 
//
// Called with LinkCache locked.
//
void 
WcettInitLinkMetric(
    MiniportAdapter *VA,
    int SNode,
    Link *Link, 
    Time Now)
{
    ProtocolAdapter *PA;
    uint i; 

    //
    // Start with an initial loss probability.
    //
    Link->wcett.LossProb = DEFAULT_WCETT_INITIAL;

    if ((SNode == 0) &&
        ((PA = FindPhysicalAdapterFromIndex(VA, Link->outif)) != NULL)) {
        uint Bandwidth;
        //
        // Get the max bandwidth and channel from our physical interface,
        // and calculate an initial bandwidth from the max.
        //
        Link->wcett.Channel = PA->Channel;
        Bandwidth = WcettDecodeBandwidth(PA->Bandwidth);
        Link->MetricInfo.Wcett.MaxBandwidth = Bandwidth;
        Bandwidth = WcettDefaultBandwidth(Bandwidth);
        Link->wcett.Bandwidth = WcettEncodeBandwidth(Bandwidth);
    }
    else {
        //
        // We don't know the bandwidth and channel yet.
        //
        Link->wcett.Channel = 0;
        Link->MetricInfo.Wcett.MaxBandwidth = 0;
        Link->wcett.Bandwidth = 0;
    }

    Link->MetricInfo.Wcett.Etx.TotSentProbes = 0;
    Link->MetricInfo.Wcett.Etx.TotRcvdProbes = 0;
    Link->MetricInfo.Wcett.Etx.FwdDeliv = 0;
    Link->MetricInfo.Wcett.Etx.ProbeHistorySZ = 0;
    Link->MetricInfo.Wcett.Etx.PHStart = 0;
    Link->MetricInfo.Wcett.Etx.PHEnd = 0;
    for (i = 0; i < MAX_ETX_PROBE_HISTORY; i++) { 
        Link->MetricInfo.Wcett.Etx.PH[i].RcvdTS = 0;
    }

    //
    // We use PktPair just to measure link bandwidth.
    //

    //
    // PktPair sender-specific info.
    //
    Link->MetricInfo.Wcett.PktPair.ProbeSeq = 0;
    Link->MetricInfo.Wcett.PktPair.PairsSent = 0;
    Link->MetricInfo.Wcett.PktPair.RepliesSent = 0;
    Link->MetricInfo.Wcett.PktPair.RepliesRcvd = 0;
    Link->MetricInfo.Wcett.PktPair.LostPairs = 0;
    Link->MetricInfo.Wcett.PktPair.ProbeTimeout = Now;
    Link->MetricInfo.Wcett.PktPair.Outstanding = 0;
    Link->MetricInfo.Wcett.PktPair.Delta = 0;
    Link->MetricInfo.Wcett.PktPair.RTT = 0;
    Link->MetricInfo.Wcett.PktPair.LastRTT = 0;
    Link->MetricInfo.Wcett.PktPair.LastPktPair = 0;

    //
    // PktPair receiver-specific info.
    //
    Link->MetricInfo.Wcett.PktPair.LastProbeTimestamp = (uint)-1;
    Link->MetricInfo.Wcett.PktPair.TimeLastProbeRcvd = 0;
    Link->MetricInfo.Wcett.PktPair.LastProbeSeq = (uint)-1;

    //
    // Packetpair minimum. 
    //
    Link->MetricInfo.Wcett.PktPair.CurrMin = (uint)-1;

    Link->MetricInfo.Wcett.NumPktPairValid = 0;
    Link->MetricInfo.Wcett.NumPktPairInvalid = 0;
}


//* WcettInit
//
//  Called by MiniportInitialize.
//
void WcettInit(
    MiniportAdapter *VA)
{
    Time Now = KeQueryInterruptTime();

    VA->IsInfinite = WcettIsInfinite;
    VA->ConvMetric = WcettConvETT;
    VA->InitLinkMetric = WcettInitLinkMetric;
    VA->PathMetric = WcettCalcWCETT;

    VA->MetricParams.Wcett.ProbePeriod = DEFAULT_WCETT_PROBE_PERIOD;

    //
    // Broadcast metric is special. The packets are not sent
    // on a per-link basis, but on a per-node basis.
    //
    VA->MetricParams.Wcett.ProbeTimeout = Now + VA->MetricParams.Wcett.ProbePeriod;

    //
    // We measure loss interval over this time period.
    //
    VA->MetricParams.Wcett.LossInterval = DEFAULT_WCETT_LOSS_INTERVAL;

    VA->MetricParams.Wcett.Alpha = DEFAULT_WCETT_ALPHA;
    VA->MetricParams.Wcett.PenaltyFactor = DEFAULT_WCETT_PENALTY_FACTOR;
    VA->MetricParams.Wcett.Beta = DEFAULT_WCETT_BETA;

    //
    // We use a packet-pair type probe for determining bandwidth.
    //
    VA->MetricParams.Wcett.PktPairProbePeriod = DEFAULT_WCETT_PKTPAIR_PROBE_PERIOD;
    VA->MetricParams.Wcett.PktPairMinOverProbes = DEFAULT_WCETT_PKTPAIR_MIN_OVER_PROBES;
}

//* WcettSendProbeComplete
//
//  Called after sending a probe.  Cleans up after WcettCreateProbePacket.
//
static void
WcettSendProbeComplete(
    MiniportAdapter *VA,
    SRPacket *srp,
    NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(VA);
    UNREFERENCED_PARAMETER(Status);

    SRPacketFree(srp);
}

//* WcettFillProbeData
//
//  Fills data in broadcast probe about
//  how many broadcast packets have been
//  received on each of our links.
//
static void
WcettFillProbeData(
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
        BP->Entry[BP->NumEntries].Rcvd = (ushort)Adj->MetricInfo.Wcett.Etx.ProbeHistorySZ;
        BP->NumEntries++;
    }

    KeReleaseSpinLock(&LC->Lock, OldIrql);
}

//* WcettCreateProbePacket
//
//  Creates a packet to send a probe.
//
static NDIS_STATUS
WcettCreateProbePacket(
    MiniportAdapter *VA,
    Time Timestamp,
    SRPacket **ReturnPacket,
    uint Seq)
{
    SRPacket *SRP;
    NDIS_STATUS Status;
    const static VirtualAddress WcettDest = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
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
    // SRP->Probe->Opt.OutIf initialized in WcettForwardProbe.

    //
    // This is a broadcast packet, so we just fill our address.
    // Dst addr is set to etx addr. We can't set InIf and OutIf. 
    //
    RtlCopyMemory(SRP->Probe->Opt.From, VA->Address, sizeof(VirtualAddress));
    RtlCopyMemory(SRP->Probe->Opt.To, WcettDest, sizeof(VirtualAddress));

    //
    // Initialize the source & destination of this packet.
    //
    RtlCopyMemory(SRP->Source, VA->Address, sizeof(VirtualAddress));

    //
    // Fill in the broadcast probe receive data.
    //
    WcettFillProbeData(VA, SRP);

    //
    // Set the cleanup function.
    //
    SRP->TransmitComplete = WcettSendProbeComplete;

    *ReturnPacket = SRP;
    return NDIS_STATUS_SUCCESS;

FreeSRPAndExit:
    SRPacketFree(SRP);
    return Status;
}

//* WcettProbeComplete
//
//  Completes an individual packet transmission for ProtocolForwardRequest.
//
void
WcettProbeComplete(
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

//* WcettForwardProbe
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
WcettForwardProbe(
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
        PC(Packet)->TransmitComplete = WcettProbeComplete;
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

//* WcettRemoveOldProbes
//
//  Removes old probes.
//
//  Called with LinkCache locked.
//
void
WcettRemoveOldProbes(
    MiniportAdapter *VA,
    Link *Link)
{
    Etx *Etx = &(Link->MetricInfo.Wcett.Etx);
    uint MEPH = MAX_ETX_PROBE_HISTORY;
    uint LI = VA->MetricParams.Wcett.LossInterval;
    Time Now = KeQueryInterruptTime();
    
    ASSERT(Link->MetricInfo.Wcett.Etx.ProbeHistorySZ <= MEPH);
    ASSERT(Link->MetricInfo.Wcett.Etx.PHStart < MEPH);

    while (((Now - Etx->PH[Etx->PHStart].RcvdTS) >= LI) && (Link->MetricInfo.Wcett.Etx.ProbeHistorySZ > 0)) {
        Etx->PHStart = (Etx->PHStart + 1) % MEPH;
        Link->MetricInfo.Wcett.Etx.ProbeHistorySZ--;
    }

    ASSERT(Link->MetricInfo.Wcett.Etx.PHStart < MEPH);
}

//* WcettAddProbe
//
//  Adds a probe to probe history of this link.
//  And while we are at it, also remove old probes
//  from the history.
//
//  Called with LinkCache locked.
//
NDIS_STATUS
WcettAddProbe(
    MiniportAdapter *VA,
    Link *Link)
{
    Time Now = KeQueryInterruptTime();
    Etx *Etx = &(Link->MetricInfo.Wcett.Etx);
    uint MEPH = MAX_ETX_PROBE_HISTORY;
    NDIS_STATUS Status;

    if (Etx->ProbeHistorySZ == MEPH) {
        KdPrint(("MCL!WcettAddProbe:Too many probes.\n"));
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
    // does not exist and so WcettFindDeliveryCounts will not be called.
    //
    WcettRemoveOldProbes(VA, Link);
    return Status;
}

//* WcettFindDeliveryCounts
//
//  Given a link from this node, finds the forward and reverse delivery counts.
//  These numbers are kept in the reverse link.
//
//  Called with the LinkCache locked.
//
void
WcettFindDeliveryCounts(
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
            // and so WcettAddProbe has not been called recently.
            //
            WcettRemoveOldProbes(VA, RevLink);
            *FwdDeliv = RevLink->MetricInfo.Wcett.Etx.FwdDeliv;
            *RevDeliv = RevLink->MetricInfo.Wcett.Etx.ProbeHistorySZ;
            break;
        }
    }
}

//* WcettUpdateMetric
//
//  Update the metric for a given link.
//
//  Only called on adjacent links, meaning the source of the link is node 0.
//
//  Called with the LinkCache locked.
//
void
WcettUpdateMetric(
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
        SuccessProb /= VA->MetricParams.Wcett.PenaltyFactor;
        if (SuccessProb == 0)
            SuccessProb = 1;

        Link->wcett.LossProb = 4096 - SuccessProb;

        ASSERT(Link->wcett.LossProb < 4096);
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

        if (Link->MetricInfo.Wcett.Etx.TotSentProbes == 0) {
            //
            // We are just getting started and have no data.
            //
            Prob = DEFAULT_WCETT_INITIAL;
        }
        else {
            uint NumProbes;
            uint FwdDeliv, RevDeliv;

            //
            // Recall that we randomize probe send times by adding 0-25%
            // of the probe interval.  So, we need to account for that by
            // adding expected number of probes - 12.5%.
            //
            NumProbes = VA->MetricParams.Wcett.LossInterval / (VA->MetricParams.Wcett.ProbePeriod + (VA->MetricParams.Wcett.ProbePeriod >> 3)) ;
            ASSERT(NumProbes != 0);

            //
            // When the link is first created and  ProbeHistorySZ/FwdDeliv
            // are ramping up, we must adjust NumProbes appropriately.
            //
            if (Link->MetricInfo.Wcett.Etx.TotSentProbes < NumProbes)
                NumProbes = Link->MetricInfo.Wcett.Etx.TotSentProbes;

            //
            // Find the forward and reverse delivery counts,
            // from the reverse link.
            //
            WcettFindDeliveryCounts(VA, Link, &FwdDeliv, &RevDeliv);

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

        Link->MetricInfo.Wcett.Etx.LastProb = Prob;
        //
        // Average loss probability into the old value.
        // LossProb = Alpha * New + (1 - Alpha) * Old
        // Alpha is interpreted as Alpha/MAXALPHA.
        //
        Prob = (VA->MetricParams.Wcett.Alpha * Prob +
                ((MAXALPHA - VA->MetricParams.Wcett.Alpha) * Link->wcett.LossProb))
            / MAXALPHA;

        //
        // The 12-bit field can not hold a loss probability of 1.0 (4096).
        //
        if (Prob >= 4096)
            Prob = 4095;
        Link->wcett.LossProb = Prob;
    }
}

//* WcettPenalize
//
//  Takes Penalty due to data packet drop.
//
//  Only called on adjacent links, meaning the source of the link is node 0.
//
//  Called with the LinkCache locked.
//
void
WcettPenalize(
    MiniportAdapter *VA,
    Link *Adj)
{
    WcettUpdateMetric(VA, Adj, TRUE);
}

//* WcettUpdateFwdDeliv
//
//  Update the forward delivery ratio using received info.
//
//  Called with LinkCache locked.
//
void
WcettUpdateFwdDeliv(
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
            Adj->MetricInfo.Wcett.Etx.FwdDeliv = BP->Entry[i].Rcvd;
            break;
        }
    }
}

//* WcettUpdateMin
//
//  Update the current min reading for this link.
//  Only called on adjacent links, meaning the source of the link is node 0.
//
//  Called with the LinkCache locked.
//
static void
WcettUpdateMin(
    Link *Link,
    Time OutDelta)
{
    uint Delay;

    Delay = (uint)OutDelta;
    Link->MetricInfo.Wcett.PktPair.LastPktPair = Delay;

    //
    // Is this a valid sample?
    //
    if ((Delay != 0) &&
        ((Link->MetricInfo.Wcett.MaxBandwidth == 0) ||
         (WcettConvertPktPairDelayToBandwidth(Delay)
                                <= Link->MetricInfo.Wcett.MaxBandwidth))) {
        // 
        // Yes, it is a valid sample.
        // Is it less than current minimum?
        //
        if (Delay < Link->MetricInfo.Wcett.PktPair.CurrMin)
            Link->MetricInfo.Wcett.PktPair.CurrMin = Delay;
        Link->MetricInfo.Wcett.NumPktPairValid++;
    }
    else {
        //
        // No, the resulting bandwidth is too large.
        //
        Link->MetricInfo.Wcett.NumPktPairInvalid++;
    }
}

//* WcettUpdateBandwidth
//
//  Update the bandwidth measurement on the given link.
//
//  Called with the LinkCache locked.
//
static void
WcettUpdateBandwidth(
    Link *Link)
{
    uint Bandwidth;

    if (Link->MetricInfo.Wcett.PktPair.CurrMin == (uint)-1) {
        //
        // We have received no valid packet-pair samples.
        //
        Bandwidth = Link->MetricInfo.Wcett.MaxBandwidth;
        Bandwidth = WcettDefaultBandwidth(Bandwidth);
    }
    else {
        uint Delay;

        //
        // Calculate the bandwidth using the minimum delay sample
        // from the last sample period.
        //
        Delay = Link->MetricInfo.Wcett.PktPair.CurrMin;
        Bandwidth = WcettConvertPktPairDelayToBandwidth(Delay);
    }

    Link->wcett.Bandwidth = WcettEncodeBandwidth(Bandwidth);
}

//* WcettSendEtxProbe
//
//  Sends a broadcast probe. 
//
Time
WcettSendEtxProbes(
    MiniportAdapter *VA,
    Time Now)
{
    SRPacket *Packet;
    NDIS_STATUS Status;
    LARGE_INTEGER Timestamp;
    LARGE_INTEGER Frequency;
    LinkCache *LC = VA->LC; 
    Link *Adjacent;
    KIRQL OldIrql;
    
    //
    // Create a probe packet, and send it. It is a broadcast packet,
    // so no need to loop through links individually. It will go out
    // on all links.
    // 

    if (VA->MetricParams.Wcett.ProbeTimeout <= Now) {
        //
        // Create a probe with current timestamp.
        //
        Timestamp = KeQueryPerformanceCounter(&Frequency);
        Status = WcettCreateProbePacket(VA, Timestamp.QuadPart, &Packet, 0);

        if (Status == NDIS_STATUS_SUCCESS) {
            //
            // Send the packet.
            //
            WcettForwardProbe(VA, Packet);

            //
            // Recompute the metric on all links from us. 
            //
            KeAcquireSpinLock(&LC->Lock, &OldIrql);
            for (Adjacent = LC->nodes[0].AdjOut; 
                 Adjacent != NULL; 
                 Adjacent = Adjacent->NextOut) {

                WcettUpdateMetric(VA, Adjacent, FALSE);
                Adjacent->MetricInfo.Wcett.Etx.TotSentProbes++;
            }
            KeReleaseSpinLock(&LC->Lock, OldIrql);
        }

        //
        // Calculate next probe timeout. Randomize by adding 25% delay. 
        //

        ASSERT((VA->MetricParams.Wcett.ProbePeriod >> 2) > 0);

        VA->MetricParams.Wcett.ProbeTimeout = Now + 
            VA->MetricParams.Wcett.ProbePeriod +
            GetRandomNumber(VA->MetricParams.Wcett.ProbePeriod >> 2);

        ASSERT(VA->MetricParams.Wcett.ProbeTimeout > Now);
    }

    return VA->MetricParams.Wcett.ProbeTimeout;  
}


//* WcettSendPktPairProbes
//
//  Sends probes to adjacent nodes as needed.
//
Time
WcettSendPktPairProbes(
    MiniportAdapter *VA,
    Time Now)
{
    LinkCache *LC = VA->LC;
    NDIS_PACKET *ProbePackets = NULL;
    NDIS_PACKET *FirstPkt, *SecondPkt, *Packet;
    Link *Adj;
    Time Timeout;
    KIRQL OldIrql;
    NDIS_STATUS Status;
    uint Diff;

    Timeout = MAXTIME;
        
    KeAcquireSpinLock(&LC->Lock, &OldIrql);

    //
    // Loop through outgoing links, creating a probe pair for each,
    // when warranted by timeout value. Store the probe packets temporarily 
    // on a list since we can't send them while holding the lock on the 
    // link cache.
    //
    for (Adj = LC->nodes[0].AdjOut;
         Adj != NULL;
         Adj = Adj->NextOut) {

        //
        // If the loss probability is too large,
        // then we do not waste bandwidth with packet-pair probes.
        //
        if (WcettIsInfinite(Adj->Metric))
            continue;

        if (Adj->MetricInfo.Wcett.PktPair.ProbeTimeout <= Now) {

            //
            // Is it time to calculate a new link bandwidth?
            //
            if ((Adj->MetricInfo.Wcett.PktPair.PairsSent % VA->MetricParams.Wcett.PktPairMinOverProbes) == 0) {
                //
                // Update the bandwidth using CurrMin.
                //
                WcettUpdateBandwidth(Adj);

                //
                // Reset CurrMin to a large number.
                //
                Adj->MetricInfo.Wcett.PktPair.CurrMin = (uint)-1;
            }

            //
            // Create two packets, one with Seq and another with Seq + 1. 
            //
            Status = PktPairCreateProbePacket(VA, Adj, 0, &FirstPkt,
                                              Adj->MetricInfo.Wcett.PktPair.ProbeSeq, FALSE);
            if (Status == NDIS_STATUS_SUCCESS) {
                Status = PktPairCreateProbePacket(VA, Adj, 0, &SecondPkt, 
                                                  Adj->MetricInfo.Wcett.PktPair.ProbeSeq + 1, TRUE);
                if (Status == NDIS_STATUS_SUCCESS) {
                    //
                    // We created both packets successfuly. Bump the 
                    // sequence number by two. 
                    //
                    Adj->MetricInfo.Wcett.PktPair.ProbeSeq += 2;
                    Adj->MetricInfo.Wcett.PktPair.PairsSent++;
                    Adj->MetricInfo.Wcett.PktPair.Outstanding = Adj->MetricInfo.Wcett.PktPair.ProbeSeq - 1;

                    //
                    // Queue the packets using the OrigPacket link.
                    // The queue is "backward", so insert second packet
                    // first.
                    //
                    PC(SecondPkt)->OrigPacket = ProbePackets;
                    PC(FirstPkt)->OrigPacket = SecondPkt;
                    ProbePackets = FirstPkt;
                }
                else {
                    //
                    // We failed to create the second probe. 
                    //
                    KdPrint(("MCL!PktPairSendProbes SECOND PktPairCreateProbePacket returned %x\n", Status));
                    PktPairSendProbeComplete(VA, FirstPkt, Status);
                }
            }
            else {
                //
                // We failed to create the first probe. 
                //
                KdPrint(("MCL!PktPairSendProbes FIRST PktPairCreateProbePacket returned %x\n", Status));
            }

            //
            // Calculate next probe timeout. Randomize by adding 25% delay. 
            //
            Diff = VA->MetricParams.Wcett.PktPairProbePeriod + GetRandomNumber(VA->MetricParams.Wcett.PktPairProbePeriod >> 2);
            Adj->MetricInfo.Wcett.PktPair.ProbeTimeout = Now + Diff; 
        }

        //
        // We need to keep track of only the earliest timeout.
        //
        if (Adj->MetricInfo.Wcett.PktPair.ProbeTimeout < Timeout)
            Timeout = Adj->MetricInfo.Wcett.PktPair.ProbeTimeout;
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);

    //
    // Loop through queued packets, sending out each probe.
    //
    while ((Packet = ProbePackets) != NULL) {
        ProbePackets = PC(Packet)->OrigPacket;
        PC(Packet)->OrigPacket = NULL;

        if (PC(Packet)->PA == NULL) {
            //
            // This means the packet is trying to use a physical adapter
            // that no longer exists. LinkCacheDeleteInterface has been called.
            //
            ASSERT(PC(Packet)->TransmitComplete == PktPairSendProbeComplete);
            PktPairSendProbeComplete(VA, Packet, NDIS_STATUS_FAILURE);

        } else {
            //
            // Transmit the packet.
            //
            ProtocolTransmit(PC(Packet)->PA, Packet);
        }
    }

    return Timeout;
}


//* WcettSendProbe
//
//  Sends one type of probe or the other.
//
Time
WcettSendProbes(
    MiniportAdapter *VA,
    Time Now)
{
    Time EtxTimeout, PktPairTimeout;

    EtxTimeout = WcettSendEtxProbes(VA, Now);
    PktPairTimeout = WcettSendPktPairProbes(VA, Now);

    //
    // Return whichever time is sooner.
    //
    if (EtxTimeout < PktPairTimeout)
        return EtxTimeout;
    else
        return PktPairTimeout;
}


//* WcettReceiveEtxProbe
//
//  Processes a received broadcast probe.
//
void
WcettReceiveEtxProbe(
    MiniportAdapter *VA,
    InternalProbe *Probe,
    LQSRIf InIf)
{
    LinkCache *LC = VA->LC; 
    Link *Adj;
    KIRQL OldIrql;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    //
    // First find the link on which it showed up.
    // The link should exist because ReceiveSRPacket calls
    // LinkCacheAddLink before WcettReceiveProbe.
    //
    for (Adj = LC->nodes[0].AdjIn; ; Adj = Adj->NextIn) {

        if (Adj == NULL) {
            KdPrint(("MCL!WcettReceiveEtxProbe: link not found\n"));
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
            Adj->MetricInfo.Wcett.Etx.TotRcvdProbes++;
            WcettAddProbe(VA, Adj);
            WcettUpdateFwdDeliv(VA, Adj, Probe);
            break;
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);
}


//* WcettReceivePktPairProbe
//
//  Receive a Probe, and Reply if necessary.
//
void
WcettReceivePktPairProbe(
    MiniportAdapter *VA, 
    InternalProbe *Probe)
{
    LinkCache *LC = VA->LC; 
    Link *Adj;
    Time Now, OutDelta;
    LARGE_INTEGER Timestamp;
    LARGE_INTEGER Frequency;
    KIRQL OldIrql;
    boolint SendProbeReply = FALSE; 

    //
    // Do not send a response to ourselves.
    //
    if (VirtualAddressEqual(Probe->Opt.From, VA->Address))
        return;
   
    Timestamp = KeQueryPerformanceCounter(&Frequency);
    Now = Timestamp.QuadPart;
    OutDelta = 0;

    //
    // When we receive a probe:
    //   - Find the link on which it showed up.
    //   - If the seq number is even, store current time
    //     and seq number.
    //   - If the seq number is odd, and if it is the next expected
    //     probe, send a reply. 
    //
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    for (Adj = LC->nodes[0].AdjIn;
         Adj != NULL;
         Adj = Adj->NextIn) {

        if (VirtualAddressEqual(Probe->Opt.From,
                                LC->nodes[Adj->sourceIndex].address) &&
            (Probe->Opt.OutIf == Adj->outif) &&
            (Probe->Opt.InIf == Adj->inif)) {

            if ((Probe->Opt.Seq % 2) == 0) {
                Adj->MetricInfo.Wcett.PktPair.TimeLastProbeRcvd = Now;
                Adj->MetricInfo.Wcett.PktPair.LastProbeSeq = Probe->Opt.Seq;
            }
            else if ((Adj->MetricInfo.Wcett.PktPair.TimeLastProbeRcvd != 0) &&
                     ((Adj->MetricInfo.Wcett.PktPair.LastProbeSeq + 1) == Probe->Opt.Seq)) {
                //
                // Calculate OutDelta - the packet-pair delay.
                // We must scale OutDelta to 100ns units.
                //
                OutDelta = Now - Adj->MetricInfo.Wcett.PktPair.TimeLastProbeRcvd;
                Adj->MetricInfo.Wcett.PktPair.TimeLastProbeRcvd = 0;

                if ((10000000L * OutDelta) < OutDelta) {
                    //
                    // We got an overly large OutDelta, so we
                    // can't report a reasonable number. Therefore, don't.
                    //
                    KdPrint(("MCL!WcettReceivePktPairProbe: Not sending reply due to OutDelta overflow.\n"));
                    break;
                }
                OutDelta = (10000000L * OutDelta)/Frequency.QuadPart;

                //
                // Remember to send probe reply, and update counters. 
                //
                SendProbeReply = TRUE;
                Adj->MetricInfo.Wcett.PktPair.RepliesSent++;
            }
            break;
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);

    if (SendProbeReply) {
        InterlockedIncrement((PLONG)&VA->CountXmitProbeReply);
        PktPairSendProbeReply(VA, Probe, Now, OutDelta);
    }
}    


//* WcettReceiveProbe
//
//  Receive a Probe. Called from ReceiveSRPacket.
//  We have already verified that the MetricType is WCETT.
//
void
WcettReceiveProbe(
    MiniportAdapter *VA,
    InternalProbe *Probe,
    LQSRIf InIf)
{
    ASSERT(Probe->Opt.MetricType == METRIC_TYPE_WCETT);

    if (Probe->Opt.ProbeType == METRIC_TYPE_ETX)
        WcettReceiveEtxProbe(VA, Probe, InIf);
    else if (Probe->Opt.ProbeType == METRIC_TYPE_PKTPAIR)
        WcettReceivePktPairProbe(VA, Probe);
}


//* WcettReceivePktPairProbeReply
//
//  Receive a Probe Reply.  Called from ReceiveSRPacket.
//
void
WcettReceivePktPairProbeReply(
    MiniportAdapter *VA,
    InternalProbeReply *ProbeReply)
{
    LinkCache *LC = VA->LC;
    Link *Adj;
    KIRQL OldIrql;
    PRPktPair *Special; 

    //
    // Loop through links to adjacent nodes, looking for the link upon
    // which the probe to which this probe reply is a response was sent.
    //
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    for (Adj = LC->nodes[0].AdjOut;
         Adj != NULL;
         Adj = Adj->NextOut) {

        if (VirtualAddressEqual(ProbeReply->Opt.From,
                                LC->nodes[Adj->targetIndex].address) &&
            (ProbeReply->Opt.OutIf == Adj->inif) &&
            (ProbeReply->Opt.InIf == Adj->outif)) {
            
            //
            // Found link this probe was sent on.
            // If it's a response for the last probe 
            // we sent, calculate the bandwidth on the link.
            //
            if (Adj->MetricInfo.Wcett.PktPair.Outstanding ==
                                                ProbeReply->Opt.Seq) {
                Special = (PRPktPair *)ProbeReply->Opt.Special; 
                Adj->MetricInfo.Wcett.PktPair.RepliesRcvd++;

                //
                // Update CurrMin from this sample.
                //
                WcettUpdateMin(Adj, Special->OutDelta);

                if (Adj->MetricInfo.Wcett.PktPair.PairsSent < VA->MetricParams.Wcett.PktPairMinOverProbes) {
                    //
                    // Update the bandwidth and keep accumulating CurrMin.
                    // This lets the bandwidth converge quickly
                    // after first discovering the link.
                    //
                    WcettUpdateBandwidth(Adj);
                }
            }
            else {
                //
                // We got a late probe reply. Throw it away.
                // This should not happen too often. 
                //
                KdPrint(("MCL!PktPairReceiveProbeReply LATE outstanding %u rcvd %u\n", 
                           Adj->MetricInfo.Wcett.PktPair.Outstanding, ProbeReply->Opt.Seq));
            }
            Adj->MetricInfo.Wcett.PktPair.Outstanding = 0;
            break;
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);
}
