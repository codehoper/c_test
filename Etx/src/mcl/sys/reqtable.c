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

//* ReqTableNew
//
//  Creates a new ForwardedRequestTable of the specified size.
//
NDIS_STATUS
ReqTableNew(MiniportAdapter *VA, uint Size)
{
    ForwardedRequestTable *FRT;
    RequestTableElement *RTE;
    uint i;

    FRT = ExAllocatePool(NonPagedPool, sizeof *FRT + Size * sizeof *RTE);
    if (FRT == NULL)
        return NDIS_STATUS_RESOURCES;

    RTE = (RequestTableElement *) (FRT + 1);
    FRT->RTE = RTE;
    KeInitializeSpinLock(&FRT->Lock);
    FRT->Victim = 0;
    FRT->MaxSize = Size;
    FRT->CurSize = 0;

    RtlZeroMemory(RTE, Size * sizeof *RTE);
    for (i = 0; i < Size; i++)
        GetRandom((uchar *)&FRT->RTE[i].NextID,
                  sizeof FRT->RTE[i].NextID);

    FRT->MinElementReuse = MAXTIME;
    FRT->MinSuppressReuse = MAXTIME;

    VA->ReqTable = FRT;
    return NDIS_STATUS_SUCCESS;
}

//* ReqTableFree
//
//  Deallocates the ForwardedRequestTable.
//
void
ReqTableFree(MiniportAdapter *VA)
{
    ForwardedRequestTable *FRT = VA->ReqTable;
    ExFreePool(FRT);
}

//* ReqTableFind
//
//  Finds a RequestTableElement for the Address.
//
//  Called with the ForwardedRequestTable already locked.
//
static RequestTableElement *
ReqTableFind(
    ForwardedRequestTable *FRT,
    const VirtualAddress Address)
{
    Time Now;
    RequestTableElement *RTE;
    uint i;

    Now = KeQueryInterruptTime();

    //
    // Check if the RequestTableElement already exists.
    //
    for (i = 0; i < FRT->CurSize; i++) {
        RTE = &FRT->RTE[i];
        if (RtlEqualMemory(RTE->Addr, Address, SR_ADDR_LEN))
            goto Return;
    }

    //
    // Allocate a RequestTableElement for this Address.
    //
    if (FRT->CurSize == FRT->MaxSize) {
        Time UnusedTime;

        //
        // Recycle the oldest existing RequestTableElement.
        //
        i = FRT->Victim;
        FRT->Victim = (FRT->Victim + 1) % FRT->MaxSize;
        RTE = &FRT->RTE[i];

        UnusedTime = Now - RTE->LastUsed;
        if (UnusedTime < FRT->MinElementReuse)
            FRT->MinElementReuse = UnusedTime;
    }
    else {
        //
        // Allocate the next unused RequestTableElement.
        //
        RTE = &FRT->RTE[FRT->CurSize++];
    }

    //
    // Initialize the new RequestTableElement.
    //
    RtlCopyMemory(RTE->Addr, Address, SR_ADDR_LEN);
    RtlZeroMemory(RTE->Suppress,
                  NUM_DUPLICATE_SUPPRESS * sizeof(DuplicateSuppress));
    RTE->Backoff = 0;
    RTE->Victim  = 0;

Return:
    RTE->LastUsed = Now;
    return RTE;
}

//* ReqTableElementSuppress
//
//  Checks if a Target/Identifier pair is present in the request table.
//
//  Called with the ForwardedRequestTable already locked.
//
static boolint
ReqTableElementSuppress(
    ForwardedRequestTable *FRT,
    RequestTableElement *RTE,
    const VirtualAddress Target,
    LQSRReqId Identifier)
{
    Time Now;
    uint i;

    Now = KeQueryInterruptTime();

    //
    // Have we already seen this Target/Identifier pair?
    //

    for (i = 0; i < NUM_DUPLICATE_SUPPRESS; i++) {
        if ((RTE->Suppress[i].Id == Identifier) &&
            RtlEqualMemory(RTE->Suppress[i].Target, Target, SR_ADDR_LEN)) {
            //
            // We found the Target/Identifier pair.
            //
            RTE->Suppress[i].LastUsed = Now;
            return TRUE;
        }
    }

    //
    // We did not find the Target/Identifier pair.
    // But remember it now.
    //

    i = RTE->Victim;
    RTE->Victim = (RTE->Victim + 1) % NUM_DUPLICATE_SUPPRESS;

    RtlCopyMemory(RTE->Suppress[i].Target, Target, SR_ADDR_LEN);
    RTE->Suppress[i].Id = Identifier;

    if (RTE->Suppress[i].LastUsed != 0) {
        Time UnusedTime;

        UnusedTime = Now - RTE->Suppress[i].LastUsed;
        if (UnusedTime < FRT->MinSuppressReuse)
            FRT->MinSuppressReuse = UnusedTime;
    }

    RTE->Suppress[i].LastUsed = Now;
    return FALSE;
}

//* ReqTableSuppress
//
//  Checks if we have already seen a Target/Identifier pair
//  from the Source. In any case, remembers the pair
//  for future calls.
//
boolint
ReqTableSuppress(
    MiniportAdapter *VA,
    const VirtualAddress Source,
    const VirtualAddress Target,
    LQSRReqId Identifier)
{
    ForwardedRequestTable *FRT = VA->ReqTable;
    RequestTableElement *RTE;
    boolint Suppress;
    KIRQL OldIrql;

    KeAcquireSpinLock(&FRT->Lock, &OldIrql);
    RTE = ReqTableFind(FRT, Source);
    Suppress = ReqTableElementSuppress(FRT, RTE, Target, Identifier);
    KeReleaseSpinLock(&FRT->Lock, OldIrql);

    return Suppress;
}

//* ReqTableIdentifier
//
//  Gets an identifier to use in generating a new request
//  for the Target address.
//
LQSRReqId
ReqTableIdentifier(
    MiniportAdapter *VA,
    const VirtualAddress Target)
{
    ForwardedRequestTable *FRT = VA->ReqTable;
    RequestTableElement *RTE;
    LQSRReqId Identifier;
    KIRQL OldIrql;

    KeAcquireSpinLock(&FRT->Lock, &OldIrql);
    RTE = ReqTableFind(FRT, Target);
    Identifier = RTE->NextID++;
    KeReleaseSpinLock(&FRT->Lock, OldIrql);

    return Identifier;
}

//* ReqTableSendP
//
//  Decides if we should send a Route Request for the specified address.
//  If so, returns the Identifier.
//
boolint
ReqTableSendP(
    MiniportAdapter *VA,
    const VirtualAddress Target,
    LQSRReqId *Identifier)
{
    ForwardedRequestTable *FRT = VA->ReqTable;
    RequestTableElement *RTE;
    Time Now;
    KIRQL OldIrql;
    Time Timeout;

    KeAcquireSpinLock(&FRT->Lock, &OldIrql);
    Now = KeQueryInterruptTime();

    RTE = ReqTableFind(FRT, Target);

    //
    // If we have not sent a Route Request for this Target
    // (since the last Reply), yes we can send a Route Request.
    //
    if (RTE->Backoff == 0)
        goto SendRequest;

    //
    // Otherwise we need to check if sufficient time has elapsed
    // since the last Route Request.
    //

    Timeout = ((Time) FIRST_BACKOFF) << (RTE->Backoff - 1);
    if (Timeout > MAX_BACKOFF)
        Timeout = MAX_BACKOFF;

    if (RTE->LastReq + Timeout > Now) {
        //
        // We can not send a Route Request yet.
        //
#if 0
        KdPrint(("MCL!ReqTableSendP: suppressing "
                 "target %02x-%02x-%02x-%02x-%02x-%02x "
                 "backoff %u\n",
                 Target[0], Target[1], Target[2],
                 Target[3], Target[4], Target[5],
                 RTE->Backoff));
#endif

        KeReleaseSpinLock(&FRT->Lock, OldIrql);
        return FALSE;
    }

SendRequest:
    //
    // Yes, we can send a Route Request now.
    //
    RTE->LastReq = Now;
    RTE->Backoff++;
    *Identifier = RTE->NextID++;

    KeReleaseSpinLock(&FRT->Lock, OldIrql);
    return TRUE;
}

//* ReqTableReceivedReply
//
//  Called when we have received a Route Reply,
//  so we need to reset our Backoff value for the Target.
//
void
ReqTableReceivedReply(
    MiniportAdapter *VA,
    const VirtualAddress Target)
{
    ForwardedRequestTable *FRT = VA->ReqTable;
    RequestTableElement *RTE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&FRT->Lock, &OldIrql);
    RTE = ReqTableFind(FRT, Target);
    RTE->Backoff = 0;
    KeReleaseSpinLock(&FRT->Lock, OldIrql);
}

//* ReqTableResetStatistics
//
//  Resets all counters and statistics gathering for the request table.
//
void
ReqTableResetStatistics(MiniportAdapter *VA)
{
    ForwardedRequestTable *FRT = VA->ReqTable;
    KIRQL OldIrql;

    KeAcquireSpinLock(&FRT->Lock, &OldIrql);
    FRT->MinElementReuse = MAXTIME;
    FRT->MinSuppressReuse = MAXTIME;
    KeReleaseSpinLock(&FRT->Lock, OldIrql);
}
