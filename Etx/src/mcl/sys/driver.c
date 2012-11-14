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

uint Version = 65;

DEVICE_OBJECT *OurDeviceObject;

PDRIVER_DISPATCH IoMajorFunctions[IRP_MJ_MAXIMUM_FUNCTION + 1];

//* DriverEntry
//
//  This is the driver entry point, called by NT upon loading us.
//  Main initialization routine for the MCL miniport driver.
//
NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,   // MCL driver object.
    IN PUNICODE_STRING RegistryPath)  // Path to our info in the registry.
{
    KdPrint(("MCL!DriverEntry(Driver %p)\n", DriverObject));

    //
    // Initialize our IO handlers.
    // NdisMRegisterDevice will need this.
    //
    IoMajorFunctions[IRP_MJ_CREATE] = IoCreate;
    IoMajorFunctions[IRP_MJ_CLEANUP] = IoCleanup;
    IoMajorFunctions[IRP_MJ_CLOSE] = IoClose;
    IoMajorFunctions[IRP_MJ_DEVICE_CONTROL] = IoControl;

    //
    // Initialize our data structures.
    //

#if COUNTING_MALLOC
    InitCountingMalloc();
#endif
    RandomInit();

    if (! MiniportInit(DriverObject, RegistryPath)) {
        KdPrint(("MCL!MiniportInit failed\n"));
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}
