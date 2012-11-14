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

//* NeighborCacheInit
//
//  Initializes a neighbor cache.
//
void
NeighborCacheInit(NeighborCache *NC)
{
    KeInitializeSpinLock(&NC->Lock);
    NC->FirstNCE = NC->LastNCE = SentinelNCE(NC);
}

//* NeighborCacheCleanup
//
//  Uninitializes a neighbor cache.
//
void
NeighborCacheCleanup(NeighborCache *NC)
{
    NeighborCacheEntry *NCE;

    while ((NCE = NC->FirstNCE) != SentinelNCE(NC)) {
        //
        // Remove and free the NCE.
        //
        NCE->Next->Prev = NCE->Prev;
        NCE->Prev->Next = NCE->Next;
        ExFreePool(NCE);
    }
}

//* NeighborCacheFlushAddress
//
//  Removes entries from the neighbor cache.
//  If VAddr is NULL, removes all entries.
//  Otherwise it only removes entries for that virtual address.
//
void
NeighborCacheFlushAddress(
    NeighborCache *NC,
    const VirtualAddress VAddr,
    LQSRIf InIf)
{
    NeighborCacheEntry *NCE;
    NeighborCacheEntry *NextNCE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&NC->Lock, &OldIrql);
    for (NCE = NC->FirstNCE;
         NCE != SentinelNCE(NC);
         NCE = NextNCE) {
        NextNCE = NCE->Next;

        //
        // Should we remove this NCE?
        //
        if ((VAddr == NULL) ||
            (VirtualAddressEqual(NCE->VAddress, VAddr) &&
             (NCE->InIf == InIf))) {

            NCE->Next->Prev = NCE->Prev;
            NCE->Prev->Next = NCE->Next;
            ExFreePool(NCE);
        }
    }
    KeReleaseSpinLock(&NC->Lock, OldIrql);
}

//* NeighborFindOrCreate
//
//  Finds or creates a neighbor cache entry.
//  Called with the neighbor cache locked.
//
NeighborCacheEntry *
NeighborFindOrCreate(
    NeighborCache *NC,
    const VirtualAddress VAddr,
    LQSRIf InIf)
{
    NeighborCacheEntry *NCE;

    for (NCE = NC->FirstNCE;
         NCE != SentinelNCE(NC);
         NCE = NCE->Next) {
        //
        // Does the NCE already exist?
        //
        if (VirtualAddressEqual(NCE->VAddress, VAddr) &&
            (NCE->InIf == InIf))
            return NCE;
    }

    //
    // Create a new NCE.
    //
    NCE = ExAllocatePool(NonPagedPool, sizeof *NCE);
    if (NCE == NULL)
        return NULL;

    //
    // Initialize the NCE and insert it into the cache.
    // Our caller will initialize PAddress.
    //
    RtlCopyMemory(NCE->VAddress, VAddr, sizeof(VirtualAddress));
    NCE->InIf = InIf;

    NCE->Prev = NC->LastNCE;
    NCE->Prev->Next = NCE;
    NCE->Next = SentinelNCE(NC);
    NCE->Next->Prev = NCE;

    return NCE;
}

//* NeighborReceivePassive
//
//  Updates the neighbor cache in response
//  to the passive receipt of an address mapping.
//
void
NeighborReceivePassive(
    NeighborCache *NC,
    const VirtualAddress VAddr,
    LQSRIf InIf,
    const PhysicalAddress PAddr)
{
    NeighborCacheEntry *NCE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&NC->Lock, &OldIrql);
    NCE = NeighborFindOrCreate(NC, VAddr, InIf);
    if (NCE != NULL) {
        //
        // Update the physical address.
        //
        RtlCopyMemory(NCE->PAddress, PAddr, sizeof(PhysicalAddress));
    }
    KeReleaseSpinLock(&NC->Lock, OldIrql);
}

//* NeighborFindPhysical
//
//  Given a virtual address and physical adapter,
//  finds a corresponding physical address.
//  Returns FALSE on failure.
//
boolint
NeighborFindPhysical(
    NeighborCache *NC,
    const VirtualAddress VAddr,
    LQSRIf InIf,
    PhysicalAddress PAddr)
{
    NeighborCacheEntry *NCE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&NC->Lock, &OldIrql);
    for (NCE = NC->FirstNCE; ; NCE = NCE->Next) {
        if (NCE == SentinelNCE(NC)) {
            NCE = NULL;
            break;
        }

        //
        // Do we have an NCE for this virtual address?
        //
        if (VirtualAddressEqual(NCE->VAddress, VAddr) &&
            (NCE->InIf == InIf)) {
            //
            // Return the physical address.
            //
            RtlCopyMemory(PAddr, NCE->PAddress, sizeof(PhysicalAddress));
            break;
        }
    }
    KeReleaseSpinLock(&NC->Lock, OldIrql);
    return NCE != NULL;
}
