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

static void
MiniportSendInfo(MiniportAdapter *VA, ProtocolAdapter *PA,
                 const SRPacket *srp,
                 const InternalInfoRequest *InfoReq);

static void
MiniportRescheduleTimeoutHelper(MiniportAdapter *VA, Time Now, Time Timeout);

uint NextVirtualAdapterIndex;
NDIS_HANDLE MiniportHandle;
NDIS_HANDLE OurDeviceHandle;

struct MiniportAdapters MiniportAdapters;

#define MINIPORT_MAX_MULTICAST_ADDRESS  16
#define MINIPORT_MAX_FRAME_SIZE         1280
#define MINIPORT_HEADER_SIZE            sizeof(EtherHeader)
#define MINIPORT_MAX_LOOKAHEAD          0xffff

#define MAX_RECV_QUEUE_SIZE 16

#define MINIPORT_TIMEOUT        100     // Milliseconds.

#define DEFAULT_METRIC WCETT 

static void
MiniportTimeout(PKDPC Dpc, void *Context, void *Unused1, void *Unused2);

//* FindVirtualAdapterFromIndex
//
//  Given an index, returns the virtual adapter.
//
MiniportAdapter *
FindVirtualAdapterFromIndex(uint Index)
{
    MiniportAdapter *VA;
    KIRQL OldIrql;

    KeAcquireSpinLock(&MiniportAdapters.Lock, &OldIrql);
    for (VA = MiniportAdapters.VirtualAdapters;
         VA != NULL;
         VA = VA->Next) {
        //
        // Is this the desired virtual adapter?
        //
        if (VA->Index == Index)
            break;
    }
    KeReleaseSpinLock(&MiniportAdapters.Lock, OldIrql);

    return VA;
}

//* FindVirtualAdapterFromGuid
//
//  Given a guid, returns the virtual adapter.
//
MiniportAdapter *
FindVirtualAdapterFromGuid(const GUID *Guid)
{
    MiniportAdapter *VA;
    KIRQL OldIrql;

    KeAcquireSpinLock(&MiniportAdapters.Lock, &OldIrql);
    for (VA = MiniportAdapters.VirtualAdapters;
         VA != NULL;
         VA = VA->Next) {
        //
        // Is this the desired virtual adapter?
        //
        if (RtlEqualMemory(Guid, &VA->Guid, sizeof(GUID)))
            break;
    }
    KeReleaseSpinLock(&MiniportAdapters.Lock, OldIrql);

    return VA;
}

//* MiniportReadOrCreateAdapterAddress
//
//  Reads a virtual address from the registry,
//  or if the address is not present, creates a new address
//  and persists it in the registry.
//
NDIS_STATUS
MiniportReadOrCreateAdapterAddress(
    IN HANDLE KeyHandle,
    OUT VirtualAddress Address)
{
    NTSTATUS Status;

    Status = GetRegNetworkAddress(KeyHandle, L"NetworkAddress", Address);
    if (! NT_SUCCESS(Status)) {

        //
        // Create a pseudo-random address.
        //
        GetRandom(Address, VIRTUAL_ADDR_LENGTH);

        //
        // Clear the individual/group bit and
        // the local/universal bit:
        // We want a universal individual address.
        //
        IEEE_802_ADDR_CLEAR_GROUP(Address);
        IEEE_802_ADDR_CLEAR_LOCAL(Address);

        //
        // Write the new address to the registry.
        //
        Status = SetRegNetworkAddress(KeyHandle, L"NetworkAddress", Address);
        if (! NT_SUCCESS(Status)) {
            KdPrint(("MCL!SetRegNetworkAddress -> %x\n", Status));
            return NDIS_STATUS_FAILURE;
        }
    }

    return NDIS_STATUS_SUCCESS;
}

//* MiniportIsInfinite
//
//  Returns TRUE if the link metric indicates that the link
//  is effectively broken.
//
boolint
MiniportIsInfinite(uint Metric)
{
    return Metric == (uint)-1;
}

//* MiniportConvMetric
//
//  Converts a link metric to a path metric.
//
uint
MiniportConvMetric(uint LinkMetric)
{
    return LinkMetric;
}

//* MiniportPathMetric
//
//  Calculates the path metric of an array of links,
//  by summing the metrics of the links.
//
//  Called with the link cache locked.
//
uint
MiniportPathMetric(
    MiniportAdapter *VA,
    Link **Hops,
    uint NumHops)
{
    uint Metric;
    uint New;
    uint i;

    Metric = 0;
    for (i = 0; i < NumHops; i++) {
        //
        // Check for a broken link.
        //
        if ((*VA->IsInfinite)(Hops[i]->Metric)) {
            Metric = (uint)-1;
            break;
        }

        //
        // Check for overflow.
        //
        New = Metric + (*VA->ConvMetric)(Hops[i]->Metric);
        if (New < Metric) {
            Metric = (uint)-1;
            break;
        }
        Metric = New;
    }
    return Metric;
}

//* MiniportInitLinkMetric
//
//  Initialize metric information for a new link.
//
void 
MiniportInitLinkMetric(
    MiniportAdapter *VA,
    int SNode,
    Link *Link, 
    Time Now)
{
    UNREFERENCED_PARAMETER(VA);
    UNREFERENCED_PARAMETER(SNode);
    UNREFERENCED_PARAMETER(Now);

    Link->Metric = 1;
}

//* MiniportPersistControl
//
//  Writes configuration parameters to the registry.
//
NTSTATUS
MiniportPersistControl(
    MiniportAdapter *VA,
    MCL_CONTROL_VIRTUAL_ADAPTER *Control)
{
    NTSTATUS Status;
    HANDLE DeviceKey;
    HANDLE Key;

    //
    // We do not need to validate the configuration parameters.
    // They will be validated after being read from the registry.
    //

    Status = IoOpenDeviceRegistryKey(VA->PhysicalDeviceObject,
                                     PLUGPLAY_REGKEY_DRIVER,
                                     GENERIC_READ,
                                     &DeviceKey);
    if (! NT_SUCCESS(Status))
        return Status;

    Status = OpenRegKey(&Key, DeviceKey, L"Parameters", OpenRegKeyCreate);
    ZwClose(DeviceKey);
    if (! NT_SUCCESS(Status))
        return Status;

    if (Control->Snooping != (uint)-1) {
        Status = SetRegDWORDValue(Key, L"Snooping", Control->Snooping);
        if (! NT_SUCCESS(Status))
            goto CloseReturn;
    }

    if (Control->ArtificialDrop != (uint)-1) {
        Status = SetRegDWORDValue(Key, L"ArtificialDrop",
                                  Control->ArtificialDrop);
        if (! NT_SUCCESS(Status))
            goto CloseReturn;
    }

    if (Control->RouteFlapDampingFactor != (uint)-1) {
        Status = SetRegDWORDValue(Key, L"RouteFlapDampingFactor",
                                  Control->RouteFlapDampingFactor);
        if (! NT_SUCCESS(Status))
            goto CloseReturn;
    }

    if (Control->Crypto != (uint)-1) {
        Status = SetRegDWORDValue(Key, L"Crypto", Control->Crypto);
        if (! NT_SUCCESS(Status))
            goto CloseReturn;
    }

    if (LQSR_KEY_PRESENT(Control->CryptoKeyMAC)) {
        Status = SetRegBinaryValue(Key, L"CryptoKeyMAC",
                        Control->CryptoKeyMAC, sizeof Control->CryptoKeyMAC);
        if (! NT_SUCCESS(Status))
            goto CloseReturn;
    }

    if (LQSR_KEY_PRESENT(Control->CryptoKeyAES)) {
        Status = SetRegBinaryValue(Key, L"CryptoKeyAES",
                        Control->CryptoKeyAES, sizeof Control->CryptoKeyAES);
        if (! NT_SUCCESS(Status))
            goto CloseReturn;
    }

    if (Control->LinkTimeout != (uint)-1) {
        Status = SetRegDWORDValue(Key, L"LinkTimeout", Control->LinkTimeout);
        if (! NT_SUCCESS(Status))
            goto CloseReturn;
    }

    if (Control->MetricType != (uint)-1) {
        Status = SetRegDWORDValue(Key, L"MetricType", Control->MetricType);
        if (! NT_SUCCESS(Status))
            goto CloseReturn;
    }

    switch (Control->MetricType) {
    case METRIC_TYPE_RTT:
        if (Control->MetricParams.Rtt.Alpha != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"Alpha",
                                      Control->MetricParams.Rtt.Alpha);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Rtt.ProbePeriod != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"ProbePeriod",
                                      Control->MetricParams.Rtt.ProbePeriod);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Rtt.PenaltyFactor != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"PenaltyFactor",
                                      Control->MetricParams.Rtt.PenaltyFactor);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Rtt.HysteresisPeriod != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"HysteresisPeriod",
                                      Control->MetricParams.Rtt.HysteresisPeriod);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Rtt.SweepPeriod != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"SweepPeriod",
                                      Control->MetricParams.Rtt.SweepPeriod);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Rtt.Random != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"Random",
                                      Control->MetricParams.Rtt.Random);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Rtt.OutIfOverride != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"OutIfOverride",
                                      Control->MetricParams.Rtt.OutIfOverride);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        break;

    case METRIC_TYPE_PKTPAIR:
        if (Control->MetricParams.PktPair.Alpha != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"Alpha",
                                    Control->MetricParams.PktPair.Alpha);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.PktPair.ProbePeriod != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"ProbePeriod",
                                    Control->MetricParams.PktPair.ProbePeriod);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.PktPair.PenaltyFactor != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"PenaltyFactor",
                                    Control->MetricParams.PktPair.PenaltyFactor);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        break;

    case METRIC_TYPE_ETX:
        if (Control->MetricParams.Etx.Alpha != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"Alpha",
                                    Control->MetricParams.Etx.Alpha);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Etx.ProbePeriod != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"ProbePeriod",
                                    Control->MetricParams.Etx.ProbePeriod);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Etx.PenaltyFactor != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"PenaltyFactor",
                                    Control->MetricParams.Etx.PenaltyFactor);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Etx.LossInterval != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"LossInterval",
                                    Control->MetricParams.Etx.LossInterval);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        break;

    case METRIC_TYPE_WCETT:
        if (Control->MetricParams.Wcett.Alpha != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"Alpha",
                                    Control->MetricParams.Wcett.Alpha);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Wcett.ProbePeriod != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"ProbePeriod",
                                    Control->MetricParams.Wcett.ProbePeriod);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Wcett.PenaltyFactor != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"PenaltyFactor",
                                    Control->MetricParams.Wcett.PenaltyFactor);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Wcett.LossInterval != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"LossInterval",
                                    Control->MetricParams.Wcett.LossInterval);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Wcett.Beta != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"Beta",
                                    Control->MetricParams.Wcett.Beta);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Wcett.PktPairProbePeriod != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"PktPairProbePeriod",
                                    Control->MetricParams.Wcett.PktPairProbePeriod);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        if (Control->MetricParams.Wcett.PktPairMinOverProbes != (uint)-1) {
            Status = SetRegDWORDValue(Key, L"PktPairMinOverProbes",
                                    Control->MetricParams.Wcett.PktPairMinOverProbes);
            if (! NT_SUCCESS(Status))
                goto CloseReturn;
        }
        break;
    }

    Status = STATUS_SUCCESS;
CloseReturn:
    ZwClose(Key);
    return Status;
}

//* MiniportReadControl
//
//  Reads configuration parameters from the registry.
//
void
MiniportReadControl(
    MiniportAdapter *VA,
    MCL_CONTROL_VIRTUAL_ADAPTER *Control)
{
    NTSTATUS Status;
    HANDLE DeviceKey;
    HANDLE Key;

    //
    // We must always return an initialized Control structure.
    //
    MCL_INIT_CONTROL_VIRTUAL_ADAPTER(Control);

    Status = IoOpenDeviceRegistryKey(VA->PhysicalDeviceObject,
                                     PLUGPLAY_REGKEY_DRIVER,
                                     GENERIC_READ,
                                     &DeviceKey);
    if (! NT_SUCCESS(Status))
        return;

    Status = OpenRegKey(&Key, DeviceKey, L"Parameters", OpenRegKeyCreate);
    ZwClose(DeviceKey);
    if (! NT_SUCCESS(Status))
        return;

    //
    // Read configuration parameters.
    //

    (void) GetRegDWORDValue(Key, L"Snooping",
                            (PULONG)&Control->Snooping);
    (void) GetRegDWORDValue(Key, L"ArtificialDrop",
                            (PULONG)&Control->ArtificialDrop);
    (void) GetRegDWORDValue(Key, L"RouteFlapDampingFactor",
                            (PULONG)&Control->RouteFlapDampingFactor);

    (void) GetRegDWORDValue(Key, L"Crypto",
                            (PULONG)&Control->Crypto);
    (void) GetRegBinaryValue(Key, L"CryptoKeyMAC",
                Control->CryptoKeyMAC, sizeof Control->CryptoKeyMAC);
    (void) GetRegBinaryValue(Key, L"CryptoKeyAES",
                Control->CryptoKeyAES, sizeof Control->CryptoKeyAES);

    (void) GetRegDWORDValue(Key, L"LinkTimeout",
                            (PULONG)&Control->LinkTimeout);

    (void) GetRegDWORDValue(Key, L"MetricType",
                            (PULONG)&Control->MetricType);
    switch (Control->MetricType) {
    case METRIC_TYPE_RTT:
        MCL_INIT_CONTROL_RTT(Control);
        (void) GetRegDWORDValue(Key, L"Alpha",
                                (PULONG)&Control->MetricParams.Rtt.Alpha);
        (void) GetRegDWORDValue(Key, L"ProbePeriod",
                                (PULONG)&Control->MetricParams.Rtt.ProbePeriod);
        (void) GetRegDWORDValue(Key, L"PenaltyFactor",
                                (PULONG)&Control->MetricParams.Rtt.PenaltyFactor);
        (void) GetRegDWORDValue(Key, L"HysteresisPeriod",
                                (PULONG)&Control->MetricParams.Rtt.HysteresisPeriod);
        (void) GetRegDWORDValue(Key, L"SweepPeriod",
                                (PULONG)&Control->MetricParams.Rtt.SweepPeriod);
        (void) GetRegDWORDValue(Key, L"Random",
                                (PULONG)&Control->MetricParams.Rtt.Random);
        (void) GetRegDWORDValue(Key, L"OutIfOverride",
                                (PULONG)&Control->MetricParams.Rtt.OutIfOverride);
        break;

    case METRIC_TYPE_PKTPAIR:
        MCL_INIT_CONTROL_PKTPAIR(Control);
        (void) GetRegDWORDValue(Key, L"Alpha",
                                (PULONG)&Control->MetricParams.PktPair.Alpha);
        (void) GetRegDWORDValue(Key, L"ProbePeriod",
                                (PULONG)&Control->MetricParams.PktPair.ProbePeriod);
        (void) GetRegDWORDValue(Key, L"PenaltyFactor",
                                (PULONG)&Control->MetricParams.PktPair.PenaltyFactor);
        break;

    case METRIC_TYPE_ETX:
        MCL_INIT_CONTROL_ETX(Control);
        (void) GetRegDWORDValue(Key, L"Alpha",
                                (PULONG)&Control->MetricParams.Etx.Alpha);
        (void) GetRegDWORDValue(Key, L"ProbePeriod",
                                (PULONG)&Control->MetricParams.Etx.ProbePeriod);
        (void) GetRegDWORDValue(Key, L"PenaltyFactor",
                                (PULONG)&Control->MetricParams.Etx.PenaltyFactor);
        (void) GetRegDWORDValue(Key, L"LossInterval",
                                (PULONG)&Control->MetricParams.Etx.LossInterval);
        break;

    case METRIC_TYPE_WCETT:
        MCL_INIT_CONTROL_WCETT(Control);
        (void) GetRegDWORDValue(Key, L"Alpha",
                                (PULONG)&Control->MetricParams.Wcett.Alpha);
        (void) GetRegDWORDValue(Key, L"ProbePeriod",
                                (PULONG)&Control->MetricParams.Wcett.ProbePeriod);
        (void) GetRegDWORDValue(Key, L"PenaltyFactor",
                                (PULONG)&Control->MetricParams.Wcett.PenaltyFactor);
        (void) GetRegDWORDValue(Key, L"LossInterval",
                                (PULONG)&Control->MetricParams.Wcett.LossInterval);
        (void) GetRegDWORDValue(Key, L"Beta",
                                (PULONG)&Control->MetricParams.Wcett.Beta);
        (void) GetRegDWORDValue(Key, L"PktPairProbePeriod",
                                (PULONG)&Control->MetricParams.Wcett.PktPairProbePeriod);
        (void) GetRegDWORDValue(Key, L"PktPairMinOverProbes",
                                (PULONG)&Control->MetricParams.Wcett.PktPairMinOverProbes);
        break;
    }

    ZwClose(Key);
}

//* MiniportControl
//
//  Sets configuration parameters from the structure.
//  Note that CryptoKeyMAC, CryptoKeyAES, and MetricType
//  can not be modified.
//
NTSTATUS
MiniportControl(
    MiniportAdapter *VA,
    MCL_CONTROL_VIRTUAL_ADAPTER *Control)
{
    // 
    // Check each parameter first.
    //

    if ((Control->MetricType != (uint)-1) &&
        (Control->MetricType != VA->MetricType)) {
        //
        // We do not allow the MetricType to change.
        //
        return STATUS_INVALID_PARAMETER_2;
    }

    switch (Control->MetricType) {
    case METRIC_TYPE_RTT:
        if (Control->MetricParams.Rtt.Alpha != (uint)-1) {
            //
            // Alpha is interpreted by rtt.c as Alpha/MAXALPHA.
            // Since the result of this division should
            // not exceed 1, the maximum value of Alpha is MAXALPHA.
            //   
            if (Control->MetricParams.Rtt.Alpha > MAXALPHA)
                return STATUS_INVALID_PARAMETER_3;
        }

        if (Control->MetricParams.Rtt.ProbePeriod != (uint)-1) {
            //
            // The probe period should not be less than 100ms to
            // prevent overload.
            // 
            if (Control->MetricParams.Rtt.ProbePeriod < 100 * MILLISECOND)
                return STATUS_INVALID_PARAMETER_4;
        }

        if (Control->MetricParams.Rtt.HysteresisPeriod != (uint)-1) {
            //
            // The hysteresis period should not be less than 100ms to
            // prevent overload.
            // 
            if (Control->MetricParams.Rtt.HysteresisPeriod < 100 * MILLISECOND)
                return STATUS_INVALID_PARAMETER_5;
        }

        if (Control->MetricParams.Rtt.PenaltyFactor != (uint)-1) {
            //
            // The penalty factor should be at most 32 to avoid
            // overflow problems.
            // 
            if ((Control->MetricParams.Rtt.PenaltyFactor < 1) ||
                (Control->MetricParams.Rtt.PenaltyFactor > 32))
                return STATUS_INVALID_PARAMETER_6;
        }

        if (Control->MetricParams.Rtt.SweepPeriod != (uint)-1) {
            //
            // The sweep period should not be less than 1ms 
            // to prevent system overload. 
            // 
            if (Control->MetricParams.Rtt.SweepPeriod < 1 * MILLISECOND)
                return STATUS_INVALID_PARAMETER_7;
        }
        break;

    case METRIC_TYPE_PKTPAIR:
        if (Control->MetricParams.PktPair.Alpha != (uint)-1) {
            //
            // Alpha is interpreted as Alpha/MAXALPHA.
            // Since the result of this division should
            // not exceed 1, the maximum value of Alpha is MAXALPHA.
            //   
            if (Control->MetricParams.PktPair.Alpha > MAXALPHA)
                return STATUS_INVALID_PARAMETER_3;
        }

        if (Control->MetricParams.PktPair.ProbePeriod != (uint)-1) {
            //
            // The probe period should not be less than 100ms to
            // prevent system overload.
            // 
            if (Control->MetricParams.PktPair.ProbePeriod < 100 * MILLISECOND)
                return STATUS_INVALID_PARAMETER_4;
        }

        if (Control->MetricParams.PktPair.PenaltyFactor != (uint)-1) {
            //
            // The penalty factor should be at most 32 to avoid
            // overflow problems.
            // 
            if ((Control->MetricParams.PktPair.PenaltyFactor < 1) ||
                (Control->MetricParams.PktPair.PenaltyFactor > 32))
                return STATUS_INVALID_PARAMETER_5;
        }
        break;

    case METRIC_TYPE_ETX:
        if (Control->MetricParams.Etx.ProbePeriod != (uint)-1) {
            //
            // The probe period should not be less than 100ms. 
            // to prevent system overload.
            // 
            if (Control->MetricParams.Etx.ProbePeriod < 100 * MILLISECOND)
                return STATUS_INVALID_PARAMETER_3;
        }

        if (Control->MetricParams.Etx.LossInterval != (uint)-1) {
            //
            // The loss interval should not be less than 100ms. 
            // to prevent system overload. The upper bound is 1 minute
            // to prevent too much memory consumption.
            // 
            if ((Control->MetricParams.Etx.LossInterval < 100 * MILLISECOND) || 
                (Control->MetricParams.Etx.LossInterval > 1 * MINUTE))
                return STATUS_INVALID_PARAMETER_4;
        }

        if (Control->MetricParams.Etx.Alpha != (uint)-1) {
            //
            // Alpha is interpreted as Alpha/MAXALPHA.
            // Since the result of this division should
            // not exceed 1, the maximum value of Alpha is MAXALPHA.
            //   
            if (Control->MetricParams.Etx.Alpha > MAXALPHA)
                return STATUS_INVALID_PARAMETER_5;
        }

        if (Control->MetricParams.Etx.PenaltyFactor != (uint)-1) {
            //
            // The penalty factor should be at most 32 to avoid
            // overflow problems. 
            // 
            if ((Control->MetricParams.Etx.PenaltyFactor < 1) ||
                (Control->MetricParams.Etx.PenaltyFactor > 32))
                return STATUS_INVALID_PARAMETER_6;
        }
        break;

    case METRIC_TYPE_WCETT:
        if (Control->MetricParams.Wcett.ProbePeriod != (uint)-1) {
            //
            // The probe period should not be less than 100ms. 
            // to prevent system overload. The upper bound is 2^32-1
            // to prevent overflow. Note that (uint)-1 == 2^32-1.
            // 
            if ((Control->MetricParams.Wcett.ProbePeriod < 100 * MILLISECOND) || 
                (Control->MetricParams.Wcett.ProbePeriod > (uint)-1))
                return STATUS_INVALID_PARAMETER_3;
        }

        if (Control->MetricParams.Wcett.LossInterval != (uint)-1) {
            //
            // The loss interval should not be less than 100ms. 
            // to prevent system overload. The upper bound is 1 minute
            // to prevent too much memory consumption.
            // 
            if ((Control->MetricParams.Wcett.LossInterval < 100 * MILLISECOND) || 
                (Control->MetricParams.Wcett.LossInterval > 1 * MINUTE))
                return STATUS_INVALID_PARAMETER_4;
        }

        if (Control->MetricParams.Wcett.Alpha != (uint)-1) {
            //
            // Alpha is interpreted as Alpha/MAXALPHA.
            // Since the result of this division should
            // not exceed 1, the maximum value of Alpha is MAXALPHA.
            //   
            if (Control->MetricParams.Wcett.Alpha > MAXALPHA)
                return STATUS_INVALID_PARAMETER_5;
        }

        if (Control->MetricParams.Wcett.PenaltyFactor != (uint)-1) {
            //
            // The penalty factor should be at most 32 to avoid
            // overflow problems. 
            // 
            if ((Control->MetricParams.Wcett.PenaltyFactor < 1) ||
                (Control->MetricParams.Wcett.PenaltyFactor > 32))
                return STATUS_INVALID_PARAMETER_6;
        }

        if (Control->MetricParams.Wcett.Beta != (uint)-1) {
            //
            // Beta is interpreted as Beta/MAXALPHA.
            // Since the result of this division should
            // not exceed 1, the maximum value of Weight is MAXALPHA.
            //   
            if (Control->MetricParams.Wcett.Beta > MAXALPHA)
                return STATUS_INVALID_PARAMETER_7;
        }

        if (Control->MetricParams.Wcett.PktPairProbePeriod != (uint)-1) {
            //
            // The probe period should not be less than 100ms to
            // prevent system overload.
            // 
            if (Control->MetricParams.Wcett.PktPairProbePeriod < 100 * MILLISECOND)
                return STATUS_INVALID_PARAMETER_8;
        }

        if (Control->MetricParams.Wcett.PktPairMinOverProbes != (uint)-1) {
            //
            // The number of probes should be at least one.
            //
            if (Control->MetricParams.Wcett.PktPairMinOverProbes < 1)
                return STATUS_INVALID_PARAMETER_9;
        }
        break;
    }

    //
    // Now, set the ones that we need to set.
    //

    if (Control->Snooping != (boolint) -1)
        VA->Snooping = Control->Snooping;

    if (Control->ArtificialDrop != (boolint) -1)
        VA->ArtificialDrop = Control->ArtificialDrop;

    if (Control->RouteFlapDampingFactor != (uint) -1)
        VA->RouteFlapDampingFactor = Control->RouteFlapDampingFactor;

    if (Control->Crypto != (boolint) -1)
        VA->Crypto = Control->Crypto;

    if (Control->LinkTimeout != (uint)-1)
        VA->LinkTimeout = (Time)Control->LinkTimeout * SECOND;

    switch (Control->MetricType) {
    case METRIC_TYPE_RTT:
        if (Control->MetricParams.Rtt.Random != (boolint) -1)
            VA->MetricParams.Rtt.Random = Control->MetricParams.Rtt.Random;
        if (Control->MetricParams.Rtt.OutIfOverride != (boolint) -1)
            VA->MetricParams.Rtt.OutIfOverride = Control->MetricParams.Rtt.OutIfOverride;
        if (Control->MetricParams.Rtt.Alpha != (uint)-1)
            VA->MetricParams.Rtt.Alpha = Control->MetricParams.Rtt.Alpha;
        if (Control->MetricParams.Rtt.ProbePeriod != (uint)-1)
            VA->MetricParams.Rtt.ProbePeriod = Control->MetricParams.Rtt.ProbePeriod;
        if (Control->MetricParams.Rtt.HysteresisPeriod != (uint)-1)
            VA->MetricParams.Rtt.HysteresisPeriod = Control->MetricParams.Rtt.HysteresisPeriod;
        if (Control->MetricParams.Rtt.PenaltyFactor != (uint)-1)
            VA->MetricParams.Rtt.PenaltyFactor = Control->MetricParams.Rtt.PenaltyFactor;
        if (Control->MetricParams.Rtt.SweepPeriod != (uint)-1)
            VA->MetricParams.Rtt.SweepPeriod = Control->MetricParams.Rtt.SweepPeriod;
        break;

    case METRIC_TYPE_PKTPAIR:
        if (Control->MetricParams.PktPair.Alpha != (uint)-1)
            VA->MetricParams.PktPair.Alpha = Control->MetricParams.PktPair.Alpha;
        if (Control->MetricParams.PktPair.ProbePeriod != (uint)-1)
            VA->MetricParams.PktPair.ProbePeriod = Control->MetricParams.PktPair.ProbePeriod;
        if (Control->MetricParams.PktPair.PenaltyFactor != (uint)-1)
            VA->MetricParams.PktPair.PenaltyFactor = Control->MetricParams.PktPair.PenaltyFactor;
        break;

    case METRIC_TYPE_ETX:
        if (Control->MetricParams.Etx.ProbePeriod != (uint)-1)
            VA->MetricParams.Etx.ProbePeriod = Control->MetricParams.Etx.ProbePeriod;
        if (Control->MetricParams.Etx.LossInterval != (uint)-1)
            VA->MetricParams.Etx.LossInterval = Control->MetricParams.Etx.LossInterval;
        if (Control->MetricParams.Etx.Alpha != (uint)-1)
            VA->MetricParams.Etx.Alpha = Control->MetricParams.Etx.Alpha;
        if (Control->MetricParams.Etx.PenaltyFactor != (uint)-1)
            VA->MetricParams.Etx.PenaltyFactor = Control->MetricParams.Etx.PenaltyFactor;
        break;

    case METRIC_TYPE_WCETT:
        if (Control->MetricParams.Wcett.ProbePeriod != (uint)-1)
            VA->MetricParams.Wcett.ProbePeriod = Control->MetricParams.Wcett.ProbePeriod;
        if (Control->MetricParams.Wcett.LossInterval != (uint)-1)
            VA->MetricParams.Wcett.LossInterval = Control->MetricParams.Wcett.LossInterval;
        if (Control->MetricParams.Wcett.Alpha != (uint)-1)
            VA->MetricParams.Wcett.Alpha = Control->MetricParams.Wcett.Alpha;
        if (Control->MetricParams.Wcett.PenaltyFactor != (uint)-1)
            VA->MetricParams.Wcett.PenaltyFactor = Control->MetricParams.Wcett.PenaltyFactor;
        if (Control->MetricParams.Wcett.Beta != (uint)-1)
            VA->MetricParams.Wcett.Beta = Control->MetricParams.Wcett.Beta;
        if (Control->MetricParams.Wcett.PktPairProbePeriod != (uint)-1)
            VA->MetricParams.Wcett.PktPairProbePeriod = Control->MetricParams.Wcett.PktPairProbePeriod;
        if (Control->MetricParams.Wcett.PktPairMinOverProbes != (uint)-1)
            VA->MetricParams.Wcett.PktPairMinOverProbes = Control->MetricParams.Wcett.PktPairMinOverProbes;
        break;
    }

    return STATUS_SUCCESS;
}

//* MiniportInitialize
//
//  Called by NDIS to initialize an adapter.
//
NDIS_STATUS
MiniportInitialize(
    OUT PNDIS_STATUS OpenErrorStatus,
    OUT PUINT SelectedMediumIndex,
    IN PNDIS_MEDIUM MediumArray,
    IN UINT MediumArraySize,
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_HANDLE WrapperConfigurationContext)
{
    MCL_CONTROL_VIRTUAL_ADAPTER Control;
    MiniportAdapter *VA;
    HANDLE KeyHandle;
    LARGE_INTEGER Timeout;
    KIRQL OldIrql;
    NDIS_STATUS Status;
    NTSTATUS NtStatus;
    uint i;

    UNREFERENCED_PARAMETER(OpenErrorStatus);
    UNREFERENCED_PARAMETER(WrapperConfigurationContext);

    //
    // We emulate 802.3. Figure out which index that is.
    //
    for (i = 0; ; i++) {
        if (i == MediumArraySize) {
            Status = NDIS_STATUS_UNSUPPORTED_MEDIA;
            goto ErrorReturn;
        }

        if (MediumArray[i] == NdisMedium802_3) {
            *SelectedMediumIndex = i;
            break;
        }
    }

    //
    // Allocate our virtual adapter structure.
    //
    VA = (MiniportAdapter *) ExAllocatePool(NonPagedPool, sizeof *VA);
    if (VA == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto ErrorReturn;
    }

    KdPrint(("MCL!MiniportInitialize -> VA %p\n", VA));

    RtlZeroMemory(VA, sizeof *VA);
    VA->Index = InterlockedIncrement((PLONG)&NextVirtualAdapterIndex);
    KeInitializeSpinLock(&VA->Lock);
    NeighborCacheInit(&VA->NC);
    PbackInit(VA);
    VA->MiniportHandle = MiniportAdapterContext;

    VA->Snooping = TRUE;
    VA->ArtificialDrop = FALSE;
    VA->RouteFlapDampingFactor = DEFAULT_ROUTE_FLAP_DAMPING_FACTOR;
    VA->LinkTimeout = (Time)1 * MINUTE;

    VA->LookAhead = 0;
    VA->MediumLinkSpeed = 100000; // 10 Mbps.
    VA->MediumMinPacketLen = MINIPORT_HEADER_SIZE;
    VA->MediumMaxPacketLen = MINIPORT_HEADER_SIZE + MINIPORT_MAX_FRAME_SIZE;
    VA->MediumMacHeaderLen = MINIPORT_HEADER_SIZE;
    VA->MediumMaxFrameLen = MINIPORT_MAX_FRAME_SIZE;

    VA->MinVersionSeen = Version;
    VA->MaxVersionSeen = Version;

    //
    // Get our device objects.
    //
    NdisMGetDeviceProperty(MiniportAdapterContext,
                           &VA->PhysicalDeviceObject,
                           &VA->DeviceObject,
                           NULL, NULL, NULL);

    //
    // Allocate a packet pool.
    //
    NdisAllocatePacketPool(&Status, &VA->PacketPool,
                           PACKET_POOL_SZ, sizeof(PacketContext));
    if (Status != NDIS_STATUS_SUCCESS) {
        KdPrint(("MCL!NdisAllocatePacketPool -> %x\n", Status));
        goto ErrorFreePool;
    }

    //
    // Allocate a buffer pool.
    //
    NdisAllocateBufferPool(&Status, &VA->BufferPool, PACKET_POOL_SZ);
    if (Status != NDIS_STATUS_SUCCESS) {
        KdPrint(("MCL!NdisAllocateBufferPool -> %x\n", Status));
        goto ErrorFreePacketPool;
    }

    //
    // Give NDIS some information about our virtual adapter.
    //
    NdisMSetAttributesEx(MiniportAdapterContext, VA, 0,
                         NDIS_ATTRIBUTE_IGNORE_TOKEN_RING_ERRORS |
                         NDIS_ATTRIBUTE_IGNORE_PACKET_TIMEOUT |
                         NDIS_ATTRIBUTE_IGNORE_REQUEST_TIMEOUT |
                         NDIS_ATTRIBUTE_NO_HALT_ON_SUSPEND |
                         NDIS_ATTRIBUTE_SURPRISE_REMOVE_OK |
                         NDIS_ATTRIBUTE_USES_SAFE_BUFFER_APIS |
                         NDIS_ATTRIBUTE_DESERIALIZE,
                         0);

    //
    // Open our configuration information in the registry.
    //
    NtStatus = IoOpenDeviceRegistryKey(VA->PhysicalDeviceObject,
                                       PLUGPLAY_REGKEY_DRIVER,
                                       GENERIC_READ,
                                       &KeyHandle);
    if (! NT_SUCCESS(NtStatus)) {
        KdPrint(("MCL!IoOpenDeviceRegistryKey -> %x\n", NtStatus));
        Status = NDIS_STATUS_FAILURE;
        goto ErrorFreeBufferPool;
    }

    //
    // Read the adapter's virtual link-layer address.
    //
    Status = MiniportReadOrCreateAdapterAddress(KeyHandle, VA->Address);
    if (Status != NDIS_STATUS_SUCCESS)
        goto ErrorCloseRegistry;

    KdPrint(("MCL!MiniportInitialize(VA %p): "
             "addr %02x-%02x-%02x-%02x-%02x-%02x\n", VA,
             VA->Address[0], VA->Address[1], VA->Address[2],
             VA->Address[3], VA->Address[4], VA->Address[5]));

    //
    // Read the adapter's guid.
    //
    NtStatus = GetRegGuid(KeyHandle, L"NetCfgInstanceId", &VA->Guid);
    if (! NT_SUCCESS(NtStatus)) {
        KdPrint(("MCL!GetRegGuid -> %x\n", NtStatus));
        Status = NDIS_STATUS_FAILURE;
        goto ErrorCloseRegistry;
    }

    ZwClose(KeyHandle);

    //
    // Read configuration parameters from the registry.
    //
    MiniportReadControl(VA, &Control);

    // 
    // Initialize Metric parameters.
    // The default MetricType is WCETT.
    //
    switch (Control.MetricType) {
    case METRIC_TYPE_HOP:
        VA->MetricType = METRIC_TYPE_HOP;
        VA->IsInfinite = MiniportIsInfinite;
        VA->ConvMetric = MiniportConvMetric;
        VA->PathMetric = MiniportPathMetric;
        VA->InitLinkMetric = MiniportInitLinkMetric;
        break;
    case METRIC_TYPE_RTT:
        VA->MetricType = METRIC_TYPE_RTT;
        RttInit(VA);
        break;
    case METRIC_TYPE_PKTPAIR:
        VA->MetricType = METRIC_TYPE_PKTPAIR;
        PktPairInit(VA);
        break;
    case METRIC_TYPE_ETX:
        VA->MetricType = METRIC_TYPE_ETX;
        EtxInit(VA);
        break;
    case METRIC_TYPE_WCETT:
    default:
        VA->MetricType = METRIC_TYPE_WCETT;
        WcettInit(VA);
        break;
    }

    if (LQSR_KEY_PRESENT(Control.CryptoKeyMAC) &&
        LQSR_KEY_PRESENT(Control.CryptoKeyAES)) {
        //
        // Initialize the crypto parameters.
        // Crypto will be enabled, unless MiniportControl overrides.
        //
        VA->Crypto = TRUE;
        RtlCopyMemory(VA->CryptoKeyMAC, Control.CryptoKeyMAC,
                      sizeof VA->CryptoKeyMAC);
        RtlCopyMemory(VA->CryptoKeyAES, Control.CryptoKeyAES,
                      sizeof VA->CryptoKeyAES);
    }

    //
    // Modify our MAC key, using Version and MetricType.
    // If they disagree then nodes should fail to communicate.
    //
    CryptoKeyMACModify(VA, VA->CryptoKeyMAC);

    //
    // Update the virtual adapter using the parameters.
    // This can modify VA->Crypto but not MetricType,
    // CryptoKeyMAC, or CryptoKeyAES.
    //
    (void) MiniportControl(VA, &Control);

    //
    // Now we have our address, so we can set up our link cache.
    //
    VA->LC = LinkCacheAllocate(4, VA->Address);
    if (VA->LC == NULL)
        goto ErrorFreeBufferPool;

    //
    // Set up our request table.
    //
    Status = ReqTableNew(VA, 16);
    if (Status != NDIS_STATUS_SUCCESS)
        goto ErrorFreeLinkCache;

    //
    // Set up our send buffer.
    //
    Status = SendBufNew(VA, SEND_BUF_SZ);
    if (Status != NDIS_STATUS_SUCCESS)
        goto ErrorFreeRequestTable;

    //
    // Set up our maintenance buffer.
    //
    VA->MaintBuf = MaintBufNew();
    if (VA->MaintBuf == NULL)
        goto ErrorFreeSendBuffer;

    //
    // Set up our forwarding queue.
    //
    VA->ForwardLast = &VA->ForwardList;
    ASSERT(VA->ForwardList == NULL);
    ASSERT(VA->ForwardTime == 0);

    //
    // Initialize with NDIS as a protocol. This will bind
    // our virtual adapter to physical adapters,
    // based on the protocol name that we supply here.
    //
    Status = ProtocolInit(L"msmcltp", VA);
    if (Status != NDIS_STATUS_SUCCESS)
        goto ErrorFreeMaintBuffer;

    //
    // We have an aperiodic timeout function,
    // with a maximum timeout of MINIPORT_TIMEOUT.
    //
    VA->TimeoutMinLoops = MAXULONG;
    KeInitializeDpc(&VA->TimeoutDpc, MiniportTimeout, VA);
    KeInitializeTimer(&VA->Timer);
    Timeout.QuadPart = - (LONGLONG) (MINIPORT_TIMEOUT * 10000);
    VA->Timeout = KeQueryInterruptTime() - Timeout.QuadPart;
    KeSetTimer(&VA->Timer, Timeout, &VA->TimeoutDpc);

    //
    // Add the virtual adapter to our list.
    //
    KeAcquireSpinLock(&MiniportAdapters.Lock, &OldIrql);
    VA->Prev = &MiniportAdapters.VirtualAdapters;
    VA->Next = MiniportAdapters.VirtualAdapters;
    if (MiniportAdapters.VirtualAdapters != NULL)
        MiniportAdapters.VirtualAdapters->Prev = &VA->Next;
    MiniportAdapters.VirtualAdapters = VA;
    KeReleaseSpinLock(&MiniportAdapters.Lock, OldIrql);
   
    return NDIS_STATUS_SUCCESS;

ErrorCloseRegistry:
    ZwClose(KeyHandle);
    goto ErrorFreeBufferPool;

ErrorFreeMaintBuffer:
    MaintBufFree(VA);
ErrorFreeSendBuffer:
    SendBufFree(VA);
ErrorFreeRequestTable:
    ReqTableFree(VA);
ErrorFreeLinkCache:
    LinkCacheFree(VA);
ErrorFreeBufferPool:
    NdisFreeBufferPool(VA->BufferPool);
ErrorFreePacketPool:
    NdisFreePacketPool(VA->PacketPool);
ErrorFreePool:
    ExFreePool(VA);
ErrorReturn:
    KdPrint(("MCL!MiniportInitialize -> %x\n", Status));
    return Status;
}

NDIS_OID MiniportSupportedOidArray[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAXIMUM_SEND_PACKETS,

    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,           // Set only?
    OID_802_3_MAXIMUM_LIST_SIZE,

    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,

    OID_PNP_CAPABILITIES,
    OID_PNP_SET_POWER,                  // Set only.
    OID_PNP_QUERY_POWER,
};

uchar MiniportVendorDescription[] = "MCL Miniport Driver";
uchar MiniportVendorId[3] = {0xFF, 0xFF, 0xFF};

NDIS_PNP_CAPABILITIES MiniportPnpCapabilities = {
    0, 
    NdisDeviceStateUnspecified,
    NdisDeviceStateUnspecified,
    NdisDeviceStateUnspecified
};

//* MiniportQueryInformation
//
//  Called by NDIS to retrieve information about our virtual adapter.
//
NDIS_STATUS
MiniportQueryInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesWritten,
    OUT PULONG BytesNeeded)
{
    MiniportAdapter *VA = (MiniportAdapter *) MiniportAdapterContext;
    uint GenericUint;
    ushort GenericUshort;
    void *Data;
    uint DataLength;

    //
    // Prepare to return a uint - this is the common case.
    //
    Data = &GenericUint;
    DataLength = sizeof GenericUint;

    //
    // If you change this switch, be sure to update MiniportSupportedOidArray.
    //
    switch (Oid) {
    case OID_GEN_SUPPORTED_LIST:
        Data = MiniportSupportedOidArray;
        DataLength = sizeof MiniportSupportedOidArray;
        break;

    case OID_GEN_HARDWARE_STATUS:
        GenericUint = NdisHardwareStatusReady;
        break;

    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        GenericUint = VA->Medium;
        break;

    case OID_GEN_MAXIMUM_LOOKAHEAD:
        GenericUint = MINIPORT_MAX_LOOKAHEAD;
        break;

    case OID_GEN_CURRENT_LOOKAHEAD:
        GenericUint = VA->LookAhead;
        break;

    case OID_GEN_MAXIMUM_FRAME_SIZE:
        GenericUint = VA->MediumMaxFrameLen;
        break;

    case OID_GEN_MAC_OPTIONS:
        GenericUint = NDIS_MAC_OPTION_NO_LOOPBACK;
        break;

    case OID_GEN_LINK_SPEED:
        GenericUint = VA->MediumLinkSpeed;
        break;

    case OID_GEN_TRANSMIT_BUFFER_SPACE:
    case OID_GEN_RECEIVE_BUFFER_SPACE:
    case OID_GEN_MAXIMUM_TOTAL_SIZE:
        GenericUint = VA->MediumMaxPacketLen;
        break;

    case OID_GEN_TRANSMIT_BLOCK_SIZE:
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        GenericUint = 1;
        break;

    case OID_GEN_VENDOR_ID:
        Data = MiniportVendorId;
        DataLength = sizeof MiniportVendorId;
        break;

    case OID_GEN_VENDOR_DESCRIPTION:
        Data = MiniportVendorDescription;
        DataLength = sizeof MiniportVendorDescription;
        break;

    case OID_GEN_CURRENT_PACKET_FILTER:
        GenericUint = VA->PacketFilter;
        break;

    case OID_GEN_DRIVER_VERSION:
        GenericUshort = (NDIS_MINIPORT_MAJOR_VERSION << 8) + NDIS_MINIPORT_MINOR_VERSION;
        Data = &GenericUshort;
        DataLength = sizeof GenericUshort;
        break;

    case OID_GEN_MEDIA_CONNECT_STATUS:
        if (VA->PhysicalAdapters != NULL)
            GenericUint = NdisMediaStateConnected;
        else
            GenericUint = NdisMediaStateDisconnected;
        break;

    case OID_GEN_MAXIMUM_SEND_PACKETS:
        GenericUint = MAX_RECV_QUEUE_SIZE;
        break;

    case OID_802_3_PERMANENT_ADDRESS:
    case OID_802_3_CURRENT_ADDRESS:
        Data = VA->Address;
        DataLength = sizeof VA->Address;
        break;

    case OID_802_3_MAXIMUM_LIST_SIZE:
        GenericUint = MINIPORT_MAX_MULTICAST_ADDRESS;
        break;

    case OID_802_3_RCV_ERROR_ALIGNMENT:
    case OID_802_3_XMIT_ONE_COLLISION:
    case OID_802_3_XMIT_MORE_COLLISIONS:
        GenericUint = 0;
        break;

    case OID_PNP_CAPABILITIES:
        Data = &MiniportPnpCapabilities;
        DataLength = sizeof MiniportPnpCapabilities;
        break;

    case OID_PNP_QUERY_POWER:
        DataLength = 0;
        break;

    default:
        KdPrint(("MCL!MiniportQueryInformation(VA %p OID %x)\n", VA, Oid));

        //
        // The following OIDs are called in practice,
        // but we do not support them.
        //
    case OID_GEN_VENDOR_DRIVER_VERSION:
    case OID_GEN_SUPPORTED_GUIDS:
    case OID_GEN_PHYSICAL_MEDIUM:
    case OID_GEN_MEDIA_CAPABILITIES:
    case OID_FFP_SUPPORT:
    case OID_TCP_TASK_OFFLOAD:
    case 0xff54554e: // OID_CUSTOM_TUNMP_INSTANCE_ID
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    *BytesNeeded = DataLength;
    if (DataLength > InformationBufferLength)
        return NDIS_STATUS_BUFFER_TOO_SHORT;

    RtlCopyMemory(InformationBuffer, Data, DataLength);
    *BytesWritten = DataLength;
    return NDIS_STATUS_SUCCESS;
}

//* MiniportSetInformation
//
//  Called by NDIS to specify information for our virtual adapter.
//
NDIS_STATUS
MiniportSetInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesRead,
    OUT PULONG BytesNeeded)
{
    MiniportAdapter *VA = (MiniportAdapter *) MiniportAdapterContext;

    switch (Oid) {
    case OID_GEN_CURRENT_LOOKAHEAD: {
        uint LookAhead;

        if (InformationBufferLength != sizeof LookAhead) {
            *BytesNeeded = sizeof LookAhead;
            return NDIS_STATUS_INVALID_LENGTH;
        }

        LookAhead = * (uint UNALIGNED *) InformationBuffer;
        *BytesRead = sizeof LookAhead;

        if (LookAhead > MINIPORT_MAX_LOOKAHEAD)
            return NDIS_STATUS_INVALID_DATA;

        if (LookAhead > VA->LookAhead)
            VA->LookAhead = LookAhead;

        return NDIS_STATUS_SUCCESS;
    }

    case OID_PNP_SET_POWER: {
        NDIS_DEVICE_POWER_STATE PowerState;

        if (InformationBufferLength != sizeof PowerState) {
            *BytesNeeded = sizeof PowerState;
            return NDIS_STATUS_INVALID_LENGTH;
        }

        PowerState = * (NDIS_DEVICE_POWER_STATE UNALIGNED *) InformationBuffer;
        *BytesRead = sizeof PowerState;

        KdPrint(("MCL!MiniportSetInformation(VA %p SetPower %x)\n",
                 VA, PowerState));

        //
        // Should we pend until any outstanding operations are completed,
        // when transitioning to a sleep state?
        //

        if (PowerState == NdisDeviceStateD0) {
            //
            // We are waking up after being asleep.
            // The environment has likely changed, so flush our link cache.
            // The neighbor cache does not need to be flushed.
            //
            LinkCacheFlush(VA);
        }

        return NDIS_STATUS_SUCCESS;
    }

    case OID_802_3_MULTICAST_LIST:
    case OID_GEN_CURRENT_PACKET_FILTER:
        *BytesRead = InformationBufferLength;
        return NDIS_STATUS_SUCCESS;

    default:
        KdPrint(("MCL!MiniportSetInformation(VA %p OID %x)\n", VA, Oid));

        //
        // The following OIDs are called in practice,
        // but we do not support them.
        //
    case OID_GEN_TRANSPORT_HEADER_OFFSET:
    case OID_GEN_NETWORK_LAYER_ADDRESSES:
    case OID_PNP_ADD_WAKE_UP_PATTERN:
    case OID_PNP_REMOVE_WAKE_UP_PATTERN:
        return NDIS_STATUS_NOT_SUPPORTED;
    }
}

//* ForwardRequestP
//
//  Should we forward this Route Request?
//  Returns TRUE if we should.
//
static boolint
ForwardRequestP(
    MiniportAdapter *VA,
    SRPacket *srp)
{
    InternalRouteRequest *RR;
    uint Hops;
    uint i;

    RR = srp->req;
    ASSERT(RR != NULL);

    //
    // Is the path too long? If so we can not forward.
    //
    Hops = ROUTE_REQUEST_HOPS(RR->opt.optDataLen);
    ASSERT(Hops != 0);
    if (Hops >= MAX_SR_LEN)
        return FALSE;

    //
    // Make sure we're not on the path yet.
    // If so we can not forward.
    //
    for (i = 0; i < Hops; i++)
        if (VirtualAddressEqual(RR->opt.hopList[i].addr, VA->Address))
            return FALSE;

    //
    // Check if we've forwarded this request id before.
    // If so we should not forward.
    //
    if (ReqTableSuppress(VA,
                         RR->opt.hopList[0].addr,
                         RR->opt.targetAddress,
                         RR->opt.identification))
        return FALSE;

    return TRUE;
}

//* MiniportControlSendComplete
//
//  Completes the transmission of a Source-Routed control packet,
//  such as a Route Request, Route Reply or Route Error.
//
static void
MiniportControlSendComplete(
    MiniportAdapter *VA,
    SRPacket *srp,
    NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(VA);
    UNREFERENCED_PARAMETER(Status);

    SRPacketFree(srp);
}

//* MiniportSendRouteReplyFromSR
//
//  Sends a Route Reply in response to a Source Route,
//  to propogate link metric information back to the originator.
//
//  Does not consume srp or srp->Packet.
//
static void
MiniportSendRouteReplyFromSR(
    MiniportAdapter *VA,
    const SRPacket *SRP)
{
    InternalRouteReply *RR;
    uint Hops;

    ASSERT(SRP->sr != NULL);
    Hops = SOURCE_ROUTE_HOPS(SRP->sr->opt.optDataLen);
    ASSERT(VirtualAddressEqual(SRP->sr->opt.hopList[Hops - 1].addr,
                               VA->Address));

    //
    // Allocate the Route Reply option.
    //
    RR = ExAllocatePool(NonPagedPool, sizeof *RR + sizeof(SRAddr)*MAX_SR_LEN);
    if (RR == NULL)
        return;

    //
    // Initialize the Route Reply option.
    //
    RtlZeroMemory(RR, sizeof *RR + sizeof(SRAddr)*MAX_SR_LEN);
    RR->opt.optionType = LQSR_OPTION_TYPE_REPLY;
    RR->opt.optDataLen = ROUTE_REPLY_LEN(Hops);

    RtlCopyMemory(RR->opt.hopList, SRP->sr->opt.hopList,
                  Hops * sizeof(SRAddr));

    //
    // Send the Route Reply option.
    // PbackSendOption will squash duplicate Route Replies.
    //
    PbackSendOption(VA, SRP->sr->opt.hopList[0].addr,
                    (InternalOption *) RR,
                    PBACK_ROUTE_REPLY_SR_TIMEOUT);
}

//* MiniportSendRouteReply
//
//  Sends a Route Reply in response to a Route Request.
//
//  Does not consume srp or srp->Packet.
//
static void
MiniportSendRouteReply(
    MiniportAdapter *VA,
    const SRPacket *SRP)
{
    InternalRouteReply *RR;
    uint Hops;

    InterlockedIncrement((PLONG)&VA->CountXmitRouteReply);

    ASSERT(SRP->req != NULL);
    ASSERT(VirtualAddressEqual(SRP->req->opt.targetAddress, VA->Address));
    Hops = ROUTE_REQUEST_HOPS(SRP->req->opt.optDataLen);
    ASSERT(VirtualAddressEqual(SRP->req->opt.hopList[Hops - 1].addr,
                               VA->Address));
    SRP->req->opt.hopList[Hops - 1].outif = 0;

    //
    // Allocate the Route Reply option.
    //
    RR = ExAllocatePool(NonPagedPool, sizeof *RR + sizeof(SRAddr)*MAX_SR_LEN);
    if (RR == NULL)
        return;

    //
    // Initialize the Route Reply option.
    //
    RtlZeroMemory(RR, sizeof *RR + sizeof(SRAddr)*MAX_SR_LEN);
    RR->opt.optionType = LQSR_OPTION_TYPE_REPLY;
    RR->opt.optDataLen = ROUTE_REPLY_LEN(Hops);

    RtlCopyMemory(RR->opt.hopList, SRP->req->opt.hopList,
                  Hops * sizeof(SRAddr));

    //
    // Send the Route Reply option.
    //
    PbackSendOption(VA, SRP->req->opt.hopList[0].addr,
                    (InternalOption *) RR,
                    PBACK_ROUTE_REPLY_TIMEOUT);
}

//* MiniportForwardCleanup
//
//  Cleans up the forwarding queue.
//
static void
MiniportForwardCleanup(MiniportAdapter *VA)
{
    SRPacket *SRP;

    while ((SRP = VA->ForwardList) != NULL) {
        VA->ForwardList = SRP->Next;

        (*SRP->TransmitComplete)(VA, SRP, NDIS_STATUS_FAILURE);
    }
}

//* MiniportForwardRequest
//
//  Implements a rate-limiting queue in front of ProtocolForwardRequest.
//
static void
MiniportForwardRequest(
    MiniportAdapter *VA,
    SRPacket *srp)
{
    Time Now;
    KIRQL OldIrql;

    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    Now = KeQueryInterruptTime();

    //
    // Can we forward this packet now, or must we queue it?
    //
    if (Now < VA->ForwardTime) {
        if (VA->ForwardNum >= MAX_FORWARD_QUEUE) {
            //
            // The queue is too long, so drop this packet.
            //
            VA->CountForwardDrop++;
            KeReleaseSpinLock(&VA->Lock, OldIrql);

            (*srp->TransmitComplete)(VA, srp, NDIS_STATUS_FAILURE);
        }
        else {
            //
            // Queue the packet.
            //
            VA->CountForwardQueue++;
            *VA->ForwardLast = srp;
            VA->ForwardLast = &srp->Next;
            ASSERT(srp->Next == NULL);

            if (++VA->ForwardNum > VA->ForwardMax)
                VA->ForwardMax = VA->ForwardNum;

            //
            // Possibly reschedule MiniportTimeout to run earlier,
            // so the waiting packet is sent asap.
            //
            if (VA->ForwardTime < VA->Timeout)
                MiniportRescheduleTimeoutHelper(VA, Now, VA->ForwardTime);

            KeReleaseSpinLock(&VA->Lock, OldIrql);
        }
    }
    else {
        //
        // We will forward it now.
        // Calculate time at which we can next forward.
        //
        VA->CountForwardFast++;
        VA->ForwardTime = Now + MIN_BROADCAST_GAP;
        KeReleaseSpinLock(&VA->Lock, OldIrql);

        ProtocolForwardRequest(VA, srp);
    }
}

//* MiniportForwardDelayedRequests
//
//  Checks if we can forward any queued requests.
//
static void
MiniportForwardDelayedRequests(
    MiniportAdapter *VA,
    Time Now)
{
    SRPacket *srp;
    KIRQL OldIrql;

    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    if ((VA->ForwardList != NULL) &&
        (VA->ForwardTime <= Now)) {
        //
        // Dequeue the packet.
        //
        srp = VA->ForwardList;
        VA->ForwardList = srp->Next;
        if (VA->ForwardList == NULL)
            VA->ForwardLast = &VA->ForwardList;
        VA->ForwardNum--;

        //
        // Calculate time at which we can next forward.
        // We can update VA->Timeout directly instead of
        // using MiniportRescheduleTimeoutHelper because we are
        // called from MiniportTimeout.
        //
        VA->ForwardTime = Now + MIN_BROADCAST_GAP;
        if (VA->ForwardTime < VA->Timeout)
            VA->Timeout = VA->ForwardTime;
    }
    else {
        //
        // No packet to forward.
        //
        srp = NULL;
    }
    KeReleaseSpinLock(&VA->Lock, OldIrql);

    if (srp != NULL) {
        //
        // Forward the waiting packet.
        //
        ProtocolForwardRequest(VA, srp);
    }
}

//* MiniportGenerateRouteRequest
//
//  Generates a Route Request.
//
//  Does NOT consume srp or srp->Packet,
//  that is the responsibility of the completion function.
//
static void
MiniportGenerateRouteRequest(
    MiniportAdapter *VA,
    SRPacket *srp,
    const VirtualAddress Target,
    LQSRReqId Identifier,
    void (*Complete)(MiniportAdapter *VA, SRPacket *srp, NDIS_STATUS Status))
{
    InternalRouteRequest *RR;

    ASSERT((srp->req == NULL) && (srp->sr == NULL));

    //
    // Create and initialize a Route Request option.
    //

    RR = ExAllocatePool(NonPagedPool, sizeof *RR + sizeof(SRAddr)*MAX_SR_LEN);
    if (RR == NULL) {
        (*Complete)(VA, srp, NDIS_STATUS_RESOURCES);
        return;
    }

    srp->req = RR;
    RtlZeroMemory(RR, sizeof *RR + sizeof(SRAddr)*MAX_SR_LEN);
    RR->opt.optionType = LQSR_OPTION_TYPE_REQUEST;
    RR->opt.optDataLen = ROUTE_REQUEST_LEN(1);

    RR->opt.identification = Identifier;
    RtlCopyMemory(RR->opt.targetAddress, Target, SR_ADDR_LEN);

    RtlCopyMemory(RR->opt.hopList[0].addr, VA->Address, SR_ADDR_LEN);
    ASSERT(RR->opt.hopList[0].inif == 0);
    // RR->opt.hopList[0].outif initialized in ProtocolForwardRequest.

    if (VA->MetricType != METRIC_TYPE_HOP) {
        uint Size;

        //
        // Add a Link Info option to the packet.
        // We give LinkCacheCreateLI an upper bound for the option size.
        //
        Size = PbackPacketSize(srp);
        ASSERT(Size <= PROTOCOL_MIN_FRAME_SIZE);
        srp->LinkInfo = LinkCacheCreateLI(VA, PROTOCOL_MIN_FRAME_SIZE - Size);
        InterlockedWrite64((LONGLONG *)&VA->LastLinkInfo,
                           KeQueryInterruptTime());
    }

    //
    // Send the packet.
    //
    srp->TransmitComplete = Complete;
    MiniportForwardRequest(VA, srp);
}

//* MiniportSendViaRouteRequest
//
void
MiniportSendViaRouteRequest(
    MiniportAdapter *VA,
    SRPacket *SRP,
    void (*Complete)(MiniportAdapter *VA, SRPacket *SRP, NDIS_STATUS Status))
{
    LQSRReqId Identifier;

    Identifier = ReqTableIdentifier(VA, SRP->Dest);
    MiniportGenerateRouteRequest(VA, SRP,
                                 SRP->Dest, Identifier,
                                 Complete);
}

//* MiniportSendComplete
//
//  Called when a MiniportSendPacket operation has finished
//  and the packet must be returned to NDIS.
//
void
MiniportSendComplete(
    MiniportAdapter *VA,
    SRPacket *srp,
    NDIS_STATUS Status)
{
    NDIS_PACKET *Packet = srp->Packet;

    UNREFERENCED_PARAMETER(Status);
    
    //
    // Return the original clear-text packet back to NDIS.
    //
    NdisMSendComplete(VA->MiniportHandle,
                      PC(Packet)->OrigPacket,
                      srp->ForwardStatus);

    //
    // This also frees Packet.
    //
    SRPacketFree(srp);
}

//* MiniportFreePacket
//
//  Helper function for MiniportTransmitPacket.
//
static void
MiniportFreePacket(
    ProtocolAdapter *PA,
    NDIS_PACKET *Packet)
{
    UNREFERENCED_PARAMETER(PA);
    NdisFreePacketClone(Packet);
}

//* MiniportTransmitPacket
//
//  Send an LQSR packet via our virtual link.
//
void
MiniportTransmitPacket(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet)
{
    SRPacket *srp;
    EtherHeader UNALIGNED *Ether;
    NDIS_STATUS Status;

    InterlockedIncrement((PLONG)&VA->CountXmitLocally);

    //
    // Allocate an SRPacket structure for this packet.
    //
    srp = ExAllocatePool(NonPagedPool, sizeof *srp);
    if (srp == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto ErrorReturn;
    }

    RtlZeroMemory(srp, sizeof *srp);

    //
    // Encrypt the packet payload (including the EType).
    // This generates an IV.
    //
    Status = CryptoEncryptPacket(VA, Packet,
                                 sizeof(EtherHeader) - sizeof(ushort),
                                 srp->IV, &srp->Packet);
    if (Status != NDIS_STATUS_SUCCESS)
        goto ErrorFreeSRP;

    //
    // MiniportFreePacket will free the encrypted packet
    // using NdisFreePacketClone.
    //
    srp->PA = NULL;
    srp->FreePacket = MiniportFreePacket;

    //
    // This is a dummy value, so srp->PacketLength vs srp->PayloadOffset
    // comparisons will work properly. We do not need the actual
    // length of srp->Packet.
    //
    srp->PacketLength = 1;

    //
    // MiniportSendComplete will complete the original packet.
    //
    PC(srp->Packet)->OrigPacket = Packet;

    //
    // Initialize the virtual source & destination addresses
    // in the SRPacket. If our caller supplies a bogus
    // source address, we ignore it. This can happen,
    // for example when the IPv4 stack detects a duplicate address
    // it sends an ARP with the "wrong" source address.
    //
    Ether = NdisFirstBuffer(Packet)->MappedSystemVa;
    RtlCopyMemory(srp->Dest, Ether->Dest, SR_ADDR_LEN);
    RtlCopyMemory(srp->Source, VA->Address, SR_ADDR_LEN);

    if (IEEE_802_ADDR_IS_BROADCAST(srp->Dest)) {
        //
        // We piggy-back multicast/broadcast packets on a Route Request.
        //
        InterlockedIncrement((PLONG)&VA->CountXmitMulticast);
        MiniportSendViaRouteRequest(VA, srp, MiniportSendComplete);
    }
    else {
        InternalSourceRoute *SR;

        SR = ExAllocatePool(NonPagedPool,
                            sizeof *SR + sizeof(SRAddr)*MAX_SR_LEN);
        if (SR == NULL) {
            Status = NDIS_STATUS_RESOURCES;
            goto ErrorFreeSRP;
        }

        srp->sr = SR;

        //
        // See if we can find a route in the link cache. 
        //
        Status = LinkCacheFillSR(VA, Ether->Dest, SR);
        if (Status != NDIS_STATUS_SUCCESS) {
            if (Status == NDIS_STATUS_NO_ROUTE_TO_DESTINATION) {
                //
                // Didn't find a route. Stick this packet in the send buffer.
                //
                InterlockedIncrement((PLONG)&VA->CountXmitSendBuf);
                SendBufInsert(VA, srp);
                return;
            }
            //
            // Looks like we didn't have enough memory to run dijkstra().
            // Drop this packet on the floor.
            //
            ASSERT(Status == NDIS_STATUS_RESOURCES);
            goto ErrorFreeSRP;
        }

        if (! LinkCacheUseSR(VA, srp)) {
            InterlockedIncrement((PLONG)&VA->CountXmitQueueFull);
            goto ErrorFreeSRP;
        }

        MaintBufSendPacket(VA, srp, MiniportSendComplete);
    }

    return;

ErrorFreeSRP:
    SRPacketFree(srp);

ErrorReturn:
    NdisMSendComplete(VA->MiniportHandle, Packet, Status);
}

//* MiniportTransmitPackets
//
//  Called by NDIS to send packets via our virtual adapter.
//
void
MiniportTransmitPackets(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PPNDIS_PACKET PacketArray,
    IN UINT NumberOfPackets)
{
    MiniportAdapter *VA = (MiniportAdapter *) MiniportAdapterContext;
    uint i;

    for (i = 0; i < NumberOfPackets; i++)
        MiniportTransmitPacket(VA, PacketArray[i]);
}

//* ArtificialDrop
//
//  Checks to see if the link on which this packet arrived has been flagged
//  for extra packet drops.  If so, determines whether or not to drop this
//  packet.  Return TRUE if packet should be dropped, FALSE otherwise.
//
boolint
ArtificialDrop(
    MiniportAdapter *VA,
    const SRPacket *srp)
{
    //
    // Check that ArtificialDrop is enabled.  Also, we only consider
    // (non-static) source routed packets as candidates for dropping.
    //
    if ((VA->ArtificialDrop) &&
        (srp->sr != NULL) && !srp->sr->opt.staticRoute) {
        uint Index;

        Index = SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen) -
            srp->sr->opt.segmentsLeft;

        return LinkCacheCheckForDrop(VA,
                                     srp->sr->opt.hopList[Index-1].addr,
                                     srp->sr->opt.hopList[Index].addr,
                                     srp->sr->opt.hopList[Index].inif,
                                     srp->sr->opt.hopList[Index-1].outif);
    }

    return FALSE;
}


//* CacheSRPacket
//
//  Updates the link cache and neighbor cache with information
//  from a newly-received packet.
//
//  Also calls SendBufCheck, to send any packets that have been
//  waiting for a route.
//
void
CacheSRPacket(
    MiniportAdapter *VA,
    ProtocolAdapter *PA,
    const SRPacket *srp)
{
    InternalRouteReply *Rep;
    InternalRouteError *Err;
    InternalLinkInfo *LI;
    boolint LinkAdded = FALSE;
    uint Metric;
    uint i;

    //
    // We must process Link Info before Route Request options.
    // If the Link Info mistakenly says that a link from A -> B
    // is broken, and B receives the Route Request / Link Info
    // from A, then we need to ensure that after CacheSRPacket
    // B's link cache says the A -> B link exists and that B then
    // puts that metric into the Route Request that it forwards,
    // so A will overhear this and update its link cache.
    //
    for (LI = srp->LinkInfo; LI != NULL; LI = LI->Next) {
        //
        // If this link information originated from ourselves,
        // then don't update our link cache based upon it.
        // It is stale information. Furthermore it would reset
        // the link timestamps, preventing them from ever timing out.
        //
        if (! VirtualAddressEqual(LI->Opt.From, VA->Address)) {
            for (i = 0; i < LINKINFO_HOPS(LI->Opt.optDataLen); i++) {
                LinkCacheAddLink(VA,
                                 LI->Opt.From,
                                 LI->Opt.Links[i].addr,
                                 LI->Opt.Links[i].inif,
                                 LI->Opt.Links[i].outif,
                                 LI->Opt.Links[i].Metric,
                                 LINK_STATE_CHANGE_ADD_LINKINFO);
                LinkAdded = TRUE;
            }
        }
    }

    if (srp->sr != NULL) {
        uint Index;

        for (i = 1; i < SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen); i++) {
            Metric = srp->sr->opt.hopList[i].Metric;
            if ((*VA->IsInfinite)(Metric)) {
                //
                // This shouldn't happen, because we never send infinity
                // in a Source Route.
                //
                KdPrint(("MCL!CacheSRPacket: SR infinite metric\n"));
                Metric = 0;
            }

            //
            // We know the forward link exists.
            //
            LinkCacheAddLink(VA,
                             srp->sr->opt.hopList[i-1].addr,
                             srp->sr->opt.hopList[i].addr,
                             srp->sr->opt.hopList[i].inif,
                             srp->sr->opt.hopList[i-1].outif,
                             Metric,
                             LINK_STATE_CHANGE_ADD_SNOOP_SR);
            LinkAdded = TRUE;
        }

        Index = SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen) -
            srp->sr->opt.segmentsLeft;
        NeighborReceivePassive(&VA->NC,
                               srp->sr->opt.hopList[Index-1].addr,
                               srp->sr->opt.hopList[Index-1].outif,
                               srp->EtherSource);

        if (! srp->sr->opt.staticRoute) {
            //
            // Increment the usage count for this link.
            // REVIEW: Count static-routed packets?
            //
            LinkCacheCountLinkUse(VA,
                                  srp->sr->opt.hopList[Index-1].addr,
                                  srp->sr->opt.hopList[Index].addr,
                                  srp->sr->opt.hopList[Index].inif,
                                  srp->sr->opt.hopList[Index-1].outif);
        }
    }

    if (srp->req != NULL) {
        ASSERT(srp->sr == NULL);

        for (i = 1; i < ROUTE_REQUEST_HOPS(srp->req->opt.optDataLen); i++) {
            Metric = srp->req->opt.hopList[i].Metric;
            if ((*VA->IsInfinite)(Metric)) {
                //
                // This shouldn't happen, because we never send infinity
                // in a Route Request.
                //
                KdPrint(("MCL!CacheSRPacket: Request infinite metric\n"));
                Metric = 0;
            }

            LinkCacheAddLink(VA,
                             srp->req->opt.hopList[i-1].addr,
                             srp->req->opt.hopList[i].addr,
                             srp->req->opt.hopList[i].inif,
                             srp->req->opt.hopList[i-1].outif,
                             Metric,
                             LINK_STATE_CHANGE_ADD_SNOOP_REQUEST);
        }
        LinkCacheAddLink(VA,
                         srp->req->opt.hopList[i-1].addr,
                         VA->Address,
                         (LQSRIf) PA->Index,
                         srp->req->opt.hopList[i-1].outif,
                         0, // Metric.
                         LINK_STATE_CHANGE_ADD_SNOOP_REQUEST);
        LinkAdded = TRUE;

        NeighborReceivePassive(&VA->NC,
                               srp->req->opt.hopList[i-1].addr,
                               srp->req->opt.hopList[i-1].outif,
                               srp->EtherSource);
    }

    for (Rep = srp->rep; Rep != NULL; Rep = Rep->next) {
        uint Reason;

        if (VirtualAddressEqual(Rep->opt.hopList[0].addr, VA->Address))
            Reason = LINK_STATE_CHANGE_ADD_REPLY;
        else
            Reason = LINK_STATE_CHANGE_ADD_SNOOP_REPLY;

        for (i = 1; i < ROUTE_REPLY_HOPS(Rep->opt.optDataLen); i++) {
            Metric = Rep->opt.hopList[i].Metric;
            if ((*VA->IsInfinite)(Metric)) {
                //
                // This shouldn't happen, because we never send infinity
                // in a Route Reply.
                //
                KdPrint(("MCL!CacheSRPacket: Reply infinite metric\n"));
                Metric = 0;
            }

            LinkCacheAddLink(VA,
                             Rep->opt.hopList[i-1].addr,
                             Rep->opt.hopList[i].addr,
                             Rep->opt.hopList[i].inif,
                             Rep->opt.hopList[i-1].outif,
                             Metric,
                             Reason);
            LinkAdded = TRUE;
        }

        //
        // Reset the backoff parameters for sending a Route Request
        // for the target address in this Route Reply.
        // NB: We do this whether or not we requested the reply.
        //
        ReqTableReceivedReply(VA, Rep->opt.hopList[i-1].addr);
    }

    for (Err = srp->err; Err != NULL; Err = Err->next) {
        LinkCacheAddLink(VA,
                         Err->opt.errorSrc,
                         Err->opt.unreachNode,
                         Err->opt.inIf,
                         Err->opt.outIf,
                         Err->opt.Metric,
                         (VirtualAddressEqual(Err->opt.errorDst,
                                              VA->Address) ?
                          LINK_STATE_CHANGE_ERROR :
                          LINK_STATE_CHANGE_SNOOP_ERROR));
        LinkAdded = TRUE;
    }

    //
    // Call SendBufCheck once, after the link cache is fully updated.
    //
    if (LinkAdded)
        SendBufCheck(VA);
}

//* MiniportReceiveLocally
//
//  Receives an SRPacket locally, using NdisMIndicateReceivePacket.
//  Decrypts the packet data for receiving it.
//
//  The ReceiveComplete handler is always called.
//
//  Does NOT directly consume srp or srp->Packet,
//  but the ReceiveComplete handler may do that.
//
void
MiniportReceiveLocally(
    MiniportAdapter *VA,
    SRPacket *srp,
    void (*Complete)(MiniportAdapter *VA, SRPacket *srp, NDIS_STATUS Status))
{
    NDIS_PACKET *Packet;
    NDIS_BUFFER *Buffer;
    void *Data;
    uint BufferLength;
    EtherHeader UNALIGNED *Ether;
    NDIS_STATUS Status;

    //
    // srp->Packet is NULL only in SRPackets that we generate ourself
    // (for example, for Acks, Route Requests, Route Replies)
    // and we should never try to receive such a packet locally.
    //
    ASSERT(srp->Packet != NULL);

    if (srp->PacketLength == srp->PayloadOffset) {
        //
        // This happens when we receive a packet which only contains
        // options, no payload.
        //
        InterlockedIncrement((PLONG)&VA->CountRecvEmpty);
        (*Complete)(VA, srp, NDIS_STATUS_SUCCESS);
        return;
    }

    //
    // The ethernet Type field is part of the encrypted data.
    //
    Status = CryptoDecryptPacket(VA, srp->Packet,
                                 srp->PayloadOffset,
                                 sizeof *Ether - sizeof Ether->Type,
                                 srp->IV, &Packet); 
    if (Status != NDIS_STATUS_SUCCESS) { 
        KdPrint(("MCL!MiniportReceiveLocally: CryptoDecryptPacket: %x\n", Status));
        InterlockedIncrement((PLONG)&VA->CountRecvDecryptFailure);
        (*Complete)(VA, srp, Status);
        return;
    }

    //
    // Get the ethernet header in the packet.
    //
    Buffer = NdisFirstBuffer(Packet);
    NdisQueryBuffer(Buffer, &Data, &BufferLength);
    if (BufferLength < sizeof *Ether) {
        InterlockedIncrement((PLONG)&VA->CountRecvSmall);
        NdisFreePacketClone(Packet);
        (*Complete)(VA, srp, NDIS_STATUS_INVALID_PACKET);
        return;
    }

    //
    // If the Type is ETYPE_MSFT, we do not receive the packet.
    //
    Ether = (EtherHeader UNALIGNED *) Data;
    if (Ether->Type == ETYPE_MSFT) {
        InterlockedIncrement((PLONG)&VA->CountRecvRecursive);
        NdisFreePacketClone(Packet);
        (*Complete)(VA, srp, NDIS_STATUS_SUCCESS);
        return;
    }

    InterlockedIncrement((PLONG)&VA->CountRecvLocally);

    if (srp->sr != NULL) {
        if (srp->sr->opt.salvageCount != 0)
            InterlockedIncrement((PLONG)&VA->CountRecvLocallySalvaged);

        //
        // On its way to us, the source route was augmented
        // with link metric information. Send this info
        // back to the originator in a route reply.
        //
        if (! srp->sr->opt.staticRoute)
            MiniportSendRouteReplyFromSR(VA, srp);
    }

    //
    // Initialize the ethernet header.
    //
    RtlCopyMemory(Ether->Dest, srp->Dest, SR_ADDR_LEN);
    RtlCopyMemory(Ether->Source, srp->Source, SR_ADDR_LEN);

    //
    // Prepare for MiniportReturnPacket.
    //
    PC(Packet)->srp = srp;
    PC(Packet)->ReceiveComplete = Complete;

    //
    // Indicate the packet up to any transports bound to us.
    // MiniportReturnPacket will be called asynchronously.
    //
    NDIS_SET_PACKET_STATUS(Packet, NDIS_STATUS_SUCCESS);
    NDIS_SET_PACKET_HEADER_SIZE(Packet, sizeof *Ether);
    NdisMIndicateReceivePacket(VA->MiniportHandle, &Packet, 1);
}

//* MiniportReturnPacket
//
//  Called by NDIS to return a packet after the transports have
//  finished their receive processing.
//
void
MiniportReturnPacket(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_PACKET Packet)
{
    MiniportAdapter *VA = (MiniportAdapter *) MiniportAdapterContext;
    SRPacket *srp = PC(Packet)->srp;
    void (*Complete)(MiniportAdapter *VA, SRPacket *srp, NDIS_STATUS Status) =
        PC(Packet)->ReceiveComplete;

    //
    // Free the clear-text packet that was created
    // in MiniportReceiveLocally.
    //
    NdisFreePacketClone(Packet);

    //
    // Indicate the completion of receive processing.
    //
    (*Complete)(VA, srp, NDIS_STATUS_SUCCESS);
}

//* MiniportReceiveComplete
//
//  Called after the completion of local receive processing
//  of an SRPacket.
//
static void
MiniportReceiveComplete(
    MiniportAdapter *VA,
    SRPacket *srp,
    NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(VA);
    UNREFERENCED_PARAMETER(Status);

    //
    // Free the SRPacket. This frees srp->Packet.
    //
    SRPacketFree(srp);
}

//* MiniportForwardRequestComplete
//
//  Called after the completion of forwarding of an SRPacket
//  with a Route Request option. The SRPacket still needs
//  to be received locally.
//
static void
MiniportForwardRequestComplete(
    MiniportAdapter *VA,
    SRPacket *srp,
    NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(Status);

    //
    // Receive the packet locally too.
    //
    MiniportReceiveLocally(VA, srp, MiniportReceiveComplete);
}

//* MiniportForwardingStall
//
//  Stalls execution very briefly, to prevent synchronization
//  when forwarding a broadcast route request.
//
void
MiniportForwardingStall(MiniportAdapter *VA)
{
    uint Degree;
    uint Microseconds;

    //
    // How many neighbors do we have?
    // With more neighbors we need to choose from a larger interval.
    //
    Degree = LinkCacheMyDegree(VA);

    //
    // We have a lower bound in case there are neighbors we don't know about.
    //
    Degree = max(Degree, 2);

    //
    // We have an upper bound to prevent the stall from being too large.
    // The DDK doc recommends stalling no longer than 50 microseconds.
    //
    Degree = min(Degree, 10);

    //
    // We spread ourselves across this range. The multiplicative
    // factor decreases collisions with neighbors performing
    // this same calculation.
    //
    Microseconds = GetRandomNumber(Degree * 3);
    InterlockedExchangeAdd((PLONG)&VA->TotalForwardingStall, Microseconds);

    NdisStallExecution(Microseconds);
}

//* ReceiveSRPacket
//
//  Performs receive processing for an LQSR packet,
//  after it has been parsed into an SRPacket structure.
//
//  Disposes of srp.
//
void
ReceiveSRPacket(
    MiniportAdapter *VA,
    ProtocolAdapter *PA,
    SRPacket *srp)
{
    InternalAcknowledgement **PrevAck;
    InternalAcknowledgement *Ack;
    InternalInfoRequest *InfoReq;
    InternalInfoReply *InfoRep;
    const static VirtualAddress BcastDest = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    //
    // Deal with probes immediately.  
    //
    if (srp->Probe != NULL) {
        //
        // First verify that we are using the same metric that this probe
        // is using. 
        // 
        // Then, verify that we're the appropriate destination. This means one
        // of two things:
        //  
        //  - ProbeType is ETX, and Opt.To is the broadcast address. 
        //  -- OR --
        //  - The "Opt.To" address matches our address, and the "Opt.InIf"
        //    interface matches the index of the physical adapter. 
        //

        if ((srp->Probe->Opt.MetricType == VA->MetricType) &&
            ((srp->Probe->Opt.ProbeType == METRIC_TYPE_ETX) ?
             VirtualAddressEqual(srp->Probe->Opt.To, BcastDest) :
             (VirtualAddressEqual(srp->Probe->Opt.To, VA->Address) &&
              (srp->Probe->Opt.InIf == PA->Index)))) {

            //
            // We know there is a link to us.
            //
            LinkCacheAddLink(VA,
                             srp->Probe->Opt.From,
                             VA->Address,
                             (LQSRIf) PA->Index,
                             srp->Probe->Opt.OutIf,
                             srp->Probe->Opt.Metric,
                             LINK_STATE_CHANGE_ADD_PROBE);

            //
            // Process the probe according to whatever metric
            // we are using. 
            //
            switch (VA->MetricType) {
            case METRIC_TYPE_RTT:
                RttReceiveProbe(VA, srp->Probe);
                break;
            case METRIC_TYPE_PKTPAIR:
                PktPairReceiveProbe(VA, srp->Probe);
                break;
            case METRIC_TYPE_ETX:
                EtxReceiveProbe(VA, srp->Probe, (LQSRIf) PA->Index);
                break;
            case METRIC_TYPE_WCETT:
                WcettReceiveProbe(VA, srp->Probe, (LQSRIf) PA->Index);
                break;
            }
        }
    }

    //
    // Process a Probe Reply.
    //
    if (srp->ProbeReply != NULL) {
        //
        // Verify that we're the appropriate destination and that
        // we are using the same metric.
        //
        if (VirtualAddressEqual(srp->ProbeReply->Opt.To, VA->Address) &&
            (srp->ProbeReply->Opt.InIf == PA->Index) &&
            (srp->ProbeReply->Opt.MetricType == VA->MetricType)) {
            //
            // Process the probe reply according to whatever metric
            // we are using. 
            // 
            switch (VA->MetricType) {
            case METRIC_TYPE_RTT:
                RttReceiveProbeReply(VA, srp->ProbeReply);
                break;
            case METRIC_TYPE_PKTPAIR:
                PktPairReceiveProbeReply(VA, srp->ProbeReply);
                break;
            case METRIC_TYPE_WCETT:
                WcettReceivePktPairProbeReply(VA, srp->ProbeReply);
                break;
            }
        } 
    }

    //
    // Process any Information Requests.
    //
    for (InfoReq = srp->inforeq; InfoReq != NULL; InfoReq = InfoReq->next)
        MiniportSendInfo(VA, PA, srp, InfoReq);

    //
    // Process any Information options.
    //
    for (InfoRep = srp->inforep; InfoRep != NULL; InfoRep = InfoRep->next) {
        VA->MinVersionSeen = min(VA->MinVersionSeen, InfoRep->opt.version);
        VA->MaxVersionSeen = max(VA->MaxVersionSeen, InfoRep->opt.version);
    }

    //
    // Send an Ack immediately, if requested.
    //
    if (srp->ackreq != NULL) {
        uint Index;

        ASSERT(srp->sr != NULL);
        Index = SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen) - srp->sr->opt.segmentsLeft;

        //
        // Check to make sure I'm actually the destination,
        // since the packet may have been MAC broadcast.
        //
        if (VirtualAddressEqual(srp->sr->opt.hopList[Index].addr,
                                VA->Address) &&
            (srp->sr->opt.hopList[Index].inif == PA->Index)) {

            MaintBufSendAck(VA, srp);
        }

        ExFreePool(srp->ackreq);
        srp->ackreq = NULL;
    }

    //
    // Process any received Acks immediately,
    // if they are for us.
    //
    PrevAck = &srp->ack;
    while ((Ack = *PrevAck) != NULL) {
        //
        // Is this Ack for us?
        // NB: We do not check Ack->opt.outif == PA->Index!
        // It doesn't matter how the Ack got to us -
        // it might not have been via the reverse route.
        //
        if (VirtualAddressEqual(Ack->opt.to, VA->Address)) {
            //
            // Process the Ack.
            //
            MaintBufRecvAck(VA, Ack->opt.from, Ack->opt.inif, Ack->opt.outif,
                            Ack->opt.identification);
            //
            // Remove the Ack from the packet.
            //
            *PrevAck = Ack->next;
            ExFreePool(Ack);
            continue;
        }
        PrevAck = &Ack->next;
    }

    if (srp->sr != NULL) {
        uint Index = SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen) - srp->sr->opt.segmentsLeft;

        //
        // This packet has a source route.
        // Check to make sure I'm actually the destination, since right now
        // sometimes the packets are MAC broadcast...
        //
        if (!VirtualAddressEqual(srp->sr->opt.hopList[Index].addr,
                                 VA->Address) ||
            (srp->sr->opt.hopList[Index].inif != PA->Index))
            goto FreeSrpAndReturn;

        //
        // Inspect segmentsLeft to decide if we should receive
        // the packet or forward it on its next hop.
        //
        if (--srp->sr->opt.segmentsLeft == 0) {

            MiniportReceiveLocally(VA, srp, MiniportReceiveComplete);
        }
        else {
            //
            // We must forward the packet.
            //
            InterlockedIncrement((PLONG)&VA->CountXmitForwardUnicast);

            if (! LinkCacheUseSR(VA, srp)) {
                InterlockedIncrement((PLONG)&VA->CountXmitForwardQueueFull);
                goto FreeSrpAndReturn;
            }

            MaintBufSendPacket(VA, srp, MiniportReceiveComplete);
        }
    }
    else if (srp->req != NULL) {
        uint Hops;

        //
        // If we have already seen this Route Request before,
        // then ignore this one.
        //
        if (! ForwardRequestP(VA, srp))
            goto FreeSrpAndReturn;

        //
        // Add ourself to the Route Request Hop List.
        //
        Hops = ROUTE_REQUEST_HOPS(srp->req->opt.optDataLen);
        if (Hops >= MAX_SR_LEN)
            goto FreeSrpAndReturn;
        srp->req->opt.optDataLen = ROUTE_REQUEST_LEN(Hops + 1);
        RtlCopyMemory(srp->req->opt.hopList[Hops].addr,
                      VA->Address, SR_ADDR_LEN);
        srp->req->opt.hopList[Hops].inif = (LQSRIf) PA->Index;
        // hopList[Hops].outif will be initialized in ProtocolForwardRequest
        // and MiniportSendRouteReply.

        //
        // Find the metric for the link this packet just used to reach us.
        // Add that metric to this hop in the Route Request option.
        // This initializes srp->req->opt.hopList[Hops].Metric.
        //
        LinkCacheUpdateRR(VA, srp->req);

        if (VirtualAddressEqual(srp->req->opt.targetAddress,
                                VA->Address)) {
            //
            // This Route Request is for us, so send a Route Reply.
            //
            MiniportSendRouteReply(VA, srp);

            //
            // Then receive the packet locally.
            //
            MiniportReceiveLocally(VA, srp, MiniportReceiveComplete);
        }
        else {
            //
            // Stall our execution briefly. This prevents undesirable
            // synchronization when multiple nodes simultaneously receive
            // a broadcast route request, and simultaneously try to forward
            // it and collide.
            //
            MiniportForwardingStall(VA);
            InterlockedIncrement((PLONG)&VA->CountXmitForwardBroadcast);

            //
            // Broadcast the Route Request via our physical adapters.
            // Then completion function will receive the packet locally.
            //
            srp->TransmitComplete = MiniportForwardRequestComplete;
            MiniportForwardRequest(VA, srp);
        }
    }
    else {
        //
        // The packet does not have a Route Request or a Source Route.
        //
        goto FreeSrpAndReturn;
    }

    return;

  FreeSrpAndReturn:
    SRPacketFree(srp);
}

//* MiniportReceivePacket
//
//  Receive an LQSR packet from a virtual link.
//
void
MiniportReceivePacket(
    MiniportAdapter *VA,
    ProtocolAdapter *PA,
    NDIS_PACKET *Packet,
    void (*FreePacket)(ProtocolAdapter *PA, NDIS_PACKET *Packet))
{
    SRPacket *srp;
    NDIS_STATUS Status;

    InterlockedIncrement((PLONG)&VA->CountRecv);

    Status = SRPacketFromPkt(VA, Packet, &srp);
    if (Status != NDIS_STATUS_SUCCESS) {
        (*FreePacket)(PA, Packet);
        return;
    }

    srp->PA = PA;
    srp->FreePacket = FreePacket;

    //
    // See if we should make this link behave worse than it is.
    // For testing purposes.
    //
    if (ArtificialDrop(VA, srp)) {
        SRPacketFree(srp);
        return;
    }

    //
    // TODO: doing this here isn't quite right, because it means that the lack
    // of resources (esp NDIS_PACKET resources) will prevent a node from 
    // learning new information, which may be used to free up some of the
    // packets in the SendBuf. This theoretically isn't critical, since once
    // the sendbuf gets full, it won't consume any more resources, so 
    // eventually you should learn everything. In practice, this may be a
    // performance issue.
    //
    CacheSRPacket(VA, PA, srp);
    ReceiveSRPacket(VA, PA, srp);
}

//* MiniportSendRouteError
//
//  Given an SRPacket that Route Maintenance has given up on,
//  sends a Route Error.
//
//  Does not dispose of srp or srp->Packet.
//
void
MiniportSendRouteError(
    MiniportAdapter *VA,
    const SRPacket *srp)
{
    InternalRouteError *RE;
    uint Index;
    uint Metric;

    //
    // The Maintenance Buffer only works with Source Routed packets.
    //
    ASSERT(srp->sr != NULL);

    //
    // Do not send errors for statically routed
    // packets.
    //
    if (srp->sr->opt.staticRoute) 
        return;
   
    //
    // Do not send an error for an error.
    //
    if (srp->err != NULL)
        return;

    //
    // Figure out which hop in the Source Route has failed.
    //
    Index = SOURCE_ROUTE_HOPS(srp->sr->opt.optDataLen) - srp->sr->opt.segmentsLeft - 1;
    ASSERT(VirtualAddressEqual(srp->sr->opt.hopList[Index].addr, VA->Address));

    //
    // Do not send an error to ourselves.
    //
    if (Index == 0)
        return;

    //
    // Lookup the current metric for the failed link.
    // The link metric has already been penalized.
    //
    Metric = LinkCacheLookupMetric(VA,
                                   srp->sr->opt.hopList[Index].addr,
                                   srp->sr->opt.hopList[Index + 1].addr,
                                   srp->sr->opt.hopList[Index + 1].inif,
                                   srp->sr->opt.hopList[Index].outif);
    if (Metric == 0) {
        if (FindPhysicalAdapterFromIndex(VA,
                        srp->sr->opt.hopList[Index].outif) != NULL) {
            //
            // Hmm, the outgoing interface still exists.
            // Somehow the link managed to disappear from the link cache.
            //
            InterlockedIncrement((PLONG)&VA->CountXmitRouteErrorNoLink);
            return;
        }

        //
        // Report that the link is gone.
        //
        Metric = (uint)-1;
    }

    InterlockedIncrement((PLONG)&VA->CountXmitRouteError);

    //
    // Allocate the Route Error option.
    //
    RE = ExAllocatePool(NonPagedPool, sizeof *RE);
    if (RE == NULL)
        return;

    //
    // Initialize the Route Error option.
    //
    RtlZeroMemory(RE, sizeof *RE);
    RE->opt.optionType = LQSR_OPTION_TYPE_ERROR;
    RE->opt.optDataLen = ROUTE_ERROR_LENGTH;
    RtlCopyMemory(RE->opt.errorSrc, srp->sr->opt.hopList[Index].addr, SR_ADDR_LEN);
    RtlCopyMemory(RE->opt.errorDst, srp->sr->opt.hopList[0].addr, SR_ADDR_LEN);
    RtlCopyMemory(RE->opt.unreachNode, srp->sr->opt.hopList[Index + 1].addr,
                  SR_ADDR_LEN);
    RE->opt.inIf = srp->sr->opt.hopList[Index + 1].inif;
    RE->opt.outIf = srp->sr->opt.hopList[Index].outif;
    RE->opt.Metric = Metric;

    //
    // Send the Route Error option.
    //
    PbackSendOption(VA, RE->opt.errorDst,
                    (InternalOption *) RE,
                    PBACK_ROUTE_ERROR_TIMEOUT);
}

uint MiniportInfoRequestIdentifier = 0;

//* MiniportSendInfoRequest
//
//  Sends an Information Request.
//
void
MiniportSendInfoRequest(
    MiniportAdapter *VA)
{
    SRPacket *srp;
    InternalInfoRequest *IR;
    VirtualAddress Target;
    LQSRReqId Identifier;

    InterlockedIncrement((PLONG)&VA->CountXmitInfoRequest);

    //
    // Initialize a new SRPacket for the Information Request.
    //
    srp = ExAllocatePool(NonPagedPool, sizeof *srp);
    if (srp == NULL)
        return;
    RtlZeroMemory(srp, sizeof *srp);

    //
    // Initialize the Information Request option.
    //
    IR = ExAllocatePool(NonPagedPool, sizeof *IR);
    if (IR == NULL) {
        SRPacketFree(srp);
        return;
    }

    srp->inforeq = IR;
    RtlZeroMemory(IR, sizeof *IR);
    IR->opt.optionType = LQSR_OPTION_TYPE_INFOREQ;
    IR->opt.optDataLen = INFO_REQUEST_LEN;
    IR->opt.identification =
        InterlockedIncrement((PLONG)&MiniportInfoRequestIdentifier);
    RtlCopyMemory(IR->opt.sourceAddress, VA->Address, SR_ADDR_LEN);

    //
    // We use a dummy Target address.
    //
    RtlZeroMemory(Target, SR_ADDR_LEN);

    Identifier = ReqTableIdentifier(VA, Target);
    MiniportGenerateRouteRequest(VA, srp, Target, Identifier,
                                 MiniportControlSendComplete);
}

//* MiniportSendInfo
//
//  Sends an Information packet.
//
static void
MiniportSendInfo(
    MiniportAdapter *VA,
    ProtocolAdapter *PA,
    const SRPacket *srp,
    const InternalInfoRequest *InfoReq)
{
    InternalInfoReply *InfoRep;

    //
    // Do not send an Info packet to ourself.
    //
    if (VirtualAddressEqual(InfoReq->opt.sourceAddress, VA->Address))
        return;

    InterlockedIncrement((PLONG)&VA->CountXmitInfoReply);

    //
    // Allocate the Info option.
    //
    InfoRep = ExAllocatePool(NonPagedPool, sizeof *InfoRep);
    if (InfoRep == NULL)
        return;

    //
    // Allocate the Info option.
    //
    RtlZeroMemory(InfoRep, sizeof *InfoRep);
    InfoRep->opt.optionType = LQSR_OPTION_TYPE_INFO;
    InfoRep->opt.optDataLen = INFO_REPLY_LEN;
    InfoRep->opt.identification = InfoReq->opt.identification;
    InfoRep->opt.version = Version;

    //
    // Send the Info option.
    //
    PbackSendOption(VA, InfoReq->opt.sourceAddress,
                    (InternalOption *) InfoRep,
                    PBACK_INFO_REPLY_TIMEOUT);

    //
    // This code is similar to MiniportSendRouteReply,
    // with two differences:
    // 1. We are not yet added to the Route Request option,
    // so we have to add ourself here to the Route Reply.
    // 2. The timeout is different.
    // 3. We do not increment CountXmitRouteReply.
    //
    if (srp->req != NULL) {
        uint Hops = ROUTE_REQUEST_HOPS(srp->req->opt.optDataLen);
        InternalRouteReply *RR;

        //
        // Allocate the Route Reply option.
        //
        RR = ExAllocatePool(NonPagedPool,
                            sizeof *RR + sizeof(SRAddr)*MAX_SR_LEN);
        if (RR == NULL)
            return;

        //
        // Initialize the Route Reply option.
        //
        RtlZeroMemory(RR, sizeof *RR + sizeof(SRAddr)*MAX_SR_LEN);

        RtlCopyMemory(RR->opt.hopList, srp->req->opt.hopList,
                      Hops * sizeof(SRAddr));

        if (Hops < MAX_SR_LEN) {
            //
            // Add ourself to the Route Reply.
            //
            RtlCopyMemory(RR->opt.hopList[Hops].addr,
                          VA->Address, SR_ADDR_LEN);
            RR->opt.hopList[Hops].inif = (LQSRIf) PA->Index;
            RR->opt.hopList[Hops].outif = 0;
            Hops++;
        }

        RR->opt.optionType = LQSR_OPTION_TYPE_REPLY;
        RR->opt.optDataLen = ROUTE_REPLY_LEN(Hops);

        //
        // Send the Route Reply option.
        //
        PbackSendOption(VA, InfoReq->opt.sourceAddress,
                        (InternalOption *) RR,
                        PBACK_INFO_REPLY_TIMEOUT);
    }
}

//* MiniportSendRouteRequest
//
//  Sends a Route Request.
//
void
MiniportSendRouteRequest(
    MiniportAdapter *VA,
    const VirtualAddress Target,
    LQSRReqId Identifier)
{
    SRPacket *srp;

    InterlockedIncrement((PLONG)&VA->CountXmitRouteRequest);

    srp = ExAllocatePool(NonPagedPool, sizeof *srp);
    if (srp == NULL)
        return;
    RtlZeroMemory(srp, sizeof *srp);

    MiniportGenerateRouteRequest(VA, srp,
                                 Target, Identifier,
                                 MiniportControlSendComplete);
}

//* MiniportResetStatistics
//
//  Resets all counters and statistics gathering for the virtual adapter
//  and associated data structures.
//
void
MiniportResetStatistics(MiniportAdapter *VA)
{
    ProtocolAdapter *PA;
    KIRQL OldIrql;

    VA->CountPacketPoolFailure = 0;

    VA->CountXmit = 0;
    VA->CountXmitLocally = 0;
    VA->CountXmitMulticast = 0;
    VA->CountXmitRouteRequest = 0;
    VA->CountXmitRouteReply = 0;
    VA->CountXmitRouteError = 0;
    VA->CountXmitSendBuf = 0;
    VA->CountXmitMaintBuf = 0;
    VA->CountXmitForwardUnicast = 0;
    VA->CountXmitForwardBroadcast = 0;
    VA->CountXmitInfoRequest = 0;
    VA->CountXmitInfoReply = 0;
    VA->CountXmitProbe = 0;
    VA->CountXmitProbeReply = 0;
    VA->CountXmitLinkInfo = 0;
    VA->CountSalvageAttempt = 0;
    VA->CountSalvageStatic = 0;
    VA->CountSalvageOverflow = 0;
    VA->CountSalvageNoRoute = 0;
    VA->CountSalvageTransmit = 0;
    VA->CountRecv = 0;
    VA->CountRecvLocally = 0;
    VA->CountRecvLocallySalvaged = 0;
    VA->CountRecvBadMAC = 0;
    VA->CountRecvRouteRequest = 0;
    VA->CountRecvRouteReply = 0;
    VA->CountRecvRouteError = 0;
    VA->CountRecvAckRequest = 0;
    VA->CountRecvAck = 0;
    VA->CountRecvSourceRoute = 0;
    VA->CountRecvInfoRequest = 0;
    VA->CountRecvInfoReply = 0;
    VA->CountRecvDupAckReq = 0;
    VA->CountRecvProbe = 0;
    VA->CountRecvProbeReply = 0;
    VA->CountRecvLinkInfo = 0;
    VA->CountRecvRecursive = 0;
    VA->CountRecvEmpty = 0;
    VA->CountRecvSmall = 0;
    VA->CountRecvDecryptFailure = 0;

    VA->MinVersionSeen = Version;
    VA->MaxVersionSeen = Version;

    VA->TotalForwardingStall = 0;

    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    VA->ForwardMax = VA->ForwardNum;
    VA->CountForwardFast = 0;
    VA->CountForwardQueue = 0;
    VA->CountForwardDrop = 0;

    VA->TimeoutMinLoops = MAXULONG;
    VA->TimeoutMaxLoops = 0;

    for (PA = VA->PhysicalAdapters; PA != NULL; PA = PA->Next)
        ProtocolResetStatistics(PA);
    KeReleaseSpinLock(&VA->Lock, OldIrql);

    LinkCacheResetStatistics(VA);
    MaintBufResetStatistics(VA);
    SendBufResetStatistics(VA);
    ReqTableResetStatistics(VA);
    PbackResetStatistics(VA);
}

//* MiniportHalt
//
//  Called by NDIS. Cleans up and frees the virtual adapter.
//
//  BUGBUG: There is a race here. It is possible for a reference
//  to prevent the driver from unloading after MiniportHalt returns.
//  Then a new MiniportAdapter can be created and so we are running
//  but there is no ioctl access because there is no device.
//  Furthermore a subsequent MiniportHalt blue-screens.
//
void
MiniportHalt(
    IN NDIS_HANDLE MiniportAdapterContext)
{
    MiniportAdapter *VA = (MiniportAdapter *) MiniportAdapterContext;
    KIRQL OldIrql;
    NDIS_STATUS Status;

    KdPrint(("MCL!MiniportHalt(VA %p)\n", VA));

    //
    // Remove the virtual adapter from our list.
    //
    KeAcquireSpinLock(&MiniportAdapters.Lock, &OldIrql);
    if (VA->Next != NULL)
        VA->Next->Prev = VA->Prev;
    *VA->Prev = VA->Next;
    KeReleaseSpinLock(&MiniportAdapters.Lock, OldIrql);

    ProtocolCleanup(VA);

    //
    // Stop our periodic timer.
    // We need to do this after ProtocolCleanup,
    // so that MiniportTimeout can finish forwarding packets
    // so NdisDeregisterProtocol can complete.
    //
    KeCancelTimer(&VA->Timer);

#if 0 // This API does not exist in WinXP.
    //
    // Wait for any DPCs (in particular our timer DPC) to finish.
    //
    KeFlushQueuedDpcs();
#endif

    //
    // If we are holding onto packets that we are originating,
    // we will not be halted. After NdisDeregisterProtocol,
    // we will not hold any packets that we are forwarding.
    // But we can still have internally-originated packets
    // in the list. We need to do this after stopping the timer
    // because PbackTimeout can add packets to VA->ForwardList.
    //
    MiniportForwardCleanup(VA);
   
    NeighborCacheCleanup(&VA->NC);
    PbackCleanup(VA);
    LinkCacheFree(VA);
    ReqTableFree(VA);
    SendBufFree(VA);
    MaintBufFree(VA);
    NdisFreePacketPool(VA->PacketPool);
    NdisFreeBufferPool(VA->BufferPool);
    ExFreePool(VA);

    //
    // If there are no more virtual adapters,
    // delete our custom device. This will allow us to unload.
    //
    if (MiniportAdapters.VirtualAdapters == NULL) {

        Status = NdisMDeregisterDevice(OurDeviceHandle);
        KdPrint(("MCL!NdisMDeregisterDevice -> %x\n", Status));
    }
}

//* MiniportShutdown
//
//  Called by NDIS to shutdown the adapter.
//
void
MiniportShutdown(
    IN PVOID ShutdownContext)
{
    MiniportAdapter *VA = (MiniportAdapter *) ShutdownContext;

    UNREFERENCED_PARAMETER(VA);
    KdPrint(("MCL!MiniportShutdown(VA %p)\n", VA));
}

//* MiniportIndicateStatusConnected
//
//  Indicate that our virtual adapter is now connected.
//
void
MiniportIndicateStatusConnected(MiniportAdapter *VA)
{
    KdPrint(("MCL!MiniportIndicateStatusConnected(VA %p)\n", VA));

    NdisMIndicateStatus(VA->MiniportHandle, NDIS_STATUS_MEDIA_CONNECT,
                        NULL, 0);
    NdisMIndicateStatusComplete(VA->MiniportHandle);
}

//* MiniportIndicateStatusDisconnected
//
//  Indicate that our virtual adapter is now disconnected.
//
void
MiniportIndicateStatusDisconnected(
    MiniportAdapter *VA)
{
    KdPrint(("MCL!MiniportIndicateStatusDisconnected(VA %p)\n", VA));

    NdisMIndicateStatus(VA->MiniportHandle, NDIS_STATUS_MEDIA_DISCONNECT,
                        NULL, 0);
    NdisMIndicateStatusComplete(VA->MiniportHandle);
}

//* MiniportSendLinkInfo
//
//  Sends a null Route Request for the purpose of disseminating
//  link information.
//
void
MiniportSendLinkInfo(
    MiniportAdapter *VA)
{
    const static VirtualAddress Dest = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    ASSERT(VA->MetricType != METRIC_TYPE_HOP);
    InterlockedIncrement((PLONG)&VA->CountXmitLinkInfo);

    MiniportSendRouteRequest(VA, Dest, ReqTableIdentifier(VA, Dest));
}

//* MiniportSendPeriodicLinkInfo
//
//  If we have not sent link information recently,
//  then do it now.
//
//  Called at DPC level from MiniportTimeout.
//
Time
MiniportSendPeriodicLinkInfo(
    MiniportAdapter *VA,
    Time Now)
{
    Time LastLinkInfo = InterlockedRead64((LONGLONG *)&VA->LastLinkInfo);

    ASSERT(VA->MetricType != METRIC_TYPE_HOP);

    //
    // If we have not sent link information recently,
    // do it now.
    //
    if (LastLinkInfo + LINKINFO_PERIOD <= Now) {
        //
        // This updates VA->LastLinkInfo if it succeeds.
        // But we use our local variable to guarantee that
        // our returned timeout is in the future.
        //
        LastLinkInfo = Now;
        MiniportSendLinkInfo(VA);
    }

    //
    // Return the time by which we should next send link information.
    //
    return LastLinkInfo + LINKINFO_PERIOD;
}

//* MiniportRescheduleTimeoutHelper
//
//  Reschedules the timer so that MiniportTimeout
//  is called no later than Timeout.
//
//  Called with the miniport lock held.
//
static void
MiniportRescheduleTimeoutHelper(
    MiniportAdapter *VA,
    Time Now,
    Time Timeout)
{
    LARGE_INTEGER Delay;

    ASSERT(Now < Timeout);
    ASSERT(Timeout < VA->Timeout);

    //
    // Switch to the new earlier time.
    //
    VA->Timeout = Timeout;

    //
    // We want to reschedule the timer.
    // Try to cancel it.
    //
    if (KeCancelTimer(&VA->Timer)) {
        //
        // We canceled the timer, so we can reschedule.
        //
        Delay.QuadPart = Now - VA->Timeout; // Negative so its relative.
        KeSetTimer(&VA->Timer, Delay, &VA->TimeoutDpc);
    }
    else {
        //
        // We failed to cancel the timer.
        // This means MiniportTimeout is already running.
        //
    }
}

//* MiniportRescheduleTimeout
//
//  Reschedules the timer so that MiniportTimeout
//  is called no later than Timeout.
//  NB: Unfortunately the granularity of the system clock
//  interrupt prevents MiniportTimeout from waking up accurately.
//
//  Called with no locks held.
//
void
MiniportRescheduleTimeout(
    MiniportAdapter *VA,
    Time Now,
    Time Timeout)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    if (Timeout < VA->Timeout)
        MiniportRescheduleTimeoutHelper(VA, Now, Timeout);
    KeReleaseSpinLock(&VA->Lock, OldIrql);
}


//* MiniportTimeout
//
//  Called aperiodically (at least every MINIPORT_TIMEOUT milliseconds)
//  at DPC level.
//
static void
MiniportTimeout(
    PKDPC Dpc,
    void *Context,
    void *Unused1,
    void *Unused2)
{
    MiniportAdapter *VA = (MiniportAdapter *) Context;
    Time Now;
    Time Timeout;
    Time UpdateTimeout;
    LARGE_INTEGER TimerTimeout;
    uint NumLoops = 0;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Unused1);
    UNREFERENCED_PARAMETER(Unused2);

    for (;;) {
        KeAcquireSpinLockAtDpcLevel(&VA->Lock);
        Now = KeQueryInterruptTime();

        //
        // If we have not reached the Timeout yet,
        // we should re-arm the timer and wait.
        //
        if (Now < VA->Timeout)
            break;

        NumLoops++;

        //
        // Set our next Timeout.
        // NB: KeQueryInterruptTime does not advance continuously.
        // It can and will jump forward when the system sleeps.
        // Hence VA->Timeout += (Time) (MINIPORT_TIMEOUT * 10000)
        // is not good: we will loop multiple times catching up
        // after a jump.
        //
        VA->Timeout = Now + (Time) (MINIPORT_TIMEOUT * 10000);
        KeReleaseSpinLockFromDpcLevel(&VA->Lock);

        //
        // Perform our timeout processing,
        // keeping track of a new desired timeout.
        // NB: MiniportRescheduleTimeout may also lower VA->Timeout.
        //
        Timeout = MAXTIME;

        //
        // Metric-specific procedures. 
        //

        switch (VA->MetricType) {
        case METRIC_TYPE_RTT:
            // 
            // Sweep for late probes. 
            //
            UpdateTimeout = RttSweepForLateProbes(VA, Now);
            ASSERT(Now < UpdateTimeout);
            if (UpdateTimeout < Timeout)
                Timeout = UpdateTimeout;
    
            // 
            // Set metric using hysteresis.
            //
            UpdateTimeout = RttUpdateMetric(VA, Now);
            ASSERT(Now < UpdateTimeout);
            if (UpdateTimeout < Timeout)
                Timeout = UpdateTimeout;
            
            //
            // Send Probes. 
            //
            UpdateTimeout = RttSendProbes(VA, Now);
            ASSERT(Now < UpdateTimeout);
            if (UpdateTimeout < Timeout)
                Timeout = UpdateTimeout;
            break;

        case METRIC_TYPE_PKTPAIR:
            //
            // Send Probes. 
            //
            UpdateTimeout = PktPairSendProbes(VA, Now);
            ASSERT(Now < UpdateTimeout);
            if (UpdateTimeout < Timeout)
                Timeout = UpdateTimeout;
            break;

        case METRIC_TYPE_ETX:
            //
            // Send Probes. 
            //
            UpdateTimeout = EtxSendProbes(VA, Now);
            ASSERT(Now < UpdateTimeout);
            if (UpdateTimeout < Timeout)
                Timeout = UpdateTimeout;
            break;

        case METRIC_TYPE_WCETT:
            //
            // Send Probes.
            //
            UpdateTimeout = WcettSendProbes(VA, Now);
            ASSERT(Now < UpdateTimeout);
            if (UpdateTimeout < Timeout)
                Timeout = UpdateTimeout;
            break;
        }

        MiniportForwardDelayedRequests(VA, Now);

        SendBufCheck(VA);

        UpdateTimeout = MaintBufTimer(VA, Now);
        ASSERT(Now < UpdateTimeout);
        if (UpdateTimeout < Timeout)
            Timeout = UpdateTimeout;

        UpdateTimeout = PbackTimeout(VA, Now);
        ASSERT(Now < UpdateTimeout);
        if (UpdateTimeout < Timeout)
            Timeout = UpdateTimeout;

        if (VA->MetricType != METRIC_TYPE_HOP) {
            UpdateTimeout = MiniportSendPeriodicLinkInfo(VA, Now);
            ASSERT(Now < UpdateTimeout);
            if (UpdateTimeout < Timeout)
                Timeout = UpdateTimeout;
        }

        //
        // Update VA->Timeout.
        //
        if (Timeout < VA->Timeout)
            VA->Timeout = Timeout;
    }

    if (NumLoops > VA->TimeoutMaxLoops)
        VA->TimeoutMaxLoops = NumLoops;
    if (NumLoops < VA->TimeoutMinLoops)
        VA->TimeoutMinLoops = NumLoops;

    //
    // Re-arm the timer so we get called again.
    //
    TimerTimeout.QuadPart = Now - VA->Timeout; // Negative so its relative.
    KeSetTimer(&VA->Timer, TimerTimeout, &VA->TimeoutDpc);
    KeReleaseSpinLockFromDpcLevel(&VA->Lock);
}

//* MiniportUnload
//
//  Called by NDIS when the miniport is unloading.
//
void
MiniportUnload(
    PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    KdPrint(("MCL!MiniportUnload(Driver %p)\n", DriverObject));
    
    //
    // Restore the system default timer resolution.
    // NB: If other drivers have also modified the timer resolution,
    // the default is not restored until they also request it.
    //
    (void) ExSetTimerResolution(0, FALSE);

#if COUNTING_MALLOC
    DumpCountingMallocStats();
    UnloadCountingMalloc();
#endif
}

//* MiniportInit
//
//  Initializes the MCL miniport module.
//  Registers with NDIS as a miniport.
//
//  Returns FALSE to indicate failure.
//
boolint
MiniportInit(
    IN PDRIVER_OBJECT DriverObject,   // MCL driver object.
    IN PUNICODE_STRING RegistryPath)  // Path to our info in the registry.
{
    UNICODE_STRING DeviceName;
    UNICODE_STRING WinDeviceName;
    NDIS_MINIPORT_CHARACTERISTICS MPC;
    NDIS_STATUS Status;
    ULONG TimerResolution;
#if DBG
    LARGE_INTEGER PerformanceFrequency;
#endif

    //
    // Ask for 100us timer resolution and see what we get.
    // Typically we will get about 1ms.
    //
    TimerResolution = ExSetTimerResolution(1000, TRUE);
    KdPrint(("ExSetTimerResolution -> %u\n", TimerResolution));

#if DBG
    //
    // See how often the interval clock interrupts.
    //
    KdPrint(("KeQueryTimeIncrement -> %u\n", KeQueryTimeIncrement()));
    (void)KeQueryPerformanceCounter(&PerformanceFrequency);
    KdPrint(("PerformanceFrequency -> %I64d\n", PerformanceFrequency));
#endif

    //
    // We maintain a list of virtual adapters.
    //
    KeInitializeSpinLock(&MiniportAdapters.Lock);
    ASSERT(MiniportAdapters.VirtualAdapters == NULL);

    //
    // Register with NDIS as a miniport.
    // As a result, NDIS will call MiniportInitialize
    // for each virtual adapter.
    //

    NdisMInitializeWrapper(&MiniportHandle, DriverObject, RegistryPath, NULL);
    KdPrint(("MCL!NdisMInitializeWrapper -> %x\n", MiniportHandle));
    if (MiniportHandle == NULL)
        return FALSE;

    RtlZeroMemory(&MPC, sizeof MPC);
    MPC.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    MPC.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    MPC.InitializeHandler = MiniportInitialize;
    MPC.QueryInformationHandler = MiniportQueryInformation;
    MPC.SetInformationHandler = MiniportSetInformation;
    MPC.SendPacketsHandler = MiniportTransmitPackets;
    MPC.ReturnPacketHandler = MiniportReturnPacket;
    MPC.HaltHandler = MiniportHalt;
    MPC.AdapterShutdownHandler = MiniportShutdown;

    Status = NdisMRegisterMiniport(MiniportHandle, &MPC, sizeof MPC);
    KdPrint(("MCL!NdisMRegisterMiniport -> %x\n", Status));
    if (Status != NDIS_STATUS_SUCCESS) {
        NdisTerminateWrapper(MiniportHandle, NULL);
        return FALSE;
    }

    //
    // Create a custom device object that we can use for IOCTLs.
    //

    RtlInitUnicodeString(&DeviceName, DD_MCL_DEVICE_NAME);
    RtlInitUnicodeString(&WinDeviceName, L"\\??\\" WIN_MCL_BASE_DEVICE_NAME);

    Status = NdisMRegisterDevice(MiniportHandle,
                                 &DeviceName, &WinDeviceName,
                                 IoMajorFunctions,
                                 &OurDeviceObject,
                                 &OurDeviceHandle);
    KdPrint(("MCL!NdisMRegisterDevice -> %x\n", Status));
    if (Status != NDIS_STATUS_SUCCESS) {
        NdisTerminateWrapper(MiniportHandle, NULL);
        return FALSE;
    }

    //
    // Register an unload handler for cleanup.
    //
    NdisMRegisterUnloadHandler(MiniportHandle, MiniportUnload);

    return TRUE;
}
