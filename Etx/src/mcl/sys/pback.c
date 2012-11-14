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
// Piggy-backing module. General mechanism for sending LQSR options
// at a later time, hopefully by piggy-backing on another packet
// that is going in the right direction.
//

#include "headers.h"

//* PbackInit
//
//  Initializes the piggy-back data structures.
//
void
PbackInit(MiniportAdapter *VA)
{
    PbackCache *PCache = &VA->PCache;

    KeInitializeSpinLock(&PCache->Lock);
    ASSERT(PCache->List == NULL);
}

//* PbackCleanup
//
//  Cleans up the piggy-back data structures.
//
void
PbackCleanup(MiniportAdapter *VA)
{
    PbackCache *PCache = &VA->PCache;
    PbackOption *PO;

    while ((PO = PCache->List) != NULL) {
        PCache->List = PO->Next;
        ExFreePool(PO->Opt);
        ExFreePool(PO);
    }
}

//* PbackResetStatistics
//
//  Resets all counters and statistics gathering for the piggy-backing module.
//
void
PbackResetStatistics(MiniportAdapter *VA)
{
    PbackCache *PCache = &VA->PCache;
    KIRQL OldIrql;

    KeAcquireSpinLock(&PCache->Lock, &OldIrql);
    PCache->HighWater = PCache->Number;
    PCache->AckMaxDupTime = 0;
    PCache->CountPbackTooBig = 0;
    PCache->CountPbackTotal = 0;
    PCache->CountAloneTotal = 0;
    PCache->CountPbackAck = 0;
    PCache->CountAloneAck = 0;
    PCache->CountPbackReply = 0;
    PCache->CountAloneReply = 0;
    PCache->CountPbackError = 0;
    PCache->CountAloneError = 0;
    PCache->CountPbackInfo = 0;
    PCache->CountAloneInfo = 0;
    KeReleaseSpinLock(&PCache->Lock, OldIrql);
}

//* PbackSendOption
//
//  Send the option to the destination by the specified time.
//  Takes ownership of the option memory.
//
void
PbackSendOption(
    MiniportAdapter *VA,
    const VirtualAddress Dest,  // May point into the option.
    InternalOption *Opt,
    Time Timeout)               // Relative.
{
    PbackCache *PCache = &VA->PCache;
    PbackOption *PO, *NewPO, **PrevPO;
    Time Now;
    KIRQL OldIrql;

    //
    // If you add another option type that can be piggy-backed,
    // you must also update PbackAddOptionToPacket and
    // the send-reliably decision in PbackTimeout.
    //
    ASSERT((Opt->Opt.optionType == LQSR_OPTION_TYPE_REPLY) ||
           (Opt->Opt.optionType == LQSR_OPTION_TYPE_ERROR) ||
           (Opt->Opt.optionType == LQSR_OPTION_TYPE_ACK) ||
           (Opt->Opt.optionType == LQSR_OPTION_TYPE_INFO));

    KeAcquireSpinLock(&PCache->Lock, &OldIrql);
    Now = KeQueryInterruptTime();

    if (Opt->Opt.optionType == LQSR_OPTION_TYPE_ACK) {
        //
        // We implement some special behavior for Acks.
        // If we already have an Ack outstanding,
        // we replace it with this new one and leave
        // the timeout unchanged.
        //
        for (PO = PCache->List; PO != NULL; PO = PO->Next) {
            InternalOption *OldOpt = PO->Opt;

            if ((OldOpt->Opt.optionType == LQSR_OPTION_TYPE_ACK) &&
                VirtualAddressEqual(PO->Dest, Dest)) {

                //
                // If the new Ack has the same identification,
                // then it means we delayed too long because
                // the other guy has already retransmitted.
                //
                if (((InternalAcknowledgement *)OldOpt)->opt.identification ==
                    ((InternalAcknowledgement *)Opt)->opt.identification) {

                    InterlockedIncrement((PLONG)&VA->CountRecvDupAckReq);
                    if ((PO->Timeout > Now) &&
                        (PO->Timeout - Now > PCache->AckMaxDupTime))
                        PCache->AckMaxDupTime = PO->Timeout - Now;
                }

                //
                // Replace the old Ack with the new Ack.
                // We do not update the timeout.
                //
                PO->Opt = Opt;
                ExFreePool(OldOpt);

                Timeout = MAXTIME; // No need to reschedule.
                goto Return;
            }
        }
    }

    if (Opt->Opt.optionType == LQSR_OPTION_TYPE_REPLY) {
        //
        // We implement similar special behavior for Route Replies.
        // If we already have an equivalent Route Reply outstanding,
        // we replace it with this one (which has newer metrics)
        // and leave the timeout unchanged.
        //
        for (PO = PCache->List; PO != NULL; PO = PO->Next) {
            InternalRouteReply *OldReply = (InternalRouteReply *) PO->Opt;
            InternalRouteReply *NewReply = (InternalRouteReply *) Opt;

            if ((OldReply->opt.optionType == LQSR_OPTION_TYPE_REPLY) &&
                (OldReply->opt.optDataLen == NewReply->opt.optDataLen) &&
                VirtualAddressEqual(PO->Dest, Dest)) {
                uint Hop;

                //
                // Check if the new reply has the same route.
                //
                for (Hop = 0; ; Hop++) {
                    SRAddr *OldAddr, *NewAddr;

                    if (Hop == ROUTE_REPLY_HOPS(OldReply->opt.optDataLen)) {
                        //
                        // These route replies are equivalent.
                        // Replace the old Route Reply with the new one.
                        // We do not update the timeout.
                        //
                        PO->Opt = Opt;
                        ExFreePool(OldReply);

                        Timeout = MAXTIME; // No need to reschedule.
                        goto Return;
                    }

                    OldAddr = &OldReply->opt.hopList[Hop];
                    NewAddr = &NewReply->opt.hopList[Hop];

                    if (! VirtualAddressEqual(OldAddr->addr, NewAddr->addr) ||
                        (OldAddr->inif != NewAddr->inif) ||
                        (OldAddr->outif != NewAddr->outif)) {
                        //
                        // These route replies are not equivalent.
                        //
                        break;
                    }
                }
            }
        }
    }

    if (Opt->Opt.optionType == LQSR_OPTION_TYPE_ERROR) {
        //
        // We implement similar special behavior for Route Errors.
        // If we already have an equivalent Route Error outstanding,
        // we replace it with this one (which has newer metric)
        // and leave the timeout unchanged.
        //
        for (PO = PCache->List; PO != NULL; PO = PO->Next) {
            InternalRouteError *OldError = (InternalRouteError *) PO->Opt;
            InternalRouteError *NewError = (InternalRouteError *) Opt;

            if ((OldError->opt.optionType == LQSR_OPTION_TYPE_ERROR) &&
                VirtualAddressEqual(OldError->opt.errorSrc,
                                    NewError->opt.errorSrc) &&
                VirtualAddressEqual(OldError->opt.errorDst,
                                    NewError->opt.errorDst) &&
                VirtualAddressEqual(OldError->opt.unreachNode,
                                    NewError->opt.unreachNode) &&
                (OldError->opt.inIf == NewError->opt.inIf) &&
                (OldError->opt.outIf == NewError->opt.outIf)) {

                PO->Opt = Opt;
                ExFreePool(OldError);
                Timeout = MAXTIME; // No need to reschedule.
                goto Return;
            }
        }
    }

    NewPO = ExAllocatePool(NonPagedPool, sizeof *NewPO);
    if (NewPO == NULL) {
        //
        // We can't send the option but we do need to free it.
        //
        ExFreePool(Opt);
        Timeout = MAXTIME; // No need to reschedule.
        goto Return;
    }

    //
    // Initialize the piggyback option.
    //
    RtlCopyMemory(NewPO->Dest, Dest, SR_ADDR_LEN);
    NewPO->Opt = Opt;

    //
    // Convert the relative timeout to absolute.
    //
    Timeout = Now + Timeout;
    NewPO->Timeout = Timeout;

    //
    // Add the option to our list.
    // The list is sorted by timeout, from smallest to largest.
    //
    PrevPO = &PCache->List;
    while ((PO = *PrevPO) != NULL) {
        if (Timeout < PO->Timeout)
            break;
        PrevPO = &PO->Next;
        Timeout = MAXTIME; // No need to reschedule.
    }
    NewPO->Next = PO;
    *PrevPO = NewPO;

    if (++PCache->Number > PCache->HighWater)
        PCache->HighWater = PCache->Number;

Return:
    KeReleaseSpinLock(&PCache->Lock, OldIrql);

    //
    // If necessary, reschedule the next timeout.
    //
    if (Timeout != MAXTIME)
        MiniportRescheduleTimeout(VA, Now, Timeout);
}

//* PbackAddOptionToPacket
//
//  Helper for PbackSendPacket and PbackTimeout.
//  Adds an LQSR option to an SRPacket.
//
static void
PbackAddOptionToPacket(
    PbackCache *PCache,
    SRPacket *SRP,
    InternalOption *Opt)
{
    InternalOption **Field;

    InterlockedIncrement((PLONG)&PCache->CountPbackTotal);
    switch (Opt->Opt.optionType) {
    case LQSR_OPTION_TYPE_REPLY:
        InterlockedIncrement((PLONG)&PCache->CountPbackReply);
        Field = (InternalOption **) &SRP->rep;
        break;
    case LQSR_OPTION_TYPE_ERROR:
        InterlockedIncrement((PLONG)&PCache->CountPbackError);
        Field = (InternalOption **) &SRP->err;
        break;
    case LQSR_OPTION_TYPE_ACK:
        InterlockedIncrement((PLONG)&PCache->CountPbackAck);
        Field = (InternalOption **) &SRP->ack;
        break;
    case LQSR_OPTION_TYPE_INFO:
        InterlockedIncrement((PLONG)&PCache->CountPbackInfo);
        Field = (InternalOption **) &SRP->inforep;
        break;
    default:
        ASSERT(!"PbackAddOptionToPacket bad option");
        ExFreePool(Opt);
        return;
    }

    Opt->Next = *Field;
    *Field = Opt;
}

//* PbackAddOptions
//
//  Removes options from the list, as appropriate,
//  and adds them to the packet.
//
//  Returns a count of how many options were added
//  and updates the packet size.
//
//  Should be called with the piggy-back cache locked,
//  if PrevPO is &PCache->List.
//
static uint
PbackAddOptions(
    PbackCache *PCache,
    PbackOption **PrevPO,       // Pointer to list of options.
    SRPacket *SRP,
    uint *Size,                 // Packet size.
    const VirtualAddress Dest)  // May be NULL, meaning all destinations.
{
    PbackOption *PO;
    uint Count = 0;

    //
    // Iterate over all waiting options looking
    // for options for this destination.
    //
    while ((PO = *PrevPO) != NULL) {

        //
        // Should this option be sent to this destination?
        //
        if ((Dest == NULL) ||
            VirtualAddressEqual(PO->Dest, Dest)) {
            uint OptionSize = sizeof(LQSROption) + PO->Opt->Opt.optDataLen;

            //
            // Would it make the packet too big?
            // NB: Currently we check against PROTOCOL_MIN_FRAME_SIZE,
            // but sometimes we know which physical link will be used
            // and perhaps that physical link has a larger MTU.
            // Furthermore some options will not be forwarded.
            // So in some cases we could safely make the packet larger
            // than PROTOCOL_MIN_FRAME_SIZE.
            //
            if (*Size + OptionSize > PROTOCOL_MIN_FRAME_SIZE) {
                InterlockedIncrement((PLONG)&PCache->CountPbackTooBig);
                goto KeepLooking;
            }

            //
            // Remove the option and add it to the packet.
            //
            *PrevPO = PO->Next;
            PbackAddOptionToPacket(PCache, SRP, PO->Opt);
            Count++;
            *Size += OptionSize;

            ExFreePool(PO);
        }
        else {
        KeepLooking:
            PrevPO = &PO->Next;
        }
    }

    return Count;
}

//* PbackPacketSize
//
//  Calculates the packet's current size, for the purposes of piggy-backing.
//  This makes worst-case assumptions about the possible growth in size
//  of some options.
//
uint
PbackPacketSize(SRPacket *SRP)
{
    uint Size;

    //
    // Start with the fixed-size LQSR header.
    //
    Size = sizeof(LQSRHeader);

    //
    // Add the size of the options.
    // This is exactly SROptionLength except
    // we assume worst-case size of Source Route
    // (because of Route Salvaging) and Route Request options.
    //
    if (SRP->req != NULL)
        Size += sizeof(LQSROption) + ROUTE_REQUEST_LEN(MAX_SR_LEN);
    Size += SROptionListLength((InternalOption *)SRP->rep);
    Size += SROptionListLength((InternalOption *)SRP->err);
    Size += SROptionListLength((InternalOption *)SRP->ackreq);
    Size += SROptionListLength((InternalOption *)SRP->ack);
    if (SRP->sr != NULL)
        Size += sizeof(LQSROption) + SOURCE_ROUTE_LEN(MAX_SR_LEN);
    Size += SROptionListLength((InternalOption *)SRP->inforeq);
    Size += SROptionListLength((InternalOption *)SRP->inforep);
    Size += SROptionListLength((InternalOption *)SRP->Probe);
    Size += SROptionListLength((InternalOption *)SRP->ProbeReply);
    Size += SROptionListLength((InternalOption *)SRP->LinkInfo);

    //
    // Add the size of the encrypted payload.
    //
    if (SRP->Packet != NULL) {
        uint Length;

        NdisQueryPacketLength(SRP->Packet, &Length);
        Size += Length;
    }

    return Size;
}

//* PbackSendPacketHelper
//
//  Helper for PbackSendPacket. Checks if there are any
//  waiting options which should be added to the packet.
//  Returns a count of how many options were added.
//
//  Should be called with the piggy-back cache locked,
//  if PrevPO is &PCache->List.
//
static uint
PbackSendPacketHelper(
    PbackCache *PCache,
    PbackOption **PrevPO,       // Pointer to list of options.
    SRPacket *SRP)
{
    uint Size;
    uint Count;

    //
    // There are many considerations when deciding whether
    // to piggy-back an option on a packet. Among them:
    // If you piggy-back on a route request, the option
    // will go everywhere, not just to the intended destination.
    // The larger packet will take up channel capacity over more area.
    // If you piggy-back on a source-routed packet, and the option
    // is intended for an intermediate destination in the source route,
    // and along the way the packet is salvaged, the option might
    // not get to its destination. Piggy-backing for the next-hop
    // destination and final destination does not have this problem.
    // If you piggy-back on an unreliable packet (probe or probe reply),
    // then there is no retransmission.
    // For the moment, we piggy-back as much as possible.
    //

    Size = PbackPacketSize(SRP);

    if (SRP->req != NULL) {

        //
        // Add all waiting options to this packet.
        //
        Count = PbackAddOptions(PCache, PrevPO, SRP, &Size, NULL);
    }
    else if (SRP->sr != NULL) {
        uint Hops = SOURCE_ROUTE_HOPS(SRP->sr->opt.optDataLen);
        uint i = Hops - SRP->sr->opt.segmentsLeft;

        //
        // Add options destined to all remaining nodes in the route.
        //
        for (Count = 0; i < Hops; i++)
            Count += PbackAddOptions(PCache, PrevPO, SRP, &Size,
                                     SRP->sr->opt.hopList[i].addr);
    }
    else {

        //
        // Add options destined for the packet's destination.
        //
        Count = PbackAddOptions(PCache, PrevPO, SRP, &Size, SRP->Dest);
    }

    return Count;
}

//* PbackSendPacket
//
//  Called when sending a packet. Checks if there are any
//  waiting options which should be added to the packet.
//
void
PbackSendPacket(
    MiniportAdapter *VA,
    SRPacket *SRP)
{
    PbackCache *PCache = &VA->PCache;
    uint Count;
    KIRQL OldIrql;

    KeAcquireSpinLock(&PCache->Lock, &OldIrql);
    Count = PbackSendPacketHelper(PCache, &PCache->List, SRP);
    PCache->Number -= Count;
    KeReleaseSpinLock(&PCache->Lock, OldIrql);
}

//* PbackSendComplete
//
//  Completes the transmission of a control packet,
//  which is carrying options that could not be piggy-backed.
//
static void
PbackSendComplete(
    MiniportAdapter *VA,
    SRPacket *SRP,
    NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(VA);
    UNREFERENCED_PARAMETER(Status);

    SRPacketFree(SRP);
}

//* PbackTimeout
//
//  Ensures that waiting options are sent by their timeout.
//
//  Called from MiniportTimeout at DPC level.
//
Time
PbackTimeout(
    MiniportAdapter *VA,
    Time Now)
{
    PbackCache *PCache = &VA->PCache;
    PbackOption *OptionList = NULL;
    PbackOption **NextPO = &OptionList;
    PbackOption *PO;
    Time Timeout = MAXTIME;
    uint Count = 0;

    //
    // Search the list of options, looking for the first option
    // which is not expired. The list is sorted by timeout.
    //
    KeAcquireSpinLockAtDpcLevel(&PCache->Lock);
    for (OptionList = PCache->List;
         (PO = *NextPO) != NULL;
         NextPO = &PO->Next) {

        if (PO->Timeout <= Now) {
            //
            // We will remove this option from the list.
            //
            Count++;
        }
        else {
            //
            // Update the time at which we should be called next.
            // We do not have to look at further options
            // because the list is sorted.
            //
            Timeout = PO->Timeout;
            break;
        }
    }

    //
    // Break the list in two.
    //
    *NextPO = NULL;
    PCache->List = PO;
    PCache->Number -= Count;
    KeReleaseSpinLockFromDpcLevel(&PCache->Lock);

    //
    // Now traverse the list of expired options,
    // generating packets.
    //
    while ((PO = OptionList) != NULL) {
        SRPacket *SRP;
        InternalSourceRoute *SR;
        NDIS_STATUS Status;
        boolint OKToSend;

        ASSERT(PO->Timeout <= Now);

        InterlockedIncrement((PLONG)&PCache->CountAloneTotal);
        switch (PO->Opt->Opt.optionType) {
        case LQSR_OPTION_TYPE_REPLY:
            InterlockedIncrement((PLONG)&PCache->CountAloneReply);
            break;
        case LQSR_OPTION_TYPE_ERROR:
            InterlockedIncrement((PLONG)&PCache->CountAloneError);
            break;
        case LQSR_OPTION_TYPE_ACK:
            InterlockedIncrement((PLONG)&PCache->CountAloneAck);
            break;
        case LQSR_OPTION_TYPE_INFO:
            InterlockedIncrement((PLONG)&PCache->CountAloneInfo);
            break;
        default:
            ASSERT(!"PbackTimeout bad option");
            break;
        }

        //
        // Allocate a packet with which to send the option.
        //
        SRP = ExAllocatePool(NonPagedPool, sizeof *SRP);
        if (SRP == NULL)
            goto DropOption;
        RtlZeroMemory(SRP, sizeof *SRP);

        //
        // Initialize source & destination of this packet.
        //
        RtlCopyMemory(SRP->Dest, PO->Dest, SR_ADDR_LEN);
        RtlCopyMemory(SRP->Source, VA->Address, SR_ADDR_LEN);

        //
        // Initialize a source route to this destination.
        //
        SR = ExAllocatePool(NonPagedPool,
                            sizeof *SR + sizeof(SRAddr)*MAX_SR_LEN);
        if (SR == NULL)
            goto DropOptionAndPacket;

        Status = LinkCacheFillSR(VA, SRP->Dest, SR);
        if (Status != NDIS_STATUS_SUCCESS) {
            ExFreePool(SR);

            if (Status == NDIS_STATUS_NO_ROUTE_TO_DESTINATION) {
                //
                // Didn't find a source route. Send via route request.
                // But first add PO and other expired options
                // to this destination. PbackSendPacket will be called
                // later, when this packet is about to go out.
                //
                (void) PbackSendPacketHelper(PCache, &OptionList, SRP);
                ASSERT(OptionList != PO);
                MiniportSendViaRouteRequest(VA, SRP, PbackSendComplete);
                continue;
            }

            ASSERT(Status == NDIS_STATUS_RESOURCES);
        DropOptionAndPacket:
            SRPacketFree(SRP);
        DropOption:
            //
            // Drop this option in the bit bucket.
            //
            OptionList = PO->Next;
            ExFreePool(PO->Opt);
            ExFreePool(PO);
            continue;
        }

        //
        // Send the packet using the source route.
        //
        SRP->sr = SR;

        //
        // Add PO and maybe other expired options to this packet.
        //
        (void) PbackSendPacketHelper(PCache, &OptionList, SRP);
        ASSERT(OptionList != PO);

        //
        // Check for other options that can be piggy-backed on this packet.
        //
        PbackSendPacket(VA, SRP);

        if ((SRP->rep == NULL) &&
            (SRP->err == NULL) &&
            (SRP->inforep == NULL)) {
            ASSERT(SRP->ack != NULL);
            //
            // This packet only contains acknowledgements.
            // Requesting an ack for an ack is a bad idea,
            // so suppress acknowledgements.
            //
            SR->opt.staticRoute = TRUE;
        }

        //
        // Send this packet using the maintenance buffer.
        //
        OKToSend = LinkCacheUseSR(VA, SRP);
        ASSERT(OKToSend);
        MaintBufSendPacket(VA, SRP, PbackSendComplete);
    }

    return Timeout;
}
