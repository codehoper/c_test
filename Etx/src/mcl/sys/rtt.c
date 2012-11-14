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
// Abstract:
//
// Round-Trip Time measurement module.
//

#include "headers.h"

//
// Default RTT parameter values
//

// Scaled by MAXALPHA of 10, this becomes 0.1.
#define DEFAULT_RTT_ALPHA 1

// 0.5 second probe period. 
#define DEFAULT_RTT_PROBE_PERIOD (500 * MILLISECOND)

// 10 second hysteresis. 
#define DEFAULT_RTT_HYSTERESIS_PERIOD (10 * SECOND)

// RTT penalty factor of 3.
#define DEFAULT_RTT_PENALTY_FACTOR 3            

// Sweep period of 5ms. 
#define DEFAULT_RTT_SWEEP_PERIOD (5 * MILLISECOND)

// By default, RTT-Random is FALSE.
#define DEFAULT_RTT_RANDOM FALSE

// By default, we do not override link decisions.
#define DEFAULT_RTT_OUTIF_OVERRIDE FALSE

// Initial RTT estimate.
#define INITIAL_RTT_ESTIMATE (2 * MILLISECOND)

// Values larger than this are considered infinite.
#define DEFAULT_RTT_INFINITY (10 * MILLISECOND)

//* RttIsInfinite
//
//  Returns TRUE if the link metric indicates that the link
//  is effectively broken.
//
boolint
RttIsInfinite(uint Metric)
{
    return (Metric > DEFAULT_RTT_INFINITY);
}

//* RttInitLinkMetric
//
//  Init metric information for a new link.
//
void 
RttInitLinkMetric(
    MiniportAdapter *VA,
    int SNode,
    Link *Link, 
    Time Now)
{
    UNREFERENCED_PARAMETER(SNode);

    //
    // The basic metric. 
    //
    Link->Metric = INITIAL_RTT_ESTIMATE;

    //
    // RTT-specific info.
    //
    Link->MetricInfo.Rtt.SentProbes = 0;
    Link->MetricInfo.Rtt.LostProbes = 0;
    Link->MetricInfo.Rtt.LateProbes = 0;
    Link->MetricInfo.Rtt.RawMetric = INITIAL_RTT_ESTIMATE;
    Link->MetricInfo.Rtt.LastRTT = 0; 
    Link->MetricInfo.Rtt.ProbeTimeout = Now + 
                                        VA->MetricParams.Rtt.ProbePeriod +
                                        GetRandomNumber(VA->MetricParams.Rtt.ProbePeriod >> 2);
    Link->MetricInfo.Rtt.HysteresisTimeout = Now + 
                                             VA->MetricParams.Rtt.HysteresisPeriod + 
                                             GetRandomNumber(VA->MetricParams.Rtt.HysteresisPeriod);
}

//* RttInit
// 
//  Called by MiniportInitialize.
//
void RttInit(
    MiniportAdapter *VA) 
{
    Time Now; 

    Now = KeQueryInterruptTime(); 

    VA->IsInfinite = RttIsInfinite;
    VA->ConvMetric = MiniportConvMetric;
    VA->InitLinkMetric = RttInitLinkMetric;
    VA->PathMetric = MiniportPathMetric;

    VA->MetricParams.Rtt.Alpha = DEFAULT_RTT_ALPHA;
    VA->MetricParams.Rtt.ProbePeriod = DEFAULT_RTT_PROBE_PERIOD; 
    VA->MetricParams.Rtt.HysteresisPeriod = DEFAULT_RTT_HYSTERESIS_PERIOD; 
    VA->MetricParams.Rtt.PenaltyFactor = DEFAULT_RTT_PENALTY_FACTOR; 
    VA->MetricParams.Rtt.SweepPeriod = DEFAULT_RTT_SWEEP_PERIOD;
    VA->MetricParams.Rtt.SweepTimeout = Now + VA->MetricParams.Rtt.SweepPeriod ;
    VA->MetricParams.Rtt.Random = DEFAULT_RTT_RANDOM; 
    VA->MetricParams.Rtt.OutIfOverride = DEFAULT_RTT_OUTIF_OVERRIDE; 
}

//* RttUpdateRawMetric
//
//  Update the raw metric for this link, which is a weighted average of rtt
//  measurements.  Assumes that it is never called more than 429 or so
//  seconds since previous measurement.
//
//  Only called on adjacent links, meaning the source of the link is node 0.
//
//  Called with the LinkCache locked.
//
static void
RttUpdateRawMetric(
    MiniportAdapter *VA,
    Link *Link,
    LARGE_INTEGER Timestamp,
    LARGE_INTEGER Frequency,
    boolint Penalty)
{
    uint Delta;
    
    if (!Penalty) { 
        //
        // If this is not a penalty, then use Delta.
        // First subtract the counters and then convert to 100ns units.
        // Avoids overflow problems.
        // Then convert to metric by throwing away the high bits.
        //
        Delta = (uint)((10000000L * (Timestamp.QuadPart - Link->MetricInfo.Rtt.LastProbe.QuadPart)) / Frequency.QuadPart);
    }
    else {
        //
        // Otherwise take penalty. 
        //
        Delta = VA->MetricParams.Rtt.PenaltyFactor * Link->MetricInfo.Rtt.RawMetric;
    }

    // 
    // Store instantaneous RTT.
    //
    Link->MetricInfo.Rtt.LastRTT = Delta; 

    if ((Link->MetricInfo.Rtt.RawMetric != 0) && (Link->MetricInfo.Rtt.RawMetric != (uint)-1)) {
        uint New, Old;

        //
        // RawMetric = Alpha * Delta + (1 - Alpha) * RawMetric.
        // Alpha is interpreted as Alpha/MAXALPHA.
        //
        New = (Delta * VA->MetricParams.Rtt.Alpha) / MAXALPHA;
        Old = (Link->MetricInfo.Rtt.RawMetric * (MAXALPHA - VA->MetricParams.Rtt.Alpha)) / MAXALPHA;
        Link->MetricInfo.Rtt.RawMetric = New + Old;
    }
    else {
        Link->MetricInfo.Rtt.RawMetric = Delta;
        Link->Metric = Delta;
    }
}

//* RttPenalize
//  
//  Takes Penalty due to data packet drop. 
//
//  Only called on adjacent links, meaning the source of the link is node 0.
//
//  Called with the LinkCache locked.
//
void
RttPenalize(
    MiniportAdapter *VA,
    Link *Link) 
{
    LARGE_INTEGER Timestamp;
    LARGE_INTEGER Frequency;

    Timestamp = KeQueryPerformanceCounter(&Frequency);
    RttUpdateRawMetric(VA, Link, Timestamp, Frequency, TRUE);
}

//* RttSendProbeComplete
//
//  Called after sending a probe.  Cleans up after RttCreateProbePacket.
//
static void
RttSendProbeComplete(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet,
    NDIS_STATUS Status)
{
    SRPacket *srp = PC(Packet)->srp;

    UNREFERENCED_PARAMETER(VA);
    UNREFERENCED_PARAMETER(Status);

    NdisFreePacketClone(Packet);
    SRPacketFree(srp);
}


//* RttCreateProbePacket
//
//  Creates a packet to send a probe.
//
static NDIS_STATUS
RttCreateProbePacket(
    MiniportAdapter *VA,
    Link *Adjacent,
    LARGE_INTEGER Timestamp,
    NDIS_PACKET **ReturnPacket)
{
    SRPacket *srp;
    NDIS_PACKET *Packet;
    NDIS_STATUS Status;

    InterlockedIncrement((PLONG)&VA->CountXmitProbe);

    //
    // Initialize an SRPacket for the Probe.
    // The Probe carries no data so it does not need an IV.
    //
    srp = ExAllocatePool(NonPagedPool, sizeof *srp);
    if (srp == NULL) {
        return NDIS_STATUS_RESOURCES;
    }
    RtlZeroMemory(srp, sizeof *srp);

    //
    // Initialize the Probe option.
    //
    srp->Probe = ExAllocatePool(NonPagedPool, sizeof *srp->Probe);
    if (srp->Probe == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto FreesrpAndExit;
    }
    RtlZeroMemory(srp->Probe, sizeof *srp->Probe);
    srp->Probe->Opt.OptionType = LQSR_OPTION_TYPE_PROBE;

    //
    // For RTT, we do not have any special data. 
    // So probe length is equal to PROBE_LEN. 
    //
    srp->Probe->Opt.OptDataLen = PROBE_LEN;
    srp->Probe->Opt.MetricType = VA->MetricType;
    srp->Probe->Opt.Timestamp = Timestamp.QuadPart;
    RtlCopyMemory(srp->Probe->Opt.From, VA->Address, sizeof(VirtualAddress));
    RtlCopyMemory(srp->Probe->Opt.To,
                  VA->LC->nodes[Adjacent->targetIndex].address,
                  sizeof(VirtualAddress));
    srp->Probe->Opt.OutIf = Adjacent->outif;
    srp->Probe->Opt.InIf = Adjacent->inif;
    srp->Probe->Opt.Metric = Adjacent->Metric;

    //
    // Initialize the source & destination of this packet.
    //
    RtlCopyMemory(srp->Dest, srp->Probe->Opt.To, sizeof(VirtualAddress));
    RtlCopyMemory(srp->Source, VA->Address, sizeof(VirtualAddress));

    //
    // Check for other options that can be piggy-backed on this packet.
    //
    PbackSendPacket(VA, srp);

    //
    // Convert the SRPacket to an NDIS Packet.
    //
    Status = SRPacketToPkt(VA, srp, &Packet);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto FreesrpAndExit;
    }

    PC(Packet)->srp = srp;
    PC(Packet)->TransmitComplete = RttSendProbeComplete;

    *ReturnPacket = Packet;
    return NDIS_STATUS_SUCCESS;

FreesrpAndExit:
    SRPacketFree(srp);
    return Status;
}

//* RttSendProbes
//
//  Sends probes to adjacent nodes as needed. Called from MiniportTimeout.
//
Time
RttSendProbes(
    MiniportAdapter *VA,
    Time Now)
{
    LinkCache *LC = VA->LC;
    NDIS_PACKET *ProbePackets = NULL;
    NDIS_PACKET *Packet;
    Link *Adjacent;
    LARGE_INTEGER Timestamp;
    LARGE_INTEGER Frequency;
    Time Timeout;
    KIRQL OldIrql;
    NDIS_STATUS Status;

    Timeout = MAXTIME;

    KeAcquireSpinLock(&LC->Lock, &OldIrql);

    Timestamp = KeQueryPerformanceCounter(&Frequency);

    //
    // Loop through outgoing links, creating a probe packet for each,
    // when warranted by timeout value. Store the probe packets temporarily 
    // on a list since we can't send them while holding the lock on the 
    // link cache.
    //
    for (Adjacent = LC->nodes[0].AdjOut;
         Adjacent != NULL;
         Adjacent = Adjacent->NextOut) {
        //
        // If the link metric is too large (effectively infinite),
        // then we do not probe it.
        //
        if (RttIsInfinite(Adjacent->Metric))
            continue;

        // 
        // Is it time to probe this link?
        //
        if (Adjacent->MetricInfo.Rtt.ProbeTimeout <= Now) {
            if (Adjacent->MetricInfo.Rtt.LastProbe.QuadPart != 0) {
                //
                // Previous Probe still outstanding.
                // Update metric as if we received the response now.
                // Note that this will happen only on the first
                // packet. Hopefully, for all packets lost later, 
                // the SweepTimeout will kick in and handle it.
                //
                Adjacent->MetricInfo.Rtt.LostProbes++;
                RttUpdateRawMetric(VA, Adjacent, Timestamp, Frequency, FALSE);
            }

            Adjacent->MetricInfo.Rtt.LastProbe = Timestamp;

            Status = RttCreateProbePacket(VA, Adjacent, Timestamp, &Packet);
            if (Status == NDIS_STATUS_SUCCESS) {
                //
                // Queue the packet using the OrigPacket link.
                //
                PC(Packet)->OrigPacket = ProbePackets;
                ProbePackets = Packet;
                Adjacent->MetricInfo.Rtt.SentProbes++;
            }

            //
            // Calculate next probe timeout. Randomize by adding 20% delay. 
            //
            Adjacent->MetricInfo.Rtt.ProbeTimeout = Now + VA->MetricParams.Rtt.ProbePeriod +
                                        GetRandomNumber(VA->MetricParams.Rtt.ProbePeriod * 20 / 100);
        }

        //
        // We need to keep track of only the earliest timeout.
        //
        if (Adjacent->MetricInfo.Rtt.ProbeTimeout < Timeout)
            Timeout = Adjacent->MetricInfo.Rtt.ProbeTimeout;
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
            ASSERT(PC(Packet)->TransmitComplete == RttSendProbeComplete);
            RttSendProbeComplete(VA, Packet, NDIS_STATUS_FAILURE);

        } else {
            //
            // Transmit the packet.
            //
            ProtocolTransmit(PC(Packet)->PA, Packet);
        }
    }

    return Timeout;
}

//* RttReceiveProbe
//
//  Receives a probe. Called from ReceiveSRPacket.
//  We have already verfied that this is indeed a
//  RTT probe. So, just verify that it is not a probe
//  we sent to oursleves, and call RttSendProbeReply. 
//
void
RttReceiveProbe(
    MiniportAdapter *VA,
    InternalProbe *Probe) {
        
    ASSERT(Probe->Opt.MetricType == METRIC_TYPE_RTT);

    //
    // Do not send a response to ourselves.
    //
    if (VirtualAddressEqual(Probe->Opt.From, VA->Address))
        return;
    
    // 
    // Send reply. 
    //
    RttSendProbeReply(VA, Probe);
}

//* RttSendProbeReply
//
//  Send a Probe Reply message in response to a Probe.
//
void
RttSendProbeReply(
    MiniportAdapter *VA,
    InternalProbe *Probe)
{
    SRPacket *srp;
    NDIS_PACKET *Packet;
    NDIS_STATUS Status;

    InterlockedIncrement((PLONG)&VA->CountXmitProbeReply);

    //
    // Initialize an SRPacket for the Probe Reply.
    // The Probe Reply carries no payload so it does not need an IV.
    //
    srp = ExAllocatePool(NonPagedPool, sizeof *srp);
    if (srp == NULL)
        return;
    RtlZeroMemory(srp, sizeof *srp);

    //
    // Initialize the Probe Reply option.
    //
    srp->ProbeReply = ExAllocatePool(NonPagedPool, sizeof *srp->ProbeReply);
    if (srp->ProbeReply == NULL)
        goto FreesrpAndExit;
    RtlZeroMemory(srp->ProbeReply, sizeof *srp->ProbeReply);
    srp->ProbeReply->Opt.OptionType = LQSR_OPTION_TYPE_PROBEREPLY;

    //
    // RTT probe reply has no additional special data, so 
    // length is PROBE_REPLY_LEN. 
    //
    srp->ProbeReply->Opt.OptDataLen = PROBE_REPLY_LEN;
    srp->ProbeReply->Opt.MetricType = VA->MetricType; 
    srp->ProbeReply->Opt.Timestamp = Probe->Opt.Timestamp;
    RtlCopyMemory(srp->ProbeReply->Opt.From, VA->Address,
                  sizeof(VirtualAddress));
    RtlCopyMemory(srp->ProbeReply->Opt.To, Probe->Opt.From,
                  sizeof(VirtualAddress));
    srp->ProbeReply->Opt.OutIf = Probe->Opt.InIf;
    srp->ProbeReply->Opt.InIf = Probe->Opt.OutIf;

    //
    // Initialize the source & destination of this packet.
    //
    RtlCopyMemory(srp->Dest, Probe->Opt.From, sizeof(VirtualAddress));
    RtlCopyMemory(srp->Source, VA->Address, sizeof(VirtualAddress));

    //
    // Check for other options that can be piggy-backed on this packet.
    //
    PbackSendPacket(VA, srp);

    //
    // Convert the SRPacket to an NDIS Packet.
    //
    Status = SRPacketToPkt(VA, srp, &Packet);
    if (Status != NDIS_STATUS_SUCCESS)
        goto FreesrpAndExit;

    PC(Packet)->srp = srp;

    //
    // Reuse completion routine from RttSendProbe.
    //
    PC(Packet)->TransmitComplete = RttSendProbeComplete;

    if (PC(Packet)->PA == NULL) {
        //
        // This means the packet is trying to use a physical adapter that
        // no longer exists.  Just free the packet and bail.
        //
        RttSendProbeComplete(VA, Packet, NDIS_STATUS_FAILURE);

    } else {
        //
        // Transmit the packet.
        //
        ProtocolTransmit(PC(Packet)->PA, Packet);
    }
    return;

FreesrpAndExit:
    SRPacketFree(srp);
    return;
}


//* RttReceiveProbeReply
//
//  Handle an incoming Probe Reply message.
//
void
RttReceiveProbeReply(
    MiniportAdapter *VA,
    InternalProbeReply *ProbeReply)
{
    LinkCache *LC;
    Link *Adjacent;
    KIRQL OldIrql;
    
    ASSERT(ProbeReply->Opt.MetricType == METRIC_TYPE_RTT);

    //
    // If this probe is for a different metric type than
    // what we are currently using, do not process it. 
    // 
    if (ProbeReply->Opt.MetricType != VA->MetricType)
        return;

    //
    // Loop through links to adjacent nodes, looking for the link upon
    // which the probe to which this probe reply is a response was sent.
    //
    LC = VA->LC;
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    for (Adjacent = LC->nodes[0].AdjOut; Adjacent != NULL;
         Adjacent = Adjacent->NextOut) {

        if (VirtualAddressEqual(ProbeReply->Opt.From,
                                LC->nodes[Adjacent->targetIndex].address) &&
            (ProbeReply->Opt.OutIf == Adjacent->inif) &&
            (ProbeReply->Opt.InIf == Adjacent->outif)) {

            //
            // Found link this probe was sent on.
            // If it's a response for the last probe sent, take measurement.
            //
            if ((Time)Adjacent->MetricInfo.Rtt.LastProbe.QuadPart ==
                ProbeReply->Opt.Timestamp) {
                LARGE_INTEGER Timestamp;
                LARGE_INTEGER Frequency;

                Timestamp = KeQueryPerformanceCounter(&Frequency);
                RttUpdateRawMetric(VA, Adjacent, Timestamp, Frequency, FALSE);
                Adjacent->MetricInfo.Rtt.LastProbe.QuadPart = 0;
            }
            else {
                Adjacent->MetricInfo.Rtt.LateProbes++;
            }
            break;
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);
}

//* RttProbeTimeout
// 
//  Returns true if the last probe timed out.
//  Otherwise, returns false. 
//
boolint
RttProbeTimeout(
    MiniportAdapter *VA,
    Link *Link,
    LARGE_INTEGER Frequency,
    LARGE_INTEGER Timestamp)
{
    uint Delta; 

    //
    // First subtract the counters and then convert to 100ns units.
    // Avoids overflow problems.
    // Then convert to metric by throwing away the high bits.
    //
    Delta = (uint)((10000000L * (Timestamp.QuadPart - Link->MetricInfo.Rtt.LastProbe.QuadPart)) / Frequency.QuadPart);
   
    // 
    // If Delta is larger than PenaltyFactor * Link->RawMetric, this packet is
    // assumed to be lost, and we take the penalty.
    // 
    if (Delta > (VA->MetricParams.Rtt.PenaltyFactor * Link->MetricInfo.Rtt.RawMetric)) {
        return TRUE;       
    }
    return FALSE;
}

//* RttSweepForLateProbes
//
//  The function sweeps for late probes, and
//  takes penalty for any that were late. It is
//  called from  DPC level by MiniportTimeout.
//
Time
RttSweepForLateProbes(
    MiniportAdapter *VA,
    Time Now)
{
    LinkCache *LC = VA->LC;
    Link *Adjacent;
    LARGE_INTEGER Timestamp;
    LARGE_INTEGER Frequency;
    KIRQL OldIrql;
   
    UNREFERENCED_PARAMETER(Now);

    KeAcquireSpinLock(&LC->Lock, &OldIrql);
   
    if (VA->MetricParams.Rtt.SweepTimeout > Now)
        goto DoNotSweep;

    Timestamp = KeQueryPerformanceCounter(&Frequency);
    
    //
    // Loop through outgoing links.
    //
    for (Adjacent = LC->nodes[0].AdjOut;
         Adjacent != NULL;
         Adjacent = Adjacent->NextOut) {

        //
        // If the link metric is > 0, and if the
        // previous Probe still outstanding, then
        // update metric with penalty. 
        //
        if ((Adjacent->MetricInfo.Rtt.RawMetric > 0) && (Adjacent->MetricInfo.Rtt.LastProbe.QuadPart != 0)) {
            if (RttProbeTimeout(VA, Adjacent, Frequency, Timestamp)) {
                Adjacent->MetricInfo.Rtt.LostProbes++;
                Adjacent->MetricInfo.Rtt.LastProbe.QuadPart = 0;
                RttUpdateRawMetric(VA, Adjacent, Timestamp, Frequency, TRUE);
            }
        }
    }

    //
    // Reset the timeout.
    //
    VA->MetricParams.Rtt.SweepTimeout = Now + VA->MetricParams.Rtt.SweepPeriod;
DoNotSweep:
    KeReleaseSpinLock(&LC->Lock, OldIrql);
    return VA->MetricParams.Rtt.SweepTimeout;
}

//* RttUpdateMetricForTarget
//
//  Updates the metric for specified link.
//
//  Called with the LinkCache locked.
//
static void
RttUpdateMetricForTarget(
    MiniportAdapter *VA,
    Link *AdjLink)
{
    //
    // If Random is enabled, just pick a
    // random time. Otherwise, update properly. 
    //
    if (VA->MetricParams.Rtt.Random) {
        //
        // Since we are going to pick a random number,
        // start with a max limit that's large enough so that
        // we will get some useful distinction among links. Apart
        // from that, it has no meaning.
        //
        AdjLink->Metric = GetRandomNumber(100000); 
    }
    else {
        AdjLink->Metric = AdjLink->MetricInfo.Rtt.RawMetric; 
    }
}

//* RttUpdateMetric
//
//  Implements hysteresis. Called from MiniportTimeout after HysteresisTimeout
//  copies RawMetric into Metric for all adjacent nodes. 
//
Time
RttUpdateMetric(
    MiniportAdapter *VA,
    Time Now)
{
    LinkCache *LC = VA->LC;
    Link *Adjacent;
    Time Timeout;
    KIRQL OldIrql;

    Timeout = MAXTIME; 

    KeAcquireSpinLock(&LC->Lock, &OldIrql);

    //
    // Loop through outgoing links, and see whose timer fired. 
    //
    for (Adjacent = LC->nodes[0].AdjOut;
         Adjacent != NULL;
         Adjacent = Adjacent->NextOut) {
        //
        // Is this link to be checked?
        //
        if (Adjacent->MetricInfo.Rtt.HysteresisTimeout <= Now) {
            //
            // Copy link metrics for all links to this 
            // address.
            //
            Adjacent->MetricInfo.Rtt.HysteresisTimeout =
                Now + VA->MetricParams.Rtt.HysteresisPeriod + 
                GetRandomNumber(VA->MetricParams.Rtt.HysteresisPeriod);
            RttUpdateMetricForTarget(VA, Adjacent);
        }
        if (Adjacent->MetricInfo.Rtt.HysteresisTimeout < Timeout)
            Timeout = Adjacent->MetricInfo.Rtt.HysteresisTimeout;
       
    }

    KeReleaseSpinLock(&LC->Lock, OldIrql);

    return Timeout;
}

//* RttSelectOutIf
//   
//  Selects outgoing interface for next hop based on minimum 
//  value of metric. 
//  
//  
void
RttSelectOutIf(
    MiniportAdapter *VA,
    const uchar *Dest, 
    LQSRIf *OutIf,
    LQSRIf *InIf)
{
    LinkCache *LC = VA->LC;
    Link *Adjacent;
    uint MinMetric; 

    MinMetric = MAXULONG;
    *OutIf = (uchar)-1;
    *InIf = (uchar)-1;

    for (Adjacent = LC->nodes[0].AdjOut;
         Adjacent != NULL;
         Adjacent = Adjacent->NextOut) {
        if (VirtualAddressEqual(Dest, LC->nodes[Adjacent->targetIndex].address)) {
            if (Adjacent->Metric < MinMetric) {
                MinMetric = Adjacent->Metric; 
                *OutIf = Adjacent->outif;
                *InIf = Adjacent->inif;
            }
        }
    }
}
