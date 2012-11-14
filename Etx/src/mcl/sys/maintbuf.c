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

//* MaintBufAckExpected
//
//  Returns TRUE if we expect to receive an ack, that is,
//  we have any unacknowledged ack requests outstanding.
//
__inline boolint
MaintBufAckExpected(MaintBufNode *MBN)
{
    return (LQSRAckId)(MBN->LastAckNum + 1) != MBN->NextAckNum;
}

//* MaintBufValidAck
//
//  Returns TRUE if AckNum is a valid sequence number, that is,
//  corresponds to an unacknowledged ack request.
//
__inline boolint
MaintBufValidAck(
    MaintBufNode *MBN,
    LQSRAckId AckNum)
{
    //
    // LastAckNum is the last acknowledged sequence number,
    // and NextAckNum is the next sequence number that will be used.
    // NB: The sequence numbers can wrap.
    //
    return ((0 < (LQSRAckId)(AckNum - MBN->LastAckNum)) &&
            ((LQSRAckId)(AckNum - MBN->LastAckNum) <
             (LQSRAckId)(MBN->NextAckNum - MBN->LastAckNum)));
}

//* MaintBufAckNum
//
//  Returns the most-recently used sequence number.
//
__inline LQSRAckId
MaintBufAckNum(MaintBufNode *MBN)
{
    return (LQSRAckId)(MBN->NextAckNum - 1);
}

//* MaintBufNew
//
//  Allocates a new maintenance buffer.
//
MaintBuf *
MaintBufNew(void)
{
    MaintBuf *MB;

    MB = ExAllocatePool(NonPagedPool, sizeof *MB);
    if (MB == NULL)
        return NULL;

    KeInitializeSpinLock(&MB->Lock);
    MB->MBN = NULL;

    MB->NumPackets = 0;
    MB->HighWater = 0;

    return MB;
}

//* MaintBufFree
//
//  Frees an existing maintenance buffer.
//
void
MaintBufFree(MiniportAdapter *VA)
{
    MaintBuf *MB = VA->MaintBuf;
    MaintBufNode *MBN;
    MaintBufPacket *MBP;

    //
    // Free the node structures.
    // There can be internally-generated packets waiting to be sent.
    //
    while ((MBN = MB->MBN) != NULL) {
        MB->MBN = MBN->Next;

        while ((MBP = MBN->MBP) != NULL) {
            SRPacket *SRP = MBP->srp;

            MBN->MBP = MBP->Next;

            (*SRP->TransmitComplete)(VA, SRP, NDIS_STATUS_SUCCESS);
            ExFreePool(MBP);
        }

        ExFreePool(MBN);
    }

    ExFreePool(MB);
}

//* MaintBufFindNode
//
//  Find a MaintBufNode with specified address and interfaces.
//  If one doesn't exist, creates it.
//
//  Called with the MaintBuf locked.
//
static MaintBufNode *
MaintBufFindNode(MaintBuf *MB,
                 const VirtualAddress Address,
                 LQSRIf InIf, LQSRIf OutIf)
{
    MaintBufNode *MBN;

    //
    // Search for an existing MaintBufNode.
    //
    for (MBN = MB->MBN; MBN != NULL; MBN = MBN->Next) {
        if (VirtualAddressEqual(MBN->Address, Address) &&
            (MBN->InIf == InIf) &&
            (MBN->OutIf == OutIf))
            return MBN;
    }

    //
    // Create a new MaintBufNode for this node.
    //
    MBN = ExAllocatePool(NonPagedPool, sizeof *MBN);
    if (MBN == NULL)
        return NULL;

    RtlCopyMemory(MBN->Address, Address, SR_ADDR_LEN);
    MBN->OutIf = OutIf;
    MBN->InIf = InIf;
    MBN->NextAckNum = 0;
    MBN->LastAckNum = (LQSRAckId)-1;
    MBN->LastAckRcv = 0;
    MBN->FirstAckReq = 0;
    MBN->LastAckReq = 0;
    MBN->MBP = NULL;

    MBN->NumPackets = 0;
    MBN->HighWater = 0;

    MBN->NumAckReqs = 0;
    MBN->NumFastReqs = 0;
    MBN->NumValidAcks = 0;
    MBN->NumInvalidAcks = 0;

    //
    // Add the new node to the Maintenance Buffer.
    //
    MBN->Next = MB->MBN;
    MB->MBN = MBN;

    return MBN;
}

//* MaintBufPacketRelease
//
//  Releases a reference for a MaintBufPacket.
//
void
MaintBufPacketRelease(
    MiniportAdapter *VA,
    MaintBufPacket *MBP)
{
    if (InterlockedDecrement((PLONG)&MBP->RefCnt) == 0) {
        SRPacket *srp = MBP->srp;
        //
        // There is no outstanding use of this MaintBufPacket,
        // so deallocate it and complete the packet.
        //
        (*srp->TransmitComplete)(VA, srp, NDIS_STATUS_SUCCESS);
        ExFreePool(MBP);
    }
}

//* MaintBufTransmitComplete
//
//  Called when ProtocolTransmit finishes transmitting a packet that is
//  in the maintenance buffer. Releases a reference to the MBP.
//
static void
MaintBufTransmitComplete(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet,
    NDIS_STATUS Status)
{
    MaintBufPacket *MBP = PC(Packet)->MBP;

    UNREFERENCED_PARAMETER(Status);

    NdisFreePacketClone(Packet);

    MaintBufPacketRelease(VA, MBP);
}

//* MaintBufAddPacket
//
//  Adds a MaintBufPacket to a MaintBufNode,
//  returning an NDIS_PACKET to transmit.
//  Adds a reference to the MaintBufPacket
//  when returning non-NULL.
//
//  Our caller must update MBN->FirstAckReq, MBN->LackAckReq,
//  and MBN->NumAckReqs.
//
//  Called with the MaintBuf locked.
//
static NDIS_PACKET *
MaintBufAddPacket(
    MiniportAdapter *VA,
    MaintBufNode *MBN,
    MaintBufPacket *MBP)
{
    MaintBuf *MB = VA->MaintBuf;
    SRPacket *srp = MBP->srp;
    NDIS_PACKET *Packet;
    NDIS_STATUS Status;

    //
    // Get the Ack identifier for this packet.
    //
    MBP->AckNum = srp->ackreq->opt.identification = MBN->NextAckNum++;

    //
    // Add the MaintBufPacket to the MaintBufNode.
    //
    MBP->Next = MBN->MBP;
    MBN->MBP = MBP;

    if (++MBN->NumPackets > MBN->HighWater)
        MBN->HighWater = MBN->NumPackets;
    if (++MB->NumPackets > MB->HighWater)
        MB->HighWater = MB->NumPackets;

    //
    // Check for options that can be piggy-backed on this packet.
    //
    PbackSendPacket(VA, srp);

    Status = SRPacketToPkt(VA, srp, &Packet);

    //
    // We only want to send Acks once.
    //
    if (srp->ack != NULL) {
        SRFreeOptionList((InternalOption *) srp->ack);
        srp->ack = NULL;
    }

    if (Status != NDIS_STATUS_SUCCESS) {
        //
        // We do not add a reference to MBP.
        //
        return NULL;
    }
    else {
        //
        // Add a reference to MBP for MaintBufTransmitComplete.
        //
        InterlockedIncrement((PLONG)&MBP->RefCnt);
        return Packet;
    }
}

//* MaintBufTransmit
//
//  If Packet is non-NULL, our caller provides a reference
//  for the MaintBufPacket that we pass through
//  to MaintBufTransmitComplete.
//
//  Called with the MaintBuf locked.
//
static void
MaintBufTransmit(
    MiniportAdapter *VA,
    MaintBufPacket *MBP,
    NDIS_PACKET *Packet)
{
    if (Packet == NULL) {
        //
        // MaintBufAddPacket did not give us a ref for MBP.
        // MaintBufTimer will retransmit.
        //
        return;
    }

    //
    // MaintBufTransmitComplete needs these fields.
    // We have a reference for MBP from MaintBufAddPacket
    // that we pass on to MaintBufTransmitComplete.
    //
    PC(Packet)->TransmitComplete = MaintBufTransmitComplete;
    PC(Packet)->MBP = MBP;

    if (PC(Packet)->PA == NULL) {
        //
        // This means the source route is trying to use a physical adapter
        // that no longer exists. LinkCacheDeleteInterface has been called.
        // REVIEW - We know this packet will never transmit successfully,
        // so we could immediately try salvaging instead of letting
        // MaintBufTimer get there eventually. But this is a rare situation.
        //
        MiniportSendRouteError(VA, MBP->srp);
        MaintBufTransmitComplete(VA, Packet, NDIS_STATUS_FAILURE);
        return;
    }

    ProtocolTransmit(PC(Packet)->PA, Packet);
}

//* MaintBufSalvage
//
//  If possible, salvages the packet by finding a new route
//  to the destination. Our caller gives us a reference for the MBP
//  that we must dispose of.
//
Time
MaintBufSalvage(
    MiniportAdapter *VA,
    MaintBufPacket *MBP,
    Time Now)
{
    MaintBuf *MB = VA->MaintBuf;
    SRPacket *srp = MBP->srp;
    VirtualAddress NextHopAddr;
    LQSRIf NextHopOutIf, NextHopInIf;
    VirtualAddress Dest;
    uint Index;
    MaintBufNode *MBN;
    NDIS_PACKET *Packet = NULL;
    NDIS_STATUS Status;
    Time Timeout = MAXTIME;
    uint SalvageCount;
    KIRQL OldIrql;

    //
    // We do not salvage statically routed packets. 
    //
    if (srp->sr->opt.staticRoute) {
        InterlockedIncrement((PLONG)&VA->CountSalvageStatic);
        MaintBufPacketRelease(VA, MBP);
        return Timeout;
    }
    
    InterlockedIncrement((PLONG)&VA->CountSalvageAttempt);

    SalvageCount = ++srp->sr->opt.salvageCount;
    if (SalvageCount == 0) {
        //
        // We can not salvage because the count overflowed.
        //
        InterlockedIncrement((PLONG)&VA->CountSalvageOverflow);
        MaintBufPacketRelease(VA, MBP);
        return Timeout;
    }

    //
    // Get the current next-hop for this packet.
    // We failed to send to this next-hop.
    //
    Index = SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen) -
        srp->sr->opt.segmentsLeft - 1;
    ASSERT(VirtualAddressEqual(srp->sr->opt.hopList[Index].addr,
                               VA->Address));
    RtlCopyMemory(NextHopAddr, srp->sr->opt.hopList[Index + 1].addr,
                  sizeof(VirtualAddress));
    NextHopInIf = srp->sr->opt.hopList[Index + 1].inif;
    NextHopOutIf = srp->sr->opt.hopList[Index].outif;

    //
    // Get the final destination of this packet.
    //
    Index = SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen) - 1;
    RtlCopyMemory(Dest, srp->sr->opt.hopList[Index].addr,
                  sizeof(VirtualAddress));

    //
    // The DSR spec says that when salvaging, you leave the originator
    // in hopList[0] and insert the new route starting with hopList[1].
    // But this seems like an unnecessary complication. Besides complicating
    // this code, it would require a change in CacheSRPacket.
    //

    Status = LinkCacheFillSR(VA, Dest, srp->sr);
    if (Status != NDIS_STATUS_SUCCESS) {
        //
        // We can not salvage because we do not have a route.
        //
        InterlockedIncrement((PLONG)&VA->CountSalvageNoRoute);
        MaintBufPacketRelease(VA, MBP);
        return Timeout;
    }

    Index = SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen) -
        srp->sr->opt.segmentsLeft - 1;
    ASSERT(Index == 0); // This is a new source route.

    //
    // Check that we are not trying the same bad next-hop again.
    //
    if (VirtualAddressEqual(NextHopAddr,
                            srp->sr->opt.hopList[Index + 1].addr) &&
        (NextHopInIf == srp->sr->opt.hopList[Index + 1].inif) &&
        (NextHopOutIf == srp->sr->opt.hopList[Index].outif)) {
        //
        // We can not salvage because the route tries the same next-hop.
        //
        InterlockedIncrement((PLONG)&VA->CountSalvageSameNextHop);
        MaintBufPacketRelease(VA, MBP);
        return Timeout;
    }

    if (! LinkCacheUseSR(VA, srp)) {
        //
        // We can not salvage because the transmit queue is full.
        //
        InterlockedIncrement((PLONG)&VA->CountSalvageQueueFull);
        MaintBufPacketRelease(VA, MBP);
        return Timeout;
    }

    // LinkCacheFillSR zeroed salvageCount, so initialize it.
    srp->sr->opt.salvageCount = (ushort)SalvageCount;

    //
    // Find the MaintBufNode for the packet.
    //
    KeAcquireSpinLock(&MB->Lock, &OldIrql);
    MBN = MaintBufFindNode(MB, srp->sr->opt.hopList[Index + 1].addr,
                           srp->sr->opt.hopList[Index + 1].inif,
                           srp->sr->opt.hopList[Index].outif);
    if (MBN != NULL) {
        //
        // Are we requesting the first ack?
        //
        if (! MaintBufAckExpected(MBN))
            MBN->FirstAckReq = Now;

        MBN->LastAckReq = Now;
        Timeout = MBN->LastAckReq + MAINTBUF_REXMIT_TIMEOUT;

        if ((MBN->LastAckRcv + MAINTBUF_HOLDOFF_TIME > Now) ||
            (MBN->NumPackets >= MAINTBUF_MAX_QUEUE)) {

            //
            // We have recent confirmation that this link is working.
            // Or we are already holding many packets for this destination.
            // In any case, just send this packet without holding it.
            // We request an ack but we do not add the MBP to the MBN.
            // So we dispose of our reference for the MBP.
            //
            MBN->NumFastReqs++;
            srp->ackreq->opt.identification = MBN->NextAckNum++;

            //
            // Check for options that can be piggy-backed on this packet.
            //
            PbackSendPacket(VA, srp);

            Status = SRPacketToPkt(VA, srp, &Packet);

            if (Status != NDIS_STATUS_SUCCESS) {
                //
                // Release our reference to MBP below.
                //
                MBN = NULL;
            }
            else {
                //
                // Donate our reference to MBP to MaintBufTransmitComplete.
                //
            }
        }
        else {
            //
            // We are sending an Ack Request to this node.
            //
            MBN->NumAckReqs++;

            //
            // Our reference for MBP goes to keep the MBP on the MBN.
            // MaintBufAddPacket returns an additional reference
            // if Packet is non-NULL.
            //
            Packet = MaintBufAddPacket(VA, MBN, MBP);
        }
    }
    KeReleaseSpinLock(&MB->Lock, OldIrql);

    if (MBN == NULL) {
        //
        // We could not salvage.
        //
        MaintBufPacketRelease(VA, MBP);
        return Timeout;
    }

    InterlockedIncrement((PLONG)&VA->CountSalvageTransmit);
    MaintBufTransmit(VA, MBP, Packet);
    return Timeout;
}

//* MaintBufCreateAckRequest
//
//  Creates an ack request packet.
//
//  Called with the MaintBuf locked.
//
static SRPacket *
MaintBufCreateAckRequest(
    MiniportAdapter *VA,
    MaintBufNode *MBN)
{
    SRPacket *SRP;
    InternalSourceRoute *SR;
    InternalAcknowledgementRequest *AR;

    //
    // Allocate a packet with which to send the ack request.
    //

    SRP = ExAllocatePool(NonPagedPool, sizeof *SRP);
    if (SRP == NULL)
        return NULL;

    SR = ExAllocatePool(NonPagedPool,
                        sizeof *SR + sizeof(SRAddr)*MAX_SR_LEN);
    if (SR == NULL) {
        ExFreePool(SRP);
        return NULL;
    }

    AR = ExAllocatePool(NonPagedPool, sizeof *AR);
    if (AR == NULL) {
        ExFreePool(SR);
        ExFreePool(SRP);
        return NULL;
    }

    //
    // Initialize the packet.
    //
    RtlZeroMemory(SRP, sizeof *SRP);
    RtlCopyMemory(SRP->Dest, MBN->Address, sizeof(VirtualAddress));
    RtlCopyMemory(SRP->Source, VA->Address, sizeof(VirtualAddress));
    SRP->sr = SR;
    SRP->ackreq = AR;

    //
    // Initialize the source route.
    //
    SR->next = NULL;
    SR->opt.optionType = LQSR_OPTION_TYPE_SOURCERT;
    SR->opt.optDataLen = SOURCE_ROUTE_LEN(2);
    SR->opt.reservedField = 0;
    SR->opt.staticRoute = 0;
    SR->opt.salvageCount = 0;
    SR->opt.segmentsLeft = (uchar) 1;
    SR->opt.hopList[0].inif = 0;
    SR->opt.hopList[0].outif = MBN->OutIf;
    RtlCopyMemory(SR->opt.hopList[0].addr, SRP->Source,
                  sizeof(VirtualAddress));
    SR->opt.hopList[0].Metric = 0;
    SR->opt.hopList[1].inif = MBN->InIf;
    SR->opt.hopList[1].outif = 0;
    RtlCopyMemory(SR->opt.hopList[1].addr, SRP->Dest,
                  sizeof(VirtualAddress));
    SR->opt.hopList[1].Metric = 0;

    //
    // Initialize the Acknowledgement Request.
    //
    AR->next = NULL;
    AR->opt.optionType = LQSR_OPTION_TYPE_ACKREQ;
    AR->opt.optDataLen = ACK_REQUEST_LEN;
    AR->opt.identification = MaintBufAckNum(MBN);

    return SRP;
}

//* MaintBufAckRequestSendComplete
//
//  Called to complete the transmission of a packet
//  from MaintBufCreateAckRequest.
//
static void
MaintBufAckRequestSendComplete(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet,
    NDIS_STATUS Status)
{
    SRPacket *SRP = PC(Packet)->srp;

    UNREFERENCED_PARAMETER(VA);
    UNREFERENCED_PARAMETER(Status);

    NdisFreePacketClone(Packet);
    SRPacketFree(SRP);
}

//* MaintBufTimer
//
//  Called periodically to retransmit packets in the Maintenance Buffer.
//
Time
MaintBufTimer(MiniportAdapter *VA, Time Now)
{
    MaintBuf *MB = VA->MaintBuf;
    MaintBufNode **PrevMBN, *MBN;
    MaintBufPacket *MBP;
    MaintBufPacket *Salvage = NULL;
    NDIS_PACKET *Packet;
    NDIS_PACKET *RexmitPackets = NULL;
    SRPacket *SRP;
    Time Timeout = MAXTIME;
    Time Deadline;
    KIRQL OldIrql;
    NDIS_STATUS Status;

    KeAcquireSpinLock(&MB->Lock, &OldIrql);

    //
    // Inspect each MaintBufNode in the Maintenance Buffer.
    // We are only interested in MBNs that have unacknowledged ack requests.
    // While doing this, we update Timeout to track
    // the next time by which we want to run.
    //
    PrevMBN = &MB->MBN;
    while ((MBN = *PrevMBN) != NULL) {
        if (MaintBufAckExpected(MBN)) {

            //
            // If it's been too long since we received an ack,
            // we conclude that the link has failed.
            //
            Deadline = max(MBN->FirstAckReq, MBN->LastAckRcv) +
                                                        MAINTBUF_LINK_TIMEOUT;
            if (Deadline <= Now) {

                //
                // Penalize the link for the failure.
                // We must do this before sending the Route Error,
                // which should carry the updated metric,
                // or attempting to salvage the packets.
                //
                LinkCachePenalizeLink(VA, MBN->Address, MBN->InIf, MBN->OutIf);

                //
                // Move any waiting packets to the salvage list.
                //
                while ((MBP = MBN->MBP) != NULL) {
                    MBN->MBP = MBP->Next;
                    MBP->Next = Salvage;
                    Salvage = MBP;
                }
                MB->NumPackets -= MBN->NumPackets;
                MBN->NumPackets = 0;

                //
                // We no longer expect an ack.
                //
                MBN->LastAckNum = MaintBufAckNum(MBN);
                // Next loop iteration will inspect the same MBN again.
                continue;
            }

            if (Deadline < Timeout)
                Timeout = Deadline;

            //
            // If it's been too long since we requested an ack,
            // we should retransmit the request.
            //
            Deadline = MBN->LastAckReq + MAINTBUF_REXMIT_TIMEOUT;
            if (Deadline <= Now) {

                //
                // Retransmit an ack request.
                //
                MBN->NumAckReqs++;
                MBN->LastAckReq = Now;
                Deadline = MBN->LastAckReq + MAINTBUF_REXMIT_TIMEOUT;

                //
                // Retransmit only the most recent packet.
                //
                MBP = MBN->MBP;
                if (MBP == NULL) {
                    //
                    // We do not have a packet to retransmit,
                    // so create a packet to carry the ack req.
                    //
                    SRP = MaintBufCreateAckRequest(VA, MBN);
                }
                else {
                    //
                    // Retransmit the existing packet,
                    // but update it with current sequence number.
                    //
                    SRP = MBP->srp;
                    SRP->ackreq->opt.identification = MaintBufAckNum(MBN);
                }

                if (SRP != NULL) {
                    //
                    // This will update the metric in the source route
                    // and bump the Usage counter for the link.
                    //
                    if (! LinkCacheUseSR(VA, SRP)) {
                        InterlockedIncrement((PLONG)&VA->CountRexmitQueueFull);
                        goto DoNotTransmit;
                    }

                    //
                    // Check for options that can be piggy-backed.
                    //
                    PbackSendPacket(VA, SRP);

                    Status = SRPacketToPkt(VA, SRP, &Packet);

                    //
                    // We only want to send Acks once.
                    //
                    if (SRP->ack != NULL) {
                        SRFreeOptionList((InternalOption *) SRP->ack);
                        SRP->ack = NULL;
                    }

                    if (Status == NDIS_STATUS_SUCCESS) {
                        if (MBP == NULL) {
                            PC(Packet)->TransmitComplete =
                                MaintBufAckRequestSendComplete;
                            PC(Packet)->srp = SRP;
                        }
                        else {
                            //
                            // Need a reference to MBP, released in
                            // MaintBufTransmitComplete.
                            //
                            InterlockedIncrement((PLONG)&MBP->RefCnt);
                            PC(Packet)->TransmitComplete =
                                MaintBufTransmitComplete;
                            PC(Packet)->MBP = MBP;
                        }

                        //
                        // Queue the packet using the OrigPacket link.
                        //
                        PC(Packet)->OrigPacket = RexmitPackets;
                        RexmitPackets = Packet;
                    }
                    else {
                    DoNotTransmit:
                        if (MBP == NULL) {
                            //
                            // We just need to free the packet.
                            //
                            SRPacketFree(SRP);
                        }
                        else {
                            //
                            // We leave the MBP on the MBN.
                            //
                        }
                    }
                }
            }
        }
        else {
            //
            // There are no outstanding ack requests.
            // Hence we must have no waiting packets.
            //
            ASSERT(MBN->MBP == NULL);

            Deadline = MBN->LastAckReq + MAINTBUF_IDLE_TIMEOUT;
            if (Deadline <= Now) {
                //
                // This Maintenance Buffer Node has been idle for a long time.
                // If we delete it, we lose the AckNum state for this neighbor.
                // But this is OK after sufficient time.
                //
                *PrevMBN = MBN->Next;
                ExFreePool(MBN);
                // Next loop iteration looks at the next MBN.
                continue;
            }
        }

        if (Deadline < Timeout)
            Timeout = Deadline;

        // Move to the next MBN.
        PrevMBN = &MBN->Next;
    }
    KeReleaseSpinLock(&MB->Lock, OldIrql);

    //
    // Send any queued retransmissions.
    //
    while ((Packet = RexmitPackets) != NULL) {
        RexmitPackets = PC(Packet)->OrigPacket;

        if (PC(Packet)->PA == NULL) {
            //
            // This means the source route is trying to use a physical adapter
            // that no longer exists. LinkCacheDeleteInterface has been called.
            //
            if (PC(Packet)->TransmitComplete == MaintBufTransmitComplete)
                MiniportSendRouteError(VA, PC(Packet)->MBP->srp);
            (*PC(Packet)->TransmitComplete)(VA, Packet, NDIS_STATUS_FAILURE);
        }
        else {
            //
            // Transmit the packet.
            //
            ProtocolTransmit(PC(Packet)->PA, Packet);
        }
    }

    //
    // If there were any waiting packets,
    // send errors and salvage them.
    // NB: These packets may have outstanding references.
    //
    while ((MBP = Salvage) != NULL) {
        Salvage = MBP->Next;

        MiniportSendRouteError(VA, MBP->srp);

        //
        // Try to salvage the packet.
        // This returns a new timeout.
        //
        Deadline = MaintBufSalvage(VA, MBP, Now);
        if (Deadline < Timeout)
            Timeout = Deadline;
    }

    //
    // Return the time of our next call.
    //
    ASSERT(Now < Timeout);
    return Timeout;
}

//* MaintBufRecvAck
//
//  Handles a received acknowledgement.
//
void
MaintBufRecvAck(
    MiniportAdapter *VA,
    const VirtualAddress Address,
    LQSRIf InIf,
    LQSRIf OutIf,
    LQSRAckId AckNum)
{
    MaintBuf *MB = VA->MaintBuf;
    MaintBufNode *MBN;
    MaintBufPacket **PrevMBP;
    MaintBufPacket *MBP = NULL;
    MaintBufPacket *NextMBP;
    uint NumPackets;
    KIRQL OldIrql;

    //
    // Find the appropriate MaintBufNode.
    //
    KeAcquireSpinLock(&MB->Lock, &OldIrql);
    MBN = MaintBufFindNode(MB, Address, InIf, OutIf);
    if (MBN != NULL) {
        //
        // Is this a valid ack?
        // That is, between LastAckNum and NextAckNum.
        //
        if (MaintBufValidAck(MBN, AckNum)) {

            //
            // We have received a valid ack, confirming the link.
            //
            MBN->NumValidAcks++;
            MBN->LastAckNum = AckNum;
            MBN->LastAckRcv = KeQueryInterruptTime();

            //
            // Remove any acknowledged packets.
            // This maintains the invariant that waiting packets
            // have a sequence number between LastAckNum and NextAckNum.
            //
            NumPackets = 0;
            for (PrevMBP = &MBN->MBP;
                 (MBP = *PrevMBP) != NULL;
                 PrevMBP = &MBP->Next) {

                //
                // If we receive MBP->AckNum, will it be valid?
                //
                if (! MaintBufValidAck(MBN, MBP->AckNum)) {
                    //
                    // Remove this packet (and any older ones).
                    //
                    *PrevMBP = NULL;
                    MB->NumPackets -= MBN->NumPackets - NumPackets;
                    MBN->NumPackets = NumPackets;
                    break;
                }
                NumPackets++;
            }
        }
        else {
            //
            // This is an invalid ack, because the ack sequence number
            // is not in the proper range.
            //
            MBN->NumInvalidAcks++;
        }
    }
    KeReleaseSpinLock(&MB->Lock, OldIrql);

    //
    // Did we find any packets to complete?
    //
    while (MBP != NULL) {
        NextMBP = MBP->Next;

        MaintBufPacketRelease(VA, MBP);

        MBP = NextMBP;
    }
}

//* MaintBufStaticSendComplete
//
//  Completion handler for statically source-routed packets.
//
static void
MaintBufStaticSendComplete(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet,
    NDIS_STATUS Status)
{
    SRPacket *SRP = PC(Packet)->srp;

    NdisFreePacketClone(Packet);

   (*SRP->TransmitComplete)(VA, SRP, Status);
}

//* MaintBufSendPacket
//
//  Sends a Source-Routed packet using Route Maintenance.
//  That is, requests an Ack and retransmits as necessary.
//
//  LinkCacheUseSR has already been called, but if we retransmit
//  then we should call it again.
//
//  NB: We must create a new NDIS_PACKET for every (re)transmission.
//  We can not allow a single NDIS_PACKET to be sent twice simultaneously.
//
void
MaintBufSendPacket(
    MiniportAdapter *VA,
    SRPacket *srp,
    void (*Complete)(MiniportAdapter *VA, SRPacket *srp, NDIS_STATUS Status))
{
    MaintBuf *MB = VA->MaintBuf;
    InternalAcknowledgementRequest *AR;
    MaintBufNode *MBN;
    MaintBufPacket *MBP = NULL;
    NDIS_PACKET *Packet = NULL;
    Time Now;
    Time Timeout = 0;
    uint Index;
    KIRQL OldIrql;
    NDIS_STATUS Status;

    ASSERT(srp->sr != NULL);
    ASSERT(srp->ackreq == NULL);

    //
    // We have to initialize this before unlocking MaintBuf.
    //
    srp->TransmitComplete = Complete;

    //
    // If the packet is using a static route,
    // then we do not use Route Maintenance.
    //
    if (! srp->sr->opt.staticRoute) {
        //
        // Use the Maintenance Buffer to send the packet,
        // requesting an acknowledgement.
        //
        InterlockedIncrement((PLONG)&VA->CountXmitMaintBuf);

        //
        // SegmentsLeft has already been decremented.
        //
        Index = SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen) -
            srp->sr->opt.segmentsLeft - 1;
        ASSERT(VirtualAddressEqual(srp->sr->opt.hopList[Index].addr, 
                                   VA->Address));

        AR = ExAllocatePool(NonPagedPool, sizeof *AR);
        if (AR == NULL) {
            (*Complete)(VA, srp, NDIS_STATUS_RESOURCES);
            return;
        }

        srp->ackreq = AR;
        RtlZeroMemory(AR, sizeof *AR);
        AR->opt.optionType = LQSR_OPTION_TYPE_ACKREQ;
        AR->opt.optDataLen = ACK_REQUEST_LEN;
        // AR->opt.identification initialized in MaintBufAddPacket.

        //
        // Find the MaintBufNode for the packet.
        //
        KeAcquireSpinLock(&MB->Lock, &OldIrql);
        Now = KeQueryInterruptTime();

        MBN = MaintBufFindNode(MB, srp->sr->opt.hopList[Index + 1].addr,
                               srp->sr->opt.hopList[Index + 1].inif,
                               srp->sr->opt.hopList[Index].outif);
        if (MBN != NULL) {

            //
            // Check if we will hold onto this packet.
            //
            if ((MBN->LastAckRcv + MAINTBUF_HOLDOFF_TIME > Now) ||
                (MBN->NumPackets >= MAINTBUF_MAX_QUEUE)) {
                //
                // We have recent confirmation that this link is working.
                // Or we are already holding many packets for this destination.
                // In any case, just send this packet without holding it.
                // We still request an ack.
                //
                if (! MaintBufAckExpected(MBN))
                    MBN->FirstAckReq = Now;

                srp->ackreq->opt.identification = MBN->NextAckNum++;

                MBN->NumFastReqs++;
                MBN->LastAckReq = Now;
                Timeout = MBN->LastAckReq + MAINTBUF_REXMIT_TIMEOUT;
                KeReleaseSpinLock(&MB->Lock, OldIrql);

                MiniportRescheduleTimeout(VA, Now, Timeout);
                goto SendDirectly;
            }

            //
            // Create and initialize a new MaintBufPacket structure.
            //
            MBP = ExAllocatePool(NonPagedPool, sizeof *MBP);
            if (MBP != NULL) {
                //
                // Initialize the MaintBufPacket. It starts with 1 ref
                // for its existence on MBN.
                //
                MBP->RefCnt = 1;
                MBP->srp = srp;

                //
                // Are we requesting the first ack?
                //
                if (! MaintBufAckExpected(MBN))
                    MBN->FirstAckReq = Now;

                //
                // If MaintBufAddPacket returns an NDIS_PACKET,
                // it also adds a reference to MBP.
                //
                Packet = MaintBufAddPacket(VA, MBN, MBP);

                //
                // We are sending an Ack Request to this node.
                //
                MBN->NumAckReqs++;
                MBN->LastAckReq = Now;
                Timeout = MBN->LastAckReq + MAINTBUF_REXMIT_TIMEOUT;
            }
        }
        KeReleaseSpinLock(&MB->Lock, OldIrql);

        if (Timeout != 0) {
            //
            // Reschedule the next MaintBufTimer call.
            //
            MiniportRescheduleTimeout(VA, Now, Timeout);
        }

        if (MBP == NULL) {
            (*Complete)(VA, srp, NDIS_STATUS_RESOURCES);
            return;
        }

        MaintBufTransmit(VA, MBP, Packet);
    }
    else {
        //
        // Just send the packet directly.
        //
    SendDirectly:
        PbackSendPacket(VA, srp);

        Status = SRPacketToPkt(VA, srp, &Packet);
        if (Status != NDIS_STATUS_SUCCESS) {
            (*Complete)(VA, srp, Status);
            return;
        }

        PC(Packet)->srp = srp;
        PC(Packet)->TransmitComplete = MaintBufStaticSendComplete;

        if (PC(Packet)->PA == NULL) {
            //
            // This means the packet is trying to use a physical adapter that
            // no longer exists.
            //
            MaintBufStaticSendComplete(VA, Packet, NDIS_STATUS_FAILURE);
        }
        else {
            ProtocolTransmit(PC(Packet)->PA, Packet);
        }
    }
}

//* MaintBufSendAck
//
//  Requests delayed transmission of an Ack.
//
void
MaintBufSendAck(
    MiniportAdapter *VA,
    SRPacket *SRP)
{
    InternalAcknowledgement *Ack;
    uint Index;

    ASSERT((SRP->sr != NULL) && (SRP->ackreq != NULL));
    Index = SOURCE_ROUTE_HOPS(SRP->sr->opt.optDataLen) - SRP->sr->opt.segmentsLeft;

    //
    // Allocate the Acknowledgement option.
    //
    Ack = ExAllocatePool(NonPagedPool, sizeof *Ack);
    if (Ack == NULL)
        return;

    //
    // Initialize the Acknowledgement option.
    //
    RtlZeroMemory(Ack, sizeof *Ack);
    Ack->opt.optionType = LQSR_OPTION_TYPE_ACK;
    Ack->opt.optDataLen = ACKNOWLEDGEMENT_LEN;
    Ack->opt.identification = SRP->ackreq->opt.identification;
    Ack->opt.inif = SRP->sr->opt.hopList[Index].inif;
    Ack->opt.outif = SRP->sr->opt.hopList[Index - 1].outif;
    RtlCopyMemory(Ack->opt.from, VA->Address, SR_ADDR_LEN);
    RtlCopyMemory(Ack->opt.to, SRP->sr->opt.hopList[Index - 1].addr, SR_ADDR_LEN);

    //
    // Send the Acknowledgement option.
    //
    PbackSendOption(VA, Ack->opt.to,
                    (InternalOption *)Ack,
                    MAX_ACK_DELAY);
}

//* MaintBufResetStatistics
//
//  Resets all counters and statistics gathering for the maintenance buffer.
//
void
MaintBufResetStatistics(MiniportAdapter *VA)
{
    MaintBuf *MB = VA->MaintBuf;
    MaintBufNode *MBN;
    KIRQL OldIrql;

    KeAcquireSpinLock(&MB->Lock, &OldIrql);
    MB->HighWater = MB->NumPackets;
    for (MBN = MB->MBN; MBN != NULL; MBN = MBN->Next) {
        MBN->HighWater = MBN->NumPackets;
        MBN->NumAckReqs = 0;
        MBN->NumFastReqs = 0;
        MBN->NumValidAcks = 0;
        MBN->NumInvalidAcks = 0;
    }
    KeReleaseSpinLock(&MB->Lock, OldIrql);
}
