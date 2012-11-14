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
#include <ntddksec.h>
#include <crypto/rc4.h>

//* GetSystemRandomBits
//
// This routine requests a block of random bits from the KSecDD driver.
// This is not cheap.
//
NDIS_STATUS
GetSystemRandomBits(
    unsigned char *Buffer,
    unsigned int Length)
{
    UNICODE_STRING DeviceName;
    NTSTATUS Status;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;
    IO_STATUS_BLOCK iosb;
    KEVENT Event;

    RtlInitUnicodeString(&DeviceName, DD_KSEC_DEVICE_NAME_U);

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    //
    // Get the file and device objects for KDSECDD,
    // acquire a reference to the device-object,
    // release the unneeded reference to the file-object,
    // and build the I/O control request to issue to KSecDD.
    //

    Status = IoGetDeviceObjectPointer(&DeviceName, FILE_ALL_ACCESS,
                                        &FileObject, &DeviceObject);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("MCL!IoGetDeviceObjectPointer(KSecDD) -> %x\n", Status));
        return NDIS_STATUS_FAILURE;
    }
    ObReferenceObject(DeviceObject);
    ObDereferenceObject(FileObject);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_KSEC_RNG,
                                         DeviceObject,
                                         NULL,    // No input buffer.
                                         0,
                                         Buffer,
                                         Length,
                                         FALSE,
                                         &Event,
                                         &iosb);
    if (Irp == NULL) {
        ObDereferenceObject(DeviceObject);
        return NDIS_STATUS_RESOURCES;
    }

    //
    // Issue the I/O control request, wait for it to complete
    // if necessary, and release the reference to KSecDD's device-object.
    //
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,       // Not alertable.
                              NULL);       // No timeout.
        Status = iosb.Status;
    }
    ObDereferenceObject(DeviceObject);

    if (!NT_SUCCESS(Status)) {
        KdPrint(("MCL!IoCallDriver(IOCTL_KSEC_RNG) -> %x\n", Status));
        return NDIS_STATUS_FAILURE;
    }

    return NDIS_STATUS_SUCCESS;
}

//
// We use RC4 to generate pseudo-random numbers.
// The RC4 key must be protected against simultaneous use.
//
KSPIN_LOCK RandomKeyLock;
RC4_KEYSTRUCT RandomKey;

//* RandomInit
//
//  Initialize the pseudo-random number module.
//  Returns TRUE for success and FALSE for failure.
// 
int
RandomInit(void)
{
    //
    // How many random bits should we use to initialize RC4?
    // Internally RC4 initialization uses a maximum of 256 bytes.
    //
    uchar RandomBits[256];
    NDIS_STATUS Status;

    Status = GetSystemRandomBits(RandomBits, sizeof RandomBits);
    if (Status != NDIS_STATUS_SUCCESS)
        return FALSE;

    KeInitializeSpinLock(&RandomKeyLock);
    rc4_key(&RandomKey, sizeof RandomBits, RandomBits);

    //
    // Zero sensitive information.
    //
    RtlSecureZeroMemory(RandomBits, sizeof RandomBits);
    return TRUE;
}

//* GetRandom
//
//  Get random bytes.
//
void
GetRandom(uchar *Buffer, uint Length)
{
    KIRQL OldIrql;

    //
    // We get random bits by using rc4 to encrypt the buffer.
    // No need to initialize the buffer first.
    //
    KeAcquireSpinLock(&RandomKeyLock, &OldIrql);
    rc4(&RandomKey, Length, Buffer);
    KeReleaseSpinLock(&RandomKeyLock, OldIrql);
}

//* GetRandomNumber
//
//  Returns a random integer in the range [0, Max).
//
uint
GetRandomNumber(uint Max)
{
    uint Random;

    //
    // Get a random 32-bit integer.
    //
    GetRandom((uchar *)&Random, sizeof Random);

    //
    // This is better than Random % Max,
    // which is biased when Max is not a power of 2.
    //
    return (uint)(((ULONGLONG)Random * Max) >> 32);
}
