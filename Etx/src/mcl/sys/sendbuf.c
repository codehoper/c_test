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

//* SendBufNew
//
//  Allocates a new send buffer with the specified maximum capacity.
//
NDIS_STATUS
SendBufNew(MiniportAdapter *VA, uint MaxSize)
{
    SendBuf *SB;
    SendBufPacket *SBP;
    uint i;

    SB = ExAllocatePool(NonPagedPool, sizeof *SB + MaxSize * sizeof *SBP);
    if (SB == NULL)
        return NDIS_STATUS_RESOURCES;

    KeInitializeSpinLock(&SB->Lock);
    SB->Packets = NULL;
    SB->Insert = &SB->Packets;
    SB->FreeList = NULL;

    SBP = (SendBufPacket *) (SB + 1);
    for (i = 0; i < MaxSize; i++) {
        SBP[i].Next = SB->FreeList;
        SB->FreeList = &SBP[i];
    }

    SB->Size = 0;
    SB->HighWater = 0;
    SB->MaxSize = MaxSize;

    VA->SB = SB;
    return NDIS_STATUS_SUCCESS;
}

//* SendBufConsistent
//
//  Checks the internal consistency of the send buffer.
//
//  Called with the send buffer locked.
//
static boolint
SendBufConsistent(SendBuf *SB)
{
    SendBufPacket *SBP;
    SendBufPacket **Prev;
    uint Size;
    Time Timeout;

    Size = 0;
    Timeout = 0;

    for (Prev = &SB->Packets;
         (SBP = *Prev) != NULL;
         Prev = &SBP->Next) {

        Size++;
        if (Timeout > SBP->Timeout)
            return FALSE;
        Timeout = SBP->Timeout;
    }

    if (Prev != SB->Insert)
        return FALSE;

    if ((Size != SB->Size) ||
        (Size > SB->HighWater) ||
        (SB->HighWater > SB->MaxSize))
        return FALSE;

    return TRUE;
}

//* SendBufFree
//
//  Deallocates the send buffer.
//
void
SendBufFree(MiniportAdapter *VA)
{
    SendBuf *SB = VA->SB;

    ASSERT(SB->Packets == NULL);
    ExFreePool(SB);
}

//* SendBufInsert
//
//  Inserts an SRPacket into the send buffer.
//
void
SendBufInsert(MiniportAdapter *VA, SRPacket *srp)
{
    SendBuf *SB = VA->SB;
    SRPacket *VictimSRP;
    SendBufPacket *SBP;
    Time Now;
    LQSRReqId Identifier;
    KIRQL OldIrql;

    //
    // We must do this before inserting the srp
    // into the send buffer and unlocking:
    // after that, srp could disappear.
    //
    // NB: Even if the request table will not let
    // us send a request before the send buffer will timeout,
    // it is still good to queue the packet in the send buffer.
    // It is quite possible that we will overhear a route
    // that will let us send the packet.
    //
    if (ReqTableSendP(VA, srp->Dest, &Identifier))
        MiniportSendRouteRequest(VA, srp->Dest, Identifier);

    KeAcquireSpinLock(&SB->Lock, &OldIrql);
    ASSERT(SendBufConsistent(SB));

    if (SB->FreeList != NULL) {
        //
        // Take a SendBufPacket from the free list.
        //
        SBP = SB->FreeList;
        SB->FreeList = SBP->Next;
        VictimSRP = NULL;
    }
    else if (SB->Packets != NULL) {
        //
        // Recycle a SendBufPacket.
        //
        SBP = SB->Packets;
        SB->Packets = SBP->Next;
        if (SB->Packets == NULL) {
            ASSERT(SB->Insert == &SBP->Next);
            SB->Insert = &SB->Packets;
        }
        SB->Size--;

        //
        // Complete the victim after unlocking.
        //
        VictimSRP = SBP->srp;
        KdPrint(("MCL!SendBufInsert: recycling srp %p\n", VictimSRP));
    }
    else {
        //
        // We can not queue this packet.
        //
        VictimSRP = srp;
        goto UnlockAndExit;
    }

    //
    // Insert the packet into the send buffer,
    // at the end of the list.
    //
    SBP->srp = srp;
    *SB->Insert = SBP;
    SB->Insert = &SBP->Next;
    SBP->Next = NULL;

    if (++SB->Size > SB->HighWater)
        SB->HighWater = SB->Size;

    //
    // Calculate a timeout for the packet.
    // Because we do this inside the lock,
    // the packet list is always in timeout order.
    //
    Now = KeQueryInterruptTime();
    SBP->Timeout = Now + SENDBUF_TIMEOUT;

UnlockAndExit:

    KeReleaseSpinLock(&SB->Lock, OldIrql);

    if (VictimSRP != NULL)
        MiniportSendComplete(VA, VictimSRP, NDIS_STATUS_RESOURCES);
}

//* SendBufCheck
//
//  Checks if any packets in the send buffer can be sent now,
//  or if we can send a Route Request for them.
//
void
SendBufCheck(MiniportAdapter *VA)
{
    SendBuf *SB = VA->SB;
    SendBufPacket *Packets;
    SendBufPacket *SBP;
    SRPacket *srp;
    Time Now;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    Now = KeQueryInterruptTime();

    //
    // Remove all the packets from the send buffer,
    // so we can play with them without holding the lock.
    //
    KeAcquireSpinLock(&SB->Lock, &OldIrql);
    Packets = SB->Packets;
    SB->Packets = NULL;
    SB->Insert = &SB->Packets;
    SB->Size = 0;
    KeReleaseSpinLock(&SB->Lock, OldIrql);

    //
    // Examine each packet and see if we can send it now.
    //

    while ((SBP = Packets) != NULL) {
        Packets = SBP->Next;

        //
        // The SRPacket already has a Source Route option,
        // from MiniportSendPacket.
        //
        srp = SBP->srp;
        ASSERT(srp->sr != NULL);

        //
        // See if we can route to the destination now.
        //
        Status = LinkCacheFillSR(VA, srp->Dest, srp->sr);
        if (Status == NDIS_STATUS_SUCCESS) {
            //
            // Is the transmit queue full?
            //
            if (! LinkCacheUseSR(VA, srp)) {
                InterlockedIncrement((PLONG)&VA->CountXmitQueueFull);
                goto GiveUp;
            }

            //
            // Hurray, we can send the packet now.
            //
            MaintBufSendPacket(VA, srp, MiniportSendComplete);
            goto PutOnFreeList;
        }
        else if ((Status != NDIS_STATUS_NO_ROUTE_TO_DESTINATION) ||
                 (SBP->Timeout < Now)) {

            InterlockedIncrement((PLONG)&VA->CountXmitNoRoute);

        GiveUp:
            //
            // Give up, we can not send this packet.
            //
            MiniportSendComplete(VA, srp, Status);

            //
            // Put the SendBufPacket on the free list.
            //
        PutOnFreeList:
            KeAcquireSpinLock(&SB->Lock, &OldIrql);
            SBP->Next = SB->FreeList;
            SB->FreeList = SBP;
            KeReleaseSpinLock(&SB->Lock, OldIrql);
        }
        else {
            LQSRReqId Identifier;
            SendBufPacket **Insert;
            SendBufPacket *NextSBP;

            //
            // Try to send another Route Request.
            //
            if (ReqTableSendP(VA, srp->Dest, &Identifier))
                MiniportSendRouteRequest(VA, srp->Dest, Identifier);

            //
            // Put the packet back in the send buffer.
            // So that the victim selection in SendBufInsert is FIFO,
            // take care to insert the packet in timeout order.
            //
            KeAcquireSpinLock(&SB->Lock, &OldIrql);

            //
            // Search for the insertion point in the list.
            //
            for (Insert = &SB->Packets;
                 (NextSBP = *Insert) != NULL;
                 Insert = &NextSBP->Next) {

                if (SBP->Timeout < NextSBP->Timeout)
                    break;
            }

            //
            // Insert the packet back in the list.
            //
            *Insert = SBP;
            SBP->Next = NextSBP;
            if (NextSBP == NULL) {
                ASSERT(SB->Insert == Insert);
                SB->Insert = &SBP->Next;
            }

            if (++SB->Size > SB->HighWater)
                SB->HighWater = SB->Size;

            KeReleaseSpinLock(&SB->Lock, OldIrql);
        }
    }
}

//* SendBufResetStatistics
//
//  Resets all counters and statistics gathering for the send buffer.
//
void
SendBufResetStatistics(MiniportAdapter *VA)
{
    SendBuf *SB = VA->SB;
    KIRQL OldIrql;

    KeAcquireSpinLock(&SB->Lock, &OldIrql);
    SB->HighWater = SB->Size;
    KeReleaseSpinLock(&SB->Lock, OldIrql);
}
