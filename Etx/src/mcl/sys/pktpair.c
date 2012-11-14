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
// Packet pair measurements.
//

#include "headers.h"

// 
// The basic algorithm is as follows: each node sends 
// two packets, back-to-back, to each of its neighbors. 
// The neighbor responds back to the second packet, 
// and reports the time difference between the two 
// receive events. If no response is received by the 
// time we are ready to send the next packet-pair, 
// the sender takes a penalty of 3 times the current 
// average.
// 

//
// Default PKT_PAIR parameter values.
//

// Scaled by MAXALPHA of 10, this becomes 0.1.  
#define DEFAULT_PKT_PAIR_ALPHA  1

// 2 second probe period. 
#define DEFAULT_PKT_PAIR_PROBE_PERIOD (2 * SECOND)

// PKT_PAIR penalty factor of 3.
#define DEFAULT_PKT_PAIR_PENALTY_FACTOR 3            

// Initial Packet pair estimate.
#define INITIAL_PKTPAIR_ESTIMATE (1 * MILLISECOND)

// Probe padding size.
#define PROBE_PADDING 1000

// Calculate min over this multiple of probe period.
#define MIN_OVER_PROBES 50

// Values larger than this are considered infinite.
#define DEFAULT_PKT_PAIR_INFINITY (30 * MILLISECOND)

//* PktPairIsInfinite
//
//  Returns TRUE if the link metric indicates that the link
//  is effectively broken.
//
boolint
PktPairIsInfinite(uint Metric)
{
    return (Metric > DEFAULT_PKT_PAIR_INFINITY);
}

//* PktPairInitLinkMetric
//
// Init metric informtion for a new link.
// Called from LinkCacheInitMetric.
//
void 
PktPairInitLinkMetric(
    MiniportAdapter *VA,
    int SNode,
    Link *Link, 
    Time Now)
{
    uint i; 

    UNREFERENCED_PARAMETER(SNode);

    //
    // The basic metric.
    //
    Link->Metric = INITIAL_PKTPAIR_ESTIMATE;

    //
    // PktPair sender-specific info.
    //
    Link->MetricInfo.PktPair.ProbeSeq = 0;
    Link->MetricInfo.PktPair.PairsSent = 0;
    Link->MetricInfo.PktPair.RepliesSent = 0;
    Link->MetricInfo.PktPair.RepliesRcvd = 0;
    Link->MetricInfo.PktPair.LostPairs = 0;
    Link->MetricInfo.PktPair.ProbeTimeout = Now + 
                                            VA->MetricParams.PktPair.ProbePeriod +
                                            GetRandomNumber(VA->MetricParams.PktPair.ProbePeriod >> 2);
    Link->MetricInfo.PktPair.Outstanding = 0; 
    Link->MetricInfo.PktPair.Delta = 0; 
    Link->MetricInfo.PktPair.RTT = INITIAL_PKTPAIR_ESTIMATE; 
    Link->MetricInfo.PktPair.LastRTT = 0; 
    Link->MetricInfo.PktPair.LastPktPair = 0; 

    //
    // PktPair receiver-specific info.
    //
    Link->MetricInfo.PktPair.LastProbeTimestamp = (uint)-1; 
    Link->MetricInfo.PktPair.TimeLastProbeRcvd = 0;
    Link->MetricInfo.PktPair.LastProbeSeq = (uint)-1;

    //
    // Min Time History. 
    //
    Link->MetricInfo.PktPair.CurrMin = (uint)-1; 
    Link->MetricInfo.PktPair.NumSamples = 0;
    Link->MetricInfo.PktPair.NextMinTime = Now + VA->MetricParams.PktPair.ProbePeriod * MIN_OVER_PROBES; 
    for (i = 0; i < MAX_PKTPAIR_HISTORY; i++) { 
        Link->MetricInfo.PktPair.MinHistory[i].Min = (uint)-1; 
        Link->MetricInfo.PktPair.MinHistory[i].Seq = (uint)-1;
    }
}

//* PktPairInit
// 
//  Called by MiniportInitialize.
//
void PktPairInit(
    MiniportAdapter *VA) 
{
    VA->IsInfinite = PktPairIsInfinite;
    VA->ConvMetric = MiniportConvMetric;
    VA->PathMetric = MiniportPathMetric;
    VA->InitLinkMetric = PktPairInitLinkMetric;

    VA->MetricParams.PktPair.Alpha = DEFAULT_PKT_PAIR_ALPHA;
    VA->MetricParams.PktPair.ProbePeriod = DEFAULT_PKT_PAIR_PROBE_PERIOD; 
    VA->MetricParams.PktPair.PenaltyFactor = DEFAULT_PKT_PAIR_PENALTY_FACTOR; 
}

//* PktPairUpdateMetric
//
//  Update the metric for this link, which is a weighted average of the 
//  OutDeltas. Also, since the receiver responds to the second probe immediately, 
//  we can compute the RTT metric "for free". We average it the same
//  way we average the OutDeltas.
//
//  Only called on adjacent links, meaning the source of the link is node 0.
//
//  Called with the LinkCache locked.
//
static void
PktPairUpdateMetric(
    MiniportAdapter *VA,
    Link *Link,
    Time OutDelta,
    Time SendTs,
    Time RcvTs,
    LARGE_INTEGER Frequency,
    boolint Penalty)
{
    uint PktPairDelta, RTTDelta;
    if (!Penalty) {
        //
        // If this is not a penalty, then use Delta.
        //
        PktPairDelta =  (uint)OutDelta;
        //
        // Update the min.
        //
        if (PktPairDelta < Link->MetricInfo.PktPair.CurrMin) {
            Link->MetricInfo.PktPair.CurrMin = PktPairDelta;
        }
        RTTDelta = (uint)((10000000L * (RcvTs - SendTs)) / Frequency.QuadPart);
    }
    else {
        //
        // Otherwise take penalty. CurrMin is set to (uint)-1. 
        //
        Link->MetricInfo.PktPair.CurrMin = (uint)-1;
        PktPairDelta = VA->MetricParams.PktPair.PenaltyFactor * Link->Metric;
        RTTDelta = VA->MetricParams.PktPair.PenaltyFactor * Link->MetricInfo.PktPair.RTT;
        Link->MetricInfo.PktPair.LostPairs ++;
    }

    Link->MetricInfo.PktPair.LastRTT = RTTDelta; 
    Link->MetricInfo.PktPair.LastPktPair = PktPairDelta; 

    if (Link->Metric != 0) {
        uint New, Old;
        //
        // Metric = Alpha * Delta + (1 - Alpha) * Metric
        // Alpha is interpreted as Alpha/MAXALPHA.
        // 
        New = (PktPairDelta * VA->MetricParams.PktPair.Alpha) / MAXALPHA;
        Old = (Link->Metric * (MAXALPHA - VA->MetricParams.PktPair.Alpha)) / MAXALPHA;
        Link->Metric = New + Old;
       
        //
        // Update RTT the same way. 
        //
        New = (RTTDelta * VA->MetricParams.PktPair.Alpha) / MAXALPHA;
        Old = (Link->MetricInfo.PktPair.RTT * (MAXALPHA - VA->MetricParams.PktPair.Alpha)) / MAXALPHA;
        Link->MetricInfo.PktPair.RTT = New + Old;
    }
    else {
        Link->Metric = PktPairDelta;
        Link->MetricInfo.PktPair.RTT = RTTDelta;
    }
}

//* PktPairPenalize
//  
//  Takes Penalty due to data packet drop. 
//
//  Only called on adjacent links, meaning the source of the link is node 0.
//
//  Called with the LinkCache locked.
//
void
PktPairPenalize(
    MiniportAdapter *VA,
    Link *Link) 
{
    LARGE_INTEGER Timestamp;
    LARGE_INTEGER Frequency;

    Timestamp = KeQueryPerformanceCounter(&Frequency);
    PktPairUpdateMetric(VA, Link, 0, 0, 0, Frequency, TRUE);
}

//* PktPairSendProbeComplete
//
//  Called after sending a probe.  Cleans up after PktPairCreateProbePacket.
//
void
PktPairSendProbeComplete(
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

//* PktPairFreePadding
//
//  Helper function when probe is padded.
//
static void
PktPairFreePadding(
    ProtocolAdapter *PA,
    NDIS_PACKET *Packet)
{
    UNREFERENCED_PARAMETER(PA);
    NdisFreePacketClone(Packet);
}

//* PktPairCreateProbePacket
//
//  Creates a packet to send a probe.
//
NDIS_STATUS
PktPairCreateProbePacket(
    MiniportAdapter *VA,
    Link *Adjacent,
    Time Timestamp,
    NDIS_PACKET **ReturnPacket,
    uint Seq,
    boolint LargeProbe)
{
    SRPacket *srp;
    NDIS_PACKET *Packet;
    NDIS_PACKET *ProbePadding;
    void *PaddingData;
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
    // Initialize the Probe option. The probe carries
    // only generic variables, so no need to allocate
    // extra space. 
    //
    srp->Probe = ExAllocatePool(NonPagedPool, sizeof *srp->Probe);
    if (srp->Probe == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto FreesrpAndExit;
    }
    RtlZeroMemory(srp->Probe, sizeof *srp->Probe);
    srp->Probe->Opt.OptionType = LQSR_OPTION_TYPE_PROBE;
    srp->Probe->Opt.OptDataLen = PROBE_LEN;
    srp->Probe->Opt.MetricType = VA->MetricType;
    srp->Probe->Opt.ProbeType = METRIC_TYPE_PKTPAIR;
    srp->Probe->Opt.Seq = Seq;
    srp->Probe->Opt.Timestamp = Timestamp;
    RtlCopyMemory(srp->Probe->Opt.From, VA->Address, sizeof(VirtualAddress));
    RtlCopyMemory(srp->Probe->Opt.To,
                  VA->LC->nodes[Adjacent->targetIndex].address,
                  sizeof(VirtualAddress));
    srp->Probe->Opt.OutIf = Adjacent->outif;
    srp->Probe->Opt.InIf = Adjacent->inif;
    srp->Probe->Opt.Metric = Adjacent->Metric;

    //
    // Create padding data based on whether LargeProbe is true or false.  
    // NB: MiniportMakeEmptyPacket is defined in headers.h.
    //
    if (LargeProbe) {
        Status = MiniportMakeEmptyPacket(VA, 
                                         PROBE_PADDING, 
                                         &ProbePadding, 
                                         &PaddingData);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto FreesrpAndExit;
        }

        //
        // Note that all-zero padding ensures that we
        // will fail decryption at the receiver, since we
        // do not have correct ethertype. But this is okay,
        // since there are no piggybacked options. 
        //
        RtlZeroMemory(PaddingData, PROBE_PADDING);
    
        srp->Packet = ProbePadding;
        srp->PayloadOffset = 0;
        srp->FreePacket = PktPairFreePadding;
    }

    //
    // Initialize the source & destination of this packet.
    //
    RtlCopyMemory(srp->Dest, srp->Probe->Opt.To, sizeof(VirtualAddress));
    RtlCopyMemory(srp->Source, VA->Address, sizeof(VirtualAddress));

    //
    // Convert the SRPacket to an NDIS Packet. Again, note that we have
    // not called PbackSendPacket. This ensures that it is okay to 
    // fail decryption at the receiver. Furthermore, without piggybacked 
    // options, we have a better estimate of packet size.
    //
    Status = SRPacketToPkt(VA, srp, &Packet);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto FreesrpAndExit;
    }

    PC(Packet)->srp = srp;
    PC(Packet)->TransmitComplete = PktPairSendProbeComplete;

    *ReturnPacket = Packet;
    return NDIS_STATUS_SUCCESS;

FreesrpAndExit:
    SRPacketFree(srp);
    return Status;
}

//* PktPairSendProbes
//
//  Sends probes to adjacent nodes as needed. Called from MiniportTimeout.
//
Time
PktPairSendProbes(
    MiniportAdapter *VA,
    Time Now)
{
    LinkCache *LC = VA->LC;
    NDIS_PACKET *ProbePackets = NULL;
    NDIS_PACKET *FirstPkt, *SecondPkt, *Packet;
    Link *Adjacent;
    Time Timeout;
    KIRQL OldIrql;
    NDIS_STATUS Status;
    LARGE_INTEGER Timestamp;
    LARGE_INTEGER Frequency;
    uint Diff;
    uint Index; 
    
    Timeout = MAXTIME;
        
    KeAcquireSpinLock(&LC->Lock, &OldIrql);

    //
    // Loop through outgoing links, creating a probe pair for each,
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
        if (PktPairIsInfinite(Adjacent->Metric))
            continue;

        //
        // First, check to see if it's time to store
        // the minimum. 
        //
        if (Adjacent->MetricInfo.PktPair.NextMinTime <= Now) {
            Index = Adjacent->MetricInfo.PktPair.NumSamples % MAX_PKTPAIR_HISTORY ;
            Adjacent->MetricInfo.PktPair.MinHistory[Index].Seq = Adjacent->MetricInfo.PktPair.NumSamples;
            Adjacent->MetricInfo.PktPair.MinHistory[Index].Min = Adjacent->MetricInfo.PktPair.CurrMin;
            Adjacent->MetricInfo.PktPair.CurrMin = (uint)-1;
            Adjacent->MetricInfo.PktPair.NumSamples++;
            Adjacent->MetricInfo.PktPair.NextMinTime +=  VA->MetricParams.PktPair.ProbePeriod * MIN_OVER_PROBES;
        }

        // 
        // Is it time to probe this link?
        //
        if (Adjacent->MetricInfo.PktPair.ProbeTimeout <= Now) {

            if (Adjacent->MetricInfo.PktPair.Outstanding != 0) {
                //
                // We have not received a reply to the last probe. 
                // Take penalty. 
                //
                Timestamp = KeQueryPerformanceCounter(&Frequency);
                PktPairUpdateMetric(VA, Adjacent, 0, 0, 0, Frequency, TRUE);
            }
            //
            // Create two packets, one with Seq and another with Seq + 1. 
            //
            Timestamp = KeQueryPerformanceCounter(&Frequency);
            Status = PktPairCreateProbePacket(VA, Adjacent, Timestamp.QuadPart, &FirstPkt, 
                                              Adjacent->MetricInfo.PktPair.ProbeSeq, FALSE);
            if (Status == NDIS_STATUS_SUCCESS) {
                Timestamp = KeQueryPerformanceCounter(&Frequency);
                Status = PktPairCreateProbePacket(VA, Adjacent, Timestamp.QuadPart, &SecondPkt, 
                                                  Adjacent->MetricInfo.PktPair.ProbeSeq + 1, TRUE);
                if (Status == NDIS_STATUS_SUCCESS) {
                    //
                    // We created both packets successfuly. Bump the 
                    // sequence number by two. 
                    //
                    Adjacent->MetricInfo.PktPair.ProbeSeq += 2;

                    //
                    // Queue the packets using the OrigPacket link.
                    // The queue is "backward", so insert second packet
                    // first.
                    //
                    PC(SecondPkt)->OrigPacket = ProbePackets;
                    ProbePackets = SecondPkt;
                    PC(FirstPkt)->OrigPacket = ProbePackets;
                    ProbePackets = FirstPkt;

                    Adjacent->MetricInfo.PktPair.PairsSent++;
                    Adjacent->MetricInfo.PktPair.Outstanding = Adjacent->MetricInfo.PktPair.ProbeSeq - 1; 
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
            Diff = VA->MetricParams.PktPair.ProbePeriod + GetRandomNumber(VA->MetricParams.PktPair.ProbePeriod >> 2);
            Adjacent->MetricInfo.PktPair.ProbeTimeout = Now + Diff; 
        }

        //
        // We need to keep track of only the earliest timeout.
        //
        if (Adjacent->MetricInfo.PktPair.ProbeTimeout < Timeout)
            Timeout = Adjacent->MetricInfo.PktPair.ProbeTimeout;
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


//* PktPairReceiveProbe
//
//  Receive a Probe, and Reply if necessary. Called from
//  ReceiveSRPacket. We have already verified that the 
//  MetricType is PKTPAIR. 
//
void
PktPairReceiveProbe(
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

    ASSERT(Probe->Opt.MetricType == METRIC_TYPE_PKTPAIR);

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
                Adj->MetricInfo.PktPair.TimeLastProbeRcvd = Now;
                Adj->MetricInfo.PktPair.LastProbeTimestamp = Probe->Opt.Timestamp;
                Adj->MetricInfo.PktPair.LastProbeSeq = Probe->Opt.Seq;
            }
            else if ((Adj->MetricInfo.PktPair.TimeLastProbeRcvd != 0) &&
                     ((Adj->MetricInfo.PktPair.LastProbeSeq + 1) == Probe->Opt.Seq)) {
                //
                // Calculate value of OutDelta.
                // Since the sender does not have our clock frequency info,
                // we must scale OutDelta to 100ns units before sending it back. 
                //

                ASSERT(Adj->MetricInfo.PktPair.LastProbeTimestamp != (uint)-1);

                OutDelta = Now - Adj->MetricInfo.PktPair.TimeLastProbeRcvd;
                Adj->MetricInfo.PktPair.TimeLastProbeRcvd = 0;

                if ((10000000L * OutDelta) < OutDelta) {
                    //
                    // We got an overly large OutDelta, so we
                    // can't report a reasonable number. Therefore, don't.
                    //
                    KdPrint(("MCL!PktPairReceiveProbe: Not sending reply due to OutDelta overflow.\n"));
                    break;
                }
                OutDelta = (10000000L * OutDelta)/Frequency.QuadPart;

                //
                // Remember to send probe reply, and update counters. 
                //
                SendProbeReply = TRUE;
                Adj->MetricInfo.PktPair.RepliesSent++;
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

//* PktPairSendProbeReply
//
//  Send a probe reply. 
//
void
PktPairSendProbeReply(
        MiniportAdapter *VA, 
        InternalProbe *Probe, 
        Time Now, 
        Time OutDelta)
{
    SRPacket *srp;
    NDIS_PACKET *Packet;
    NDIS_STATUS Status;
    PRPktPair *Special; 

    UNREFERENCED_PARAMETER (Now); 

    srp = ExAllocatePool(NonPagedPool, sizeof *srp);
    if (srp == NULL)
        return;
    RtlZeroMemory(srp, sizeof *srp);

    //
    // Initialize the Probe Reply option.
    // Allocate extra space to hold packet-pair data.
    //
    srp->ProbeReply = ExAllocatePool(NonPagedPool, sizeof *srp->ProbeReply + sizeof(PRPktPair));
    if (srp->ProbeReply == NULL)
        goto FreesrpAndExit;

    RtlZeroMemory(srp->ProbeReply, sizeof *srp->ProbeReply + sizeof(PRPktPair));

    srp->ProbeReply->Opt.OptionType = LQSR_OPTION_TYPE_PROBEREPLY;
    srp->ProbeReply->Opt.OptDataLen = PROBE_REPLY_LEN + sizeof(PRPktPair);
    srp->ProbeReply->Opt.MetricType = VA->MetricType; 
    srp->ProbeReply->Opt.ProbeType = METRIC_TYPE_PKTPAIR;
    srp->ProbeReply->Opt.Seq = Probe->Opt.Seq; 
    srp->ProbeReply->Opt.Timestamp = Probe->Opt.Timestamp; 
    RtlCopyMemory(srp->ProbeReply->Opt.From, VA->Address,
                  sizeof(VirtualAddress));
    RtlCopyMemory(srp->ProbeReply->Opt.To, Probe->Opt.From,
                  sizeof(VirtualAddress));
    srp->ProbeReply->Opt.OutIf = Probe->Opt.InIf;
    srp->ProbeReply->Opt.InIf = Probe->Opt.OutIf;

    //
    // Fill in packet-pair specific data. 
    //
    Special = (PRPktPair *)srp->ProbeReply->Opt.Special; 
    Special->OutDelta = OutDelta; 
    
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
    // Reuse completion routine from PktPairSendProbe.
    //
    PC(Packet)->TransmitComplete = PktPairSendProbeComplete;

    if (PC(Packet)->PA == NULL) {
        //
        // This means the packet is trying to use a physical adapter that
        // no longer exists.  Just free the packet and bail.
        //
        PktPairSendProbeComplete(VA, Packet, NDIS_STATUS_FAILURE);

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

//* PktPairReceiveProbeReply
//
//  Handle an incoming Probe Reply message.
//
void
PktPairReceiveProbeReply(
    MiniportAdapter *VA,
    InternalProbeReply *ProbeReply)
{
    LinkCache *LC = VA->LC;
    Link *Adjacent;
    KIRQL OldIrql;
    LARGE_INTEGER Timestamp;
    LARGE_INTEGER Frequency;
    PRPktPair *Special; 

    ASSERT((ProbeReply->Opt.MetricType == METRIC_TYPE_PKTPAIR) &&
           (VA->MetricType == METRIC_TYPE_PKTPAIR));

    //
    // Loop through links to adjacent nodes, looking for the link upon
    // which the probe to which this probe reply is a response was sent.
    //
    Timestamp = KeQueryPerformanceCounter(&Frequency);
    KeAcquireSpinLock(&LC->Lock, &OldIrql);
    for (Adjacent = LC->nodes[0].AdjOut; Adjacent != NULL;
         Adjacent = Adjacent->NextOut) {

        if (VirtualAddressEqual(ProbeReply->Opt.From,
                                LC->nodes[Adjacent->targetIndex].address) &&
            (ProbeReply->Opt.OutIf == Adjacent->inif) &&
            (ProbeReply->Opt.InIf == Adjacent->outif)) {

            //
            // Found link this probe was sent on.
            // If it's a response for the last probe 
            // we sent, update the metric. 
            //
            if (Adjacent->MetricInfo.PktPair.Outstanding ==
                                                ProbeReply->Opt.Seq) {
                Special = (PRPktPair *)ProbeReply->Opt.Special; 
                Adjacent->MetricInfo.PktPair.RepliesRcvd++;
                PktPairUpdateMetric(
                        VA, 
                        Adjacent, 
                        Special->OutDelta, 
                        ProbeReply->Opt.Timestamp,
                        Timestamp.QuadPart,
                        Frequency,
                        FALSE);
            }
            else {
                //
                // We got a late probe reply. Throw it away,
                // since we already took a penalty for this.
                // This should not happen too often. 
                //
                KdPrint(("MCL!PktPairReceiveProbeReply LATE outstanding %u rcvd %u\n", 
                           Adjacent->MetricInfo.PktPair.Outstanding, ProbeReply->Opt.Seq));
            }
            Adjacent->MetricInfo.PktPair.Outstanding = 0;
            break;
        }
    }
    KeReleaseSpinLock(&LC->Lock, OldIrql);
}
