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

#define MIN(a, b) ((a) < (b) ? (a) : (b))

//* IoTimeDelta
//
//  Calculates a delta between interrupt time and system time.
//  We use interrupt time internally but we return system time
//  in our ioctls. So any absolute time values must be translated.
//
Time
IoTimeDelta(void)
{
    Time InterruptTime;
    Time SystemTime;

    InterruptTime = KeQueryInterruptTime();
    KeQuerySystemTime((LARGE_INTEGER *)&SystemTime);

    return SystemTime - InterruptTime;
}

//* IoCreate
//
//  Handles the IRP_MJ_CREATE request for the MCL device.
//
NTSTATUS
IoCreate(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    return STATUS_SUCCESS;
}

//* IoCleanup
//
//  Handles the IRP_MJ_CLEANUP request for the MCL device.
//
NTSTATUS
IoCleanup(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    return STATUS_SUCCESS;
}

//* IoClose
//
//  Handles the IRP_MJ_CLOSE request for the MCL device.
//
NTSTATUS
IoClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    return STATUS_SUCCESS;
}

//* FindVirtualAdapterFromQuery
//
//  Given an MCL_QUERY_VIRTUAL_ADAPTER structure,
//  finds the specified virtual adapter.
//  The virtual adapter (if found) is returned with a reference.
//
MiniportAdapter *
FindVirtualAdapterFromQuery(
    MCL_QUERY_VIRTUAL_ADAPTER *Query)
{
    MiniportAdapter *VA;

    if (Query->Index == 0)
        VA = FindVirtualAdapterFromGuid(&Query->Guid);
    else
        VA = FindVirtualAdapterFromIndex(Query->Index);

    return VA;
}

//* ReturnQueryVirtualAdapter
//
//  Initializes a returned MCL_QUERY_VIRTUAL_ADAPTER structure
//  with query information for the specified virtual adapter.
//
void
ReturnQueryVirtualAdapter(
    MiniportAdapter *VA,
    MCL_QUERY_VIRTUAL_ADAPTER *Query)
{
    if (VA == NULL) {
        Query->Index = (uint)-1;
        RtlZeroMemory(&Query->Guid, sizeof Query->Guid);
    }
    else {
        Query->Index = VA->Index;
        Query->Guid = VA->Guid;
    }
}

//* IoQueryVirtualAdapter
//
//  Handles queries for information about a virtual adapter.
//
NTSTATUS
IoQueryVirtualAdapter(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_VIRTUAL_ADAPTER *Query;
    MCL_INFO_VIRTUAL_ADAPTER *Info;
    MiniportAdapter *VA;
    KIRQL OldIrql;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) ||
        (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof *Info)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Query = (MCL_QUERY_VIRTUAL_ADAPTER *) Irp->AssociatedIrp.SystemBuffer;
    Info = (MCL_INFO_VIRTUAL_ADAPTER *) Irp->AssociatedIrp.SystemBuffer;

    if (Query->Index == (uint)-1) {
        //
        // Return query information for the first virtual adapter.
        //
        KeAcquireSpinLock(&MiniportAdapters.Lock, &OldIrql);
        ReturnQueryVirtualAdapter(MiniportAdapters.VirtualAdapters,
                                  &Info->Next);
        KeReleaseSpinLock(&MiniportAdapters.Lock, OldIrql);

        Irp->IoStatus.Information = sizeof Info->Next;
    }
    else {
        //
        // Return information about the specified virtual adapter.
        //
        VA = FindVirtualAdapterFromQuery(Query);
        if (VA == NULL) {
            Status = STATUS_INVALID_PARAMETER_1;
            goto Return;
        }

        //
        // Return query information for the next virtual adapter.
        //
        KeAcquireSpinLock(&MiniportAdapters.Lock, &OldIrql);
        ReturnQueryVirtualAdapter(VA->Next, &Info->Next);
        KeReleaseSpinLock(&MiniportAdapters.Lock, OldIrql);

        //
        // Return miscellaneous information about the virtual adapter.
        //
        ReturnQueryVirtualAdapter(VA, &Info->This);
        Info->Version = Version;
        Info->MinVersionSeen = VA->MinVersionSeen;
        Info->MaxVersionSeen = VA->MaxVersionSeen;
        RtlCopyMemory(Info->Address, VA->Address, sizeof(VirtualAddress));
        Info->LookAhead = VA->LookAhead;
        Info->Snooping = VA->Snooping;
        Info->ArtificialDrop = VA->ArtificialDrop;
        Info->MetricType = VA->MetricType; 
        Info->RouteFlapDampingFactor = VA->RouteFlapDampingFactor;

        Info->Crypto = VA->Crypto;
        RtlCopyMemory(Info->CryptoKeyMAC, VA->CryptoKeyMAC,
                      sizeof Info->CryptoKeyMAC);
        CryptoKeyMACModify(VA, Info->CryptoKeyMAC);
        RtlCopyMemory(Info->CryptoKeyAES, VA->CryptoKeyAES,
                      sizeof Info->CryptoKeyAES);

        Info->LinkTimeout = (uint)(VA->LinkTimeout / SECOND);

        Info->SendBufSize = VA->SB->Size;
        Info->SendBufHighWater = VA->SB->HighWater;
        Info->SendBufMaxSize = VA->SB->MaxSize;

        Info->ReqTableMinElementReuse = VA->ReqTable->MinElementReuse;
        Info->ReqTableMinSuppressReuse = VA->ReqTable->MinSuppressReuse;

        Info->TotalForwardingStall = VA->TotalForwardingStall;
        Info->ForwardNum = VA->ForwardNum;
        Info->ForwardMax = VA->ForwardMax;
        Info->CountForwardFast = VA->CountForwardFast;
        Info->CountForwardQueue = VA->CountForwardQueue;
        Info->CountForwardDrop = VA->CountForwardDrop;

        switch (VA->MetricType) {
        case METRIC_TYPE_RTT:
            Info->MetricParams.Rtt.Alpha = VA->MetricParams.Rtt.Alpha; 
            Info->MetricParams.Rtt.ProbePeriod = VA->MetricParams.Rtt.ProbePeriod; 
            Info->MetricParams.Rtt.PenaltyFactor = VA->MetricParams.Rtt.PenaltyFactor; 
            Info->MetricParams.Rtt.HysteresisPeriod = VA->MetricParams.Rtt.HysteresisPeriod; 
            Info->MetricParams.Rtt.SweepPeriod = VA->MetricParams.Rtt.SweepPeriod; 
            Info->MetricParams.Rtt.Random = VA->MetricParams.Rtt.Random; 
            Info->MetricParams.Rtt.OutIfOverride = VA->MetricParams.Rtt.OutIfOverride; 
            break;
        case METRIC_TYPE_PKTPAIR:
            Info->MetricParams.PktPair.Alpha = VA->MetricParams.PktPair.Alpha; 
            Info->MetricParams.PktPair.PenaltyFactor = VA->MetricParams.PktPair.PenaltyFactor; 
            Info->MetricParams.PktPair.ProbePeriod = VA->MetricParams.PktPair.ProbePeriod; 
            break;
        case METRIC_TYPE_ETX:
            Info->MetricParams.Etx.ProbePeriod = VA->MetricParams.Etx.ProbePeriod; 
            Info->MetricParams.Etx.LossInterval = VA->MetricParams.Etx.LossInterval; 
            Info->MetricParams.Etx.Alpha = VA->MetricParams.Etx.Alpha; 
            Info->MetricParams.Etx.PenaltyFactor = VA->MetricParams.Etx.PenaltyFactor; 
            break;
        case METRIC_TYPE_WCETT:
            Info->MetricParams.Wcett.ProbePeriod = VA->MetricParams.Wcett.ProbePeriod; 
            Info->MetricParams.Wcett.LossInterval = VA->MetricParams.Wcett.LossInterval; 
            Info->MetricParams.Wcett.Alpha = VA->MetricParams.Wcett.Alpha; 
            Info->MetricParams.Wcett.PenaltyFactor = VA->MetricParams.Wcett.PenaltyFactor; 
            Info->MetricParams.Wcett.Beta = VA->MetricParams.Wcett.Beta;
            Info->MetricParams.Wcett.PktPairProbePeriod = VA->MetricParams.Wcett.PktPairProbePeriod;
            Info->MetricParams.Wcett.PktPairMinOverProbes = VA->MetricParams.Wcett.PktPairMinOverProbes;
            break;
        }

        Info->CountPacketPoolFailure = VA->CountPacketPoolFailure;

        Info->TimeoutMinLoops = VA->TimeoutMinLoops;
        Info->TimeoutMaxLoops = VA->TimeoutMaxLoops;

        Info->PbackAckMaxDupTime = VA->PCache.AckMaxDupTime;
        Info->PbackNumber = VA->PCache.Number;
        Info->PbackHighWater = VA->PCache.HighWater;

        Info->SmallestMetric = VA->LC->SmallestMetric;
        Info->LargestMetric = VA->LC->LargestMetric;
        Info->CountAddLinkInvalidate = VA->LC->CountAddLinkInvalidate;
        Info->CountAddLinkInsignificant = VA->LC->CountAddLinkInsignificant;
        Info->CountRouteFlap = VA->LC->CountRouteFlap;
        Info->CountRouteFlapDamp = VA->LC->CountRouteFlapDamp;

        Info->MaintBufNumPackets = VA->MaintBuf->NumPackets;
        Info->MaintBufHighWater = VA->MaintBuf->HighWater;

        Info->CountPbackTooBig = VA->PCache.CountPbackTooBig;
        Info->CountPbackTotal = VA->PCache.CountPbackTotal;
        Info->CountAloneTotal = VA->PCache.CountAloneTotal;
        Info->CountPbackAck = VA->PCache.CountPbackAck;
        Info->CountAloneAck = VA->PCache.CountAloneAck;
        Info->CountPbackReply = VA->PCache.CountPbackReply;
        Info->CountAloneReply = VA->PCache.CountAloneReply;
        Info->CountPbackError = VA->PCache.CountPbackError;
        Info->CountAloneError = VA->PCache.CountAloneError;
        Info->CountPbackInfo = VA->PCache.CountPbackInfo;
        Info->CountAloneInfo = VA->PCache.CountAloneInfo;

        Info->CountXmit = VA->CountXmit;
        Info->CountXmitLocally = VA->CountXmitLocally;
        Info->CountXmitQueueFull = VA->CountXmitQueueFull;
        Info->CountRexmitQueueFull = VA->CountRexmitQueueFull;
        Info->CountXmitNoRoute = VA->CountXmitNoRoute;
        Info->CountXmitMulticast = VA->CountXmitMulticast;
        Info->CountXmitRouteRequest = VA->CountXmitRouteRequest;
        Info->CountXmitRouteReply = VA->CountXmitRouteReply;
        Info->CountXmitRouteError = VA->CountXmitRouteError;
        Info->CountXmitRouteErrorNoLink = VA->CountXmitRouteErrorNoLink;
        Info->CountXmitSendBuf = VA->CountXmitSendBuf;
        Info->CountXmitMaintBuf = VA->CountXmitMaintBuf;
        Info->CountXmitForwardUnicast = VA->CountXmitForwardUnicast;
        Info->CountXmitForwardQueueFull = VA->CountXmitForwardQueueFull;
        Info->CountXmitForwardBroadcast = VA->CountXmitForwardBroadcast;
        Info->CountXmitInfoRequest = VA->CountXmitInfoRequest;
        Info->CountXmitInfoReply = VA->CountXmitInfoReply;
        Info->CountXmitProbe = VA->CountXmitProbe;
        Info->CountXmitProbeReply = VA->CountXmitProbeReply;
        Info->CountXmitLinkInfo = VA->CountXmitLinkInfo;
        Info->CountLinkInfoTooManyLinks = VA->CountLinkInfoTooManyLinks;
        Info->CountLinkInfoTooLarge = VA->CountLinkInfoTooLarge;
        Info->CountSalvageAttempt = VA->CountSalvageAttempt;
        Info->CountSalvageStatic = VA->CountSalvageStatic;
        Info->CountSalvageOverflow = VA->CountSalvageOverflow;
        Info->CountSalvageNoRoute = VA->CountSalvageNoRoute;
        Info->CountSalvageSameNextHop = VA->CountSalvageSameNextHop;
        Info->CountSalvageTransmit = VA->CountSalvageTransmit;
        Info->CountSalvageQueueFull = VA->CountSalvageQueueFull;
        Info->CountRecv = VA->CountRecv;
        Info->CountRecvLocallySalvaged = VA->CountRecvLocallySalvaged;
        Info->CountRecvLocally = VA->CountRecvLocally;
        Info->CountRecvBadMAC = VA->CountRecvBadMAC;
        Info->CountRecvRouteRequest = VA->CountRecvRouteRequest;
        Info->CountRecvRouteReply = VA->CountRecvRouteReply;
        Info->CountRecvRouteError = VA->CountRecvRouteError;
        Info->CountRecvAckRequest = VA->CountRecvAckRequest;
        Info->CountRecvAck = VA->CountRecvAck;
        Info->CountRecvSourceRoute = VA->CountRecvSourceRoute;
        Info->CountRecvInfoRequest = VA->CountRecvInfoRequest;
        Info->CountRecvInfoReply = VA->CountRecvInfoReply;
        Info->CountRecvDupAckReq = VA->CountRecvDupAckReq;
        Info->CountRecvProbe = VA->CountRecvProbe;
        Info->CountRecvProbeReply = VA->CountRecvProbeReply;
        Info->CountRecvLinkInfo = VA->CountRecvLinkInfo;
        Info->CountRecvRecursive = VA->CountRecvRecursive;
        Info->CountRecvEmpty = VA->CountRecvEmpty;
        Info->CountRecvSmall = VA->CountRecvSmall;
        Info->CountRecvDecryptFailure = VA->CountRecvDecryptFailure;

#if 0
        ReleaseVA(VA);
#endif

        Irp->IoStatus.Information = sizeof *Info;
    }

    Status = STATUS_SUCCESS;
Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* FindPhysicalAdapterFromQuery
//
//  Given an MCL_QUERY_PHYSICAL_ADAPTER structure,
//  finds the specified virtual adapter.
//  The virtual adapter (if found) is returned with a reference.
//
ProtocolAdapter *
FindPhysicalAdapterFromQuery(
    MiniportAdapter *VA,
    MCL_QUERY_PHYSICAL_ADAPTER *Query)
{
    ProtocolAdapter *PA;

    if (Query->Index == 0)
        PA = FindPhysicalAdapterFromGuid(VA, &Query->Guid);
    else
        PA = FindPhysicalAdapterFromIndex(VA, Query->Index);

    return PA;
}

//* ReturnQueryPhysicalAdapter
//
//  Initializes a returned MCL_QUERY_PHYSICAL_ADAPTER structure
//  with query information for the specified physical adapter.
//
void
ReturnQueryPhysicalAdapter(
    ProtocolAdapter *PA,
    MCL_QUERY_PHYSICAL_ADAPTER *Query)
{
    if (PA == NULL) {
        ReturnQueryVirtualAdapter(NULL, &Query->VA);
        Query->Index = (uint)-1;
        RtlZeroMemory(&Query->Guid, sizeof Query->Guid);
    }
    else {
        ReturnQueryVirtualAdapter(PA->VirtualAdapter, &Query->VA);
        Query->Index = PA->Index;
        Query->Guid = PA->Guid;
    }
}

//* IoQueryPhysicalAdapter
//
//  Handles queries for information about a virtual adapter.
//
NTSTATUS
IoQueryPhysicalAdapter(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_PHYSICAL_ADAPTER *Query;
    MCL_INFO_PHYSICAL_ADAPTER *Info;
    MiniportAdapter *VA;
    ProtocolAdapter *PA;
    KIRQL OldIrql;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) ||
        (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof *Info)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Query = (MCL_QUERY_PHYSICAL_ADAPTER *) Irp->AssociatedIrp.SystemBuffer;
    Info = (MCL_INFO_PHYSICAL_ADAPTER *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Query->VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    if (Query->Index == (uint)-1) {
        //
        // Return query information for the first physical adapter.
        //
        KeAcquireSpinLock(&VA->Lock, &OldIrql);
        ReturnQueryPhysicalAdapter(VA->PhysicalAdapters, &Info->Next);
        KeReleaseSpinLock(&VA->Lock, OldIrql);

        Irp->IoStatus.Information = sizeof Info->Next;
    }
    else {
        //
        // Return information about the specified physical adapter.
        //
        PA = FindPhysicalAdapterFromQuery(VA, Query);
        if (PA == NULL) {
            Status = STATUS_INVALID_PARAMETER_2;
            goto ReturnReleaseVA;
        }

        //
        // Return query information for the next physical adapter.
        //
        KeAcquireSpinLock(&VA->Lock, &OldIrql);
        ReturnQueryPhysicalAdapter(PA->Next, &Info->Next);
        KeReleaseSpinLock(&VA->Lock, OldIrql);

        //
        // Return miscellaneous information about the virtual adapter.
        //
        ReturnQueryPhysicalAdapter(PA, &Info->This);
        ProtocolQuery(PA, Info);

#if 0
        ReleasePA(PA);
#endif

        Irp->IoStatus.Information = sizeof *Info;
    }

    ReturnQueryVirtualAdapter(VA, &Info->Next.VA);
    Status = STATUS_SUCCESS;
ReturnReleaseVA:
#if 0
    ReleaseVA(VA);
#endif
Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoQueryNeighborCache
//
//  Handles queries for information about a neighbor cache entry.
//
NTSTATUS
IoQueryNeighborCache(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_NEIGHBOR_CACHE *Query;
    MCL_INFO_NEIGHBOR_CACHE *Info;
    MiniportAdapter *VA = NULL;
    NeighborCacheEntry *NCE;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) ||
        (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof *Info)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    //
    // Note that the Query and Info->Query structures overlap!
    //
    Query = (MCL_QUERY_NEIGHBOR_CACHE *) Irp->AssociatedIrp.SystemBuffer;
    Info = (MCL_INFO_NEIGHBOR_CACHE *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Query->VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    if (IsUnspecified(Query->Address)) {
        //
        // Return the address of the first NCE.
        //
        KeAcquireSpinLock(&VA->NC.Lock, &OldIrql);
        if (VA->NC.FirstNCE != SentinelNCE(&VA->NC)) {
            RtlCopyMemory(Info->Query.Address, VA->NC.FirstNCE->VAddress,
                          sizeof(VirtualAddress));
            Info->Query.InIF = VA->NC.FirstNCE->InIf;
        }
        KeReleaseSpinLock(&VA->NC.Lock, OldIrql);

        Irp->IoStatus.Information = sizeof Info->Query;
    }
    else {
        //
        // Find the specified NCE.
        //
        KeAcquireSpinLock(&VA->NC.Lock, &OldIrql);
        for (NCE = VA->NC.FirstNCE; ; NCE = NCE->Next) {
            if (NCE == SentinelNCE(&VA->NC)) {
                KeReleaseSpinLock(&VA->NC.Lock, OldIrql);
                Status = STATUS_INVALID_PARAMETER_2;
                goto Return;
            }

            if (VirtualAddressEqual(Query->Address, NCE->VAddress) &&
                (Query->InIF == NCE->InIf))
                break;
        }

        //
        // Return miscellaneous information about the NCE.
        //
        RtlCopyMemory(Info->Address, NCE->PAddress, sizeof(PhysicalAddress));

        //
        // Return address of the next NCE (or zero).
        //
        if (NCE->Next == SentinelNCE(&VA->NC)) {
            RtlZeroMemory(Info->Query.Address, sizeof(VirtualAddress));
            Info->Query.InIF = (uint)-1;
        }
        else {
            RtlCopyMemory(Info->Query.Address, NCE->Next->VAddress,
                          sizeof(VirtualAddress));
            Info->Query.InIF = NCE->Next->InIf;
        }

        KeReleaseSpinLock(&VA->NC.Lock, OldIrql);

        Irp->IoStatus.Information = sizeof *Info;
    }

    Status = STATUS_SUCCESS;
Return:
#if 0
    if (VA != NULL)
        ReleaseVA(VA);
#endif
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;

}

//* IoFlushNeighborCache
//
//  Handles requests to flush the neighbor cache.
//
NTSTATUS
IoFlushNeighborCache(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_NEIGHBOR_CACHE *Query;
    MiniportAdapter *VA;
    const uchar *Address;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Query = (MCL_QUERY_NEIGHBOR_CACHE *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Find the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Query->VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    if (IsUnspecified(Query->Address))
        Address = NULL;
    else
        Address = Query->Address;

    NeighborCacheFlushAddress(&VA->NC, Address, (LQSRIf) Query->InIF);
#if 0
    ReleaseVA(VA);
#endif
    Status = STATUS_SUCCESS;

Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoQueryCacheNode
//
//  Handles queries for information about a node in the link cache.
//
NTSTATUS
IoQueryCacheNode(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_CACHE_NODE *Query;
    MCL_INFO_CACHE_NODE *Info;
    MCL_INFO_LINK *LInfo;
    MiniportAdapter *VA = NULL;
    KIRQL OldIrql;
    NTSTATUS Status;
    uint n;
    CacheNode *node;
    Link *link;
    uint i;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) ||
        (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof *Info)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    //
    // Note that the Query and Info->Query structures overlap!
    //
    Query = (MCL_QUERY_CACHE_NODE *) Irp->AssociatedIrp.SystemBuffer;
    Info = (MCL_INFO_CACHE_NODE *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Query->VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    if (IsUnspecified(Query->Address)) {
        //
        // Return the address of the first node in the link cache.
        // There is always at least one node: this node.
        //
        KeAcquireSpinLock(&VA->LC->Lock, &OldIrql);
        RtlCopyMemory(Info->Query.Address, VA->LC->nodes[0].address,
                      sizeof(VirtualAddress));
        KeReleaseSpinLock(&VA->LC->Lock, OldIrql);

        Irp->IoStatus.Information = sizeof Info->Query;
    }
    else {
        //
        // Find the specified node.
        //
        KeAcquireSpinLock(&VA->LC->Lock, &OldIrql);
        for (n = 0; ; n++) {
            if (n == VA->LC->nodeCount) {
                KeReleaseSpinLock(&VA->LC->Lock, OldIrql);
                Status = STATUS_INVALID_PARAMETER_2;
                goto Return;
            }

            if (VirtualAddressEqual(Query->Address, VA->LC->nodes[n].address))
                break;
        }

        //
        // We always return the fixed-size information.
        // Then if there is room, we return additional info.
        //
        Irp->IoStatus.Information = sizeof *Info;

        //
        // Return miscellaneous information about the node.
        //
        node = &VA->LC->nodes[n];

        Info->DijkstraMetric = VA->LC->metric[n];
        Info->CachedMetric = node->Metric;
        Info->Hops = VA->LC->hops[n];
        Info->Previous = VA->LC->prev[n];
        Info->RouteChanges = node->RouteChanges;

        //
        // Count the number of links from this node.
        //
        Info->LinkCount = 0;
        for (link = node->AdjOut; link != NULL; link = link->NextOut)
            Info->LinkCount++;

        //
        // Return address of the next node (or zero).
        //
        if (++n == VA->LC->nodeCount)
            RtlZeroMemory(Info->Query.Address, sizeof(VirtualAddress));
        else
            RtlCopyMemory(Info->Query.Address, VA->LC->nodes[n].address,
                          sizeof(VirtualAddress));

        //
        // If there is room, return information about the links.
        //
        if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength >=
                sizeof *Info + Info->LinkCount * sizeof *LInfo) {
            Time Delta = IoTimeDelta();

            Irp->IoStatus.Information += Info->LinkCount * sizeof *LInfo;

            LInfo = Info->Links;
            for (link = node->AdjOut; link != NULL; link = link->NextOut) {

                RtlCopyMemory(LInfo->To,
                              VA->LC->nodes[link->targetIndex].address,
                              sizeof(VirtualAddress));

                LInfo->FromIF = link->outif;
                LInfo->ToIF = link->inif;
                LInfo->Metric = link->Metric;
                LInfo->TimeStamp = link->TimeStamp + Delta;
                LInfo->Usage = link->Usage;
                LInfo->Failures = link->Failures;
                LInfo->DropRatio = link->DropRatio;
                LInfo->ArtificialDrops = link->ArtificialDrops;
                LInfo->QueueDrops = link->QueueDrops;

                switch (VA->MetricType) {
                case METRIC_TYPE_RTT:
                    LInfo->MetricInfo.Rtt.SentProbes = link->MetricInfo.Rtt.SentProbes;
                    LInfo->MetricInfo.Rtt.LostProbes = link->MetricInfo.Rtt.LostProbes;
                    LInfo->MetricInfo.Rtt.LateProbes = link->MetricInfo.Rtt.LateProbes;
                    LInfo->MetricInfo.Rtt.RawMetric = link->MetricInfo.Rtt.RawMetric;
                    LInfo->MetricInfo.Rtt.LastRTT = link->MetricInfo.Rtt.LastRTT;
                    break;
                case METRIC_TYPE_PKTPAIR:
                    LInfo->MetricInfo.PktPair.PairsSent = link->MetricInfo.PktPair.PairsSent;
                    LInfo->MetricInfo.PktPair.RepliesSent = link->MetricInfo.PktPair.RepliesSent;
                    LInfo->MetricInfo.PktPair.RepliesRcvd = link->MetricInfo.PktPair.RepliesRcvd;
                    LInfo->MetricInfo.PktPair.LostPairs = link->MetricInfo.PktPair.LostPairs;
                    LInfo->MetricInfo.PktPair.RTT = link->MetricInfo.PktPair.RTT;
                    LInfo->MetricInfo.PktPair.LastRTT = link->MetricInfo.PktPair.LastRTT;
                    LInfo->MetricInfo.PktPair.LastPktPair = link->MetricInfo.PktPair.LastPktPair;
                    LInfo->MetricInfo.PktPair.CurrMin = link->MetricInfo.PktPair.CurrMin;
                    LInfo->MetricInfo.PktPair.NumSamples = link->MetricInfo.PktPair.NumSamples;
                    for (i = 0; i < MIN(MAX_PKTPAIR_HISTORY, MCL_INFO_MAX_PKTPAIR_HISTORY); i++) {
                        LInfo->MetricInfo.PktPair.MinHistory[i].Min = link->MetricInfo.PktPair.MinHistory[i].Min;
                        LInfo->MetricInfo.PktPair.MinHistory[i].Seq = link->MetricInfo.PktPair.MinHistory[i].Seq;
                    }
                    break;
                case METRIC_TYPE_ETX:
                    LInfo->MetricInfo.Etx.TotSentProbes = link->MetricInfo.Etx.TotSentProbes;
                    LInfo->MetricInfo.Etx.TotRcvdProbes = link->MetricInfo.Etx.TotRcvdProbes;
                    LInfo->MetricInfo.Etx.FwdDeliv = link->MetricInfo.Etx.FwdDeliv;
                    LInfo->MetricInfo.Etx.ProbeHistorySZ = link->MetricInfo.Etx.ProbeHistorySZ;
                    break;
                case METRIC_TYPE_WCETT:
                    LInfo->MetricInfo.Wcett.TotSentProbes = link->MetricInfo.Wcett.Etx.TotSentProbes;
                    LInfo->MetricInfo.Wcett.TotRcvdProbes = link->MetricInfo.Wcett.Etx.TotRcvdProbes;
                    LInfo->MetricInfo.Wcett.FwdDeliv = link->MetricInfo.Wcett.Etx.FwdDeliv;
                    LInfo->MetricInfo.Wcett.ProbeHistorySZ = link->MetricInfo.Wcett.Etx.ProbeHistorySZ;
                    LInfo->MetricInfo.Wcett.LastProb = link->MetricInfo.Wcett.Etx.LastProb;
                    
                    LInfo->MetricInfo.Wcett.PairsSent = link->MetricInfo.Wcett.PktPair.PairsSent;
                    LInfo->MetricInfo.Wcett.RepliesSent = link->MetricInfo.Wcett.PktPair.RepliesSent;
                    LInfo->MetricInfo.Wcett.RepliesRcvd = link->MetricInfo.Wcett.PktPair.RepliesRcvd;
                    LInfo->MetricInfo.Wcett.LastPktPair = link->MetricInfo.Wcett.PktPair.LastPktPair;
                    LInfo->MetricInfo.Wcett.CurrMin = link->MetricInfo.Wcett.PktPair.CurrMin;
                    LInfo->MetricInfo.Wcett.NumPktPairValid = link->MetricInfo.Wcett.NumPktPairValid;
                    LInfo->MetricInfo.Wcett.NumPktPairInvalid = link->MetricInfo.Wcett.NumPktPairInvalid;
                    break;
                }

                LInfo++;
            }
        }

        KeReleaseSpinLock(&VA->LC->Lock, OldIrql);
    }

    Status = STATUS_SUCCESS;
Return:
#if 0
    if (VA != NULL)
        ReleaseVA(VA);
#endif
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;

}

//* ReturnSourceRoute
//
//  Copies an SRAddr array to an MCL_INFO_SOURCE_ROUTE_HOP array.
//
static void
ReturnSourceRoute(
    uint NumHops,
    MCL_INFO_SOURCE_ROUTE_HOP *Hop,
    SRAddr *Route)
{
    uint i;

    for (i = 0; i < NumHops; i++, Hop++, Route++) {

        RtlCopyMemory(Hop->Address, Route->addr, sizeof(VirtualAddress));
        Hop->InIF = Route->inif;
        Hop->OutIF = Route->outif;
        Hop->Metric = Route->Metric;
    }
}

#if CHANGELOGS
//* IoQueryRouteUsage
//
//  Handles queries for route usage history for a node in the link cache.
//
NTSTATUS
IoQueryRouteUsage(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_CACHE_NODE *Query;
    MCL_INFO_ROUTE_USAGE *History;
    MiniportAdapter *VA = NULL;
    KIRQL OldIrql;
    NTSTATUS Status;
    uint n;
    uint BytesLeft;
    uint SizeNeeded;
    RouteUsage *RU;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    //
    // Note that the Query and History structures overlap!
    //
    Query = (MCL_QUERY_CACHE_NODE *) Irp->AssociatedIrp.SystemBuffer;
    History = (MCL_INFO_ROUTE_USAGE *) Irp->AssociatedIrp.SystemBuffer;
    BytesLeft = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Query->VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    //
    // Find the specified node.
    //
    KeAcquireSpinLock(&VA->LC->Lock, &OldIrql);
    for (n = 0; ; n++) {
        if (n == VA->LC->nodeCount) {
            Status = STATUS_INVALID_PARAMETER_2;
            goto ReturnUnlock;
        }

        if (VirtualAddressEqual(Query->Address, VA->LC->nodes[n].address))
            break;
    }

    for (RU = VA->LC->nodes[n].History; RU != NULL; RU = RU->Next) {
        SizeNeeded = sizeof *History + RU->Hops * sizeof History->Hops[0];
        if (SizeNeeded > BytesLeft) {
            Status = STATUS_BUFFER_OVERFLOW;
            goto ReturnUnlock;
        }

        History->Usage = RU->Usage;
        History->NumHops = RU->Hops;
        ReturnSourceRoute(History->NumHops, History->Hops, RU->Route);

        (uchar *)History += SizeNeeded;
        Irp->IoStatus.Information += SizeNeeded;
        BytesLeft -= SizeNeeded;
    }

    Status = STATUS_SUCCESS;
ReturnUnlock:
    KeReleaseSpinLock(&VA->LC->Lock, OldIrql);
Return:
#if 0
    if (VA != NULL)
        ReleaseVA(VA);
#endif
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;

}
#endif // CHANGELOGS

//* IoAddLink
//
//  Handles requests to add a link to the link cache.
//
NTSTATUS
IoAddLink(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_ADD_LINK *Request;
    MiniportAdapter *VA = NULL;
    LQSRIf FromIF, ToIF;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Request) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Request = (MCL_ADD_LINK *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Find the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Request->Node.VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    //
    // Convert the physical adapter indices to the internal
    // representation used in the link cache.
    //

    if (Request->FromIF > MAXUCHAR) {
        Status = STATUS_INVALID_PARAMETER_2;
        goto Return;
    }
    FromIF = (LQSRIf) Request->FromIF;

    if (Request->ToIF > MAXUCHAR) {
        Status = STATUS_INVALID_PARAMETER_3;
        goto Return;
    }
    ToIF = (LQSRIf) Request->ToIF;

    //
    // Add the link to the link cache.
    //
    LinkCacheAddLink(VA,
                     Request->Node.Address, Request->To,
                     ToIF, FromIF, 0,
                     LINK_STATE_CHANGE_ADD_MANUAL);

    Status = STATUS_SUCCESS;
Return:
#if 0
    if (VA != NULL)
        ReleaseVA(VA);
#endif
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoFlushLinkCache
//
//  Handles requests to flush the link cache.
//
NTSTATUS
IoFlushLinkCache(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_VIRTUAL_ADAPTER *Request;
    MiniportAdapter *VA;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Request) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Request = (MCL_QUERY_VIRTUAL_ADAPTER *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Find the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(Request);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    LinkCacheFlush(VA);
#if 0
    ReleaseVA(VA);
#endif
    Status = STATUS_SUCCESS;

Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoQuerySourceRoute
//
//  Handles queries for a route to a node in the link cache.
//
NTSTATUS
IoQuerySourceRoute(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_CACHE_NODE *Query;
    MCL_INFO_SOURCE_ROUTE *Info;
    InternalSourceRoute *SR;
    MiniportAdapter *VA = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    //
    // It is OK to allocate a small object on the stack
    // at this point.
    //
#pragma prefast(suppress:255, "consider using an exception handler")
    SR = alloca(sizeof *SR + sizeof(SRAddr)*MAX_SR_LEN);

    Irp->IoStatus.Information = 0;

    if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) ||
        (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof *Info)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    //
    // Note that the Query and Info->Query structures overlap!
    //
    Query = (MCL_QUERY_CACHE_NODE *) Irp->AssociatedIrp.SystemBuffer;
    Info = (MCL_INFO_SOURCE_ROUTE *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Query->VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    //
    // Get the route to the specified node.
    //
    Status = LinkCacheGetSR(VA, Query->Address, SR);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Return;

    //
    // We always return the fixed-size information.
    // Then if there is room, we return additional info.
    //
    Irp->IoStatus.Information = sizeof *Info;

    //
    // Return miscellaneous information about the source route.
    //
    Info->Static = SR->opt.staticRoute;
    Info->NumHops = SOURCE_ROUTE_HOPS(SR->opt.optDataLen);

    //
    // If there is room, return information about the hops.
    //
    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength >=
            sizeof *Info + Info->NumHops * sizeof Info->Hops[0]) {

        Irp->IoStatus.Information += Info->NumHops * sizeof Info->Hops[0];

        ReturnSourceRoute(Info->NumHops, Info->Hops, SR->opt.hopList);
    }

    Status = STATUS_SUCCESS;
Return:
#if 0
    if (VA != NULL)
        ReleaseVA(VA);
#endif
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoQueryLinkCache
//
//  Handles queries for information about a virtual adapter's link cache.
//
NTSTATUS
IoQueryLinkCache(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_VIRTUAL_ADAPTER *Query;
    MCL_INFO_LINK_CACHE *Info;
    MiniportAdapter *VA;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) ||
        (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof *Info)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Query = (MCL_QUERY_VIRTUAL_ADAPTER *) Irp->AssociatedIrp.SystemBuffer;
    Info = (MCL_INFO_LINK_CACHE *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(Query);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    //
    // Return miscellaneous information about the link cache.
    //
    Info->NumNodes = VA->LC->nodeCount;
    Info->MaxNodes = VA->LC->maxSize;
    Info->Timeout = VA->LC->timeout + IoTimeDelta();

#if 0
    ReleaseVA(VA);
#endif

    Irp->IoStatus.Information = sizeof *Info;

    Status = STATUS_SUCCESS;
Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

#if CHANGELOGS
static void
ReturnLinkChangeRecord(
    MCL_INFO_LINK_CACHE_CHANGE *Info,
    struct LinkChange *Record)
{
    Info->TimeStamp = Record->TimeStamp;
    RtlCopyMemory(Info->From, Record->From, sizeof(VirtualAddress));
    RtlCopyMemory(Info->To, Record->To, sizeof(VirtualAddress));
    Info->FromIF = Record->OutIf;
    Info->ToIF = Record->InIf;
    Info->Metric = Record->Metric;
    Info->Reason = Record->Reason;
}

//* IoQueryLinkCacheChangeLog
//
//  Handles queries for records from a link cache change log.
//
NTSTATUS
IoQueryLinkCacheChangeLog(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_LINK_CACHE_CHANGE_LOG *Query;
    MCL_INFO_LINK_CACHE_CHANGE_LOG *Info;
    MiniportAdapter *VA;
    KIRQL OldIrql;
    NTSTATUS Status;
    uint Index;
    uint NumRecords;
    uint NumBytes;
    uint i;

    Irp->IoStatus.Information = 0;

    if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) ||
        (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof *Info)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Query = (MCL_QUERY_LINK_CACHE_CHANGE_LOG *)
        Irp->AssociatedIrp.SystemBuffer;
    Info = (MCL_INFO_LINK_CACHE_CHANGE_LOG *)
        Irp->AssociatedIrp.SystemBuffer;

    if ((Query->Index >= NUM_LINKCHANGE_RECORDS) &&
        (Query->Index != (uint)-1)) {
        Status = STATUS_INVALID_PARAMETER_2;
        goto Return;
    }

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Query->VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    KeAcquireSpinLock(&VA->LC->Lock, &OldIrql);

    Index = Query->Index;
    Info->NextIndex = VA->LC->NextLinkRecord;

    //
    // If the requested Index is -1, we just return the current Index.
    //
    if (Index == (uint)-1) {
        NumBytes = sizeof *Info;
        goto ReturnInfo;
    }

    NumRecords = VA->LC->NextLinkRecord - Index;
    if ((int)NumRecords < 0)
        NumRecords += NUM_LINKCHANGE_RECORDS;

    NumBytes = sizeof *Info + sizeof Info->Changes[0] * NumRecords;
    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < NumBytes) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto ReturnUnlock;
    }

    if (VA->LC->NextLinkRecord < Index) {
        uint FirstPart = NUM_LINKCHANGE_RECORDS - Index;

        for (i = 0; i < FirstPart; i++)
            ReturnLinkChangeRecord(&Info->Changes[i],
                                   &VA->LC->LinkChangeLog[Index + i]);

        for (i = FirstPart; i < NumRecords; i++)
            ReturnLinkChangeRecord(&Info->Changes[i],
                                   &VA->LC->LinkChangeLog[i - FirstPart]);
    }
    else {
        for (i = 0; i < NumRecords; i++)
            ReturnLinkChangeRecord(&Info->Changes[i],
                                   &VA->LC->LinkChangeLog[Index + i]);
    }

ReturnInfo:
    Irp->IoStatus.Information = NumBytes;
    Status = STATUS_SUCCESS;

ReturnUnlock:
    KeReleaseSpinLock(&VA->LC->Lock, OldIrql);
#if 0
    ReleaseVA(VA);
#endif

Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}
#endif // CHANGELOGS

//* ReturnQueryMaintenanceBufferNode
//
//  Initializes a returned MCL_QUERY_MAINTENANCE_BUFFER_NODE structure
//  with query information for the specified maintenance buffer node.
//  Does NOT initialize Query->VA.
//
void
ReturnQueryMaintenanceBufferNode(
    MaintBufNode *MBN,
    MCL_QUERY_MAINTENANCE_BUFFER_NODE *Query)
{
    if (MBN == NULL) {
        RtlZeroMemory(Query->Node.Address, SR_ADDR_LEN);
        Query->Node.InIF = 0;
        Query->Node.OutIF = 0;
    }
    else {
        RtlCopyMemory(Query->Node.Address, MBN->Address, SR_ADDR_LEN);
        Query->Node.InIF = MBN->InIf;
        Query->Node.OutIF = MBN->OutIf;
    }
}

//* IoQueryMaintenanceBuffer
//
//  Handles queries for information about the maintenance buffer.
//
NTSTATUS
IoQueryMaintenanceBuffer(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_MAINTENANCE_BUFFER_NODE *Query;
    MCL_INFO_MAINTENANCE_BUFFER_NODE *Info;
    MiniportAdapter *VA;
    MaintBufNode *MBN;
    Time Delta;
    KIRQL OldIrql;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) ||
        (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof *Info)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Query = (MCL_QUERY_MAINTENANCE_BUFFER_NODE *)
        Irp->AssociatedIrp.SystemBuffer;
    Info = (MCL_INFO_MAINTENANCE_BUFFER_NODE *)
        Irp->AssociatedIrp.SystemBuffer;

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Query->VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    if (IsUnspecified(Query->Node.Address)) {
        //
        // Return query information for the first maintenance buffer node.
        //
        KeAcquireSpinLock(&VA->MaintBuf->Lock, &OldIrql);
        ReturnQueryMaintenanceBufferNode(VA->MaintBuf->MBN, &Info->Query);
        KeReleaseSpinLock(&VA->MaintBuf->Lock, OldIrql);

        Irp->IoStatus.Information = sizeof Info->Query;
    }
    else {
        //
        // Return information about the specified maintenance buffer node.
        //
        KeAcquireSpinLock(&VA->MaintBuf->Lock, &OldIrql);
        for (MBN = VA->MaintBuf->MBN; ; MBN = MBN->Next) {
            if (MBN == NULL) {
                KeReleaseSpinLock(&VA->MaintBuf->Lock, OldIrql);
                Status = STATUS_INVALID_PARAMETER_2;
                goto ReturnReleaseVA;
            }

            if (RtlEqualMemory(MBN->Address,
                               Query->Node.Address, SR_ADDR_LEN) &&
                (MBN->InIf == Query->Node.InIF) &&
                (MBN->OutIf == Query->Node.OutIF))
                break;
        }

        //
        // Return query information for the next maintenance buffer node.
        //
        ReturnQueryMaintenanceBufferNode(MBN->Next, &Info->Query);

        //
        // Return miscellaneous information about the maintenance buffer node.
        //
        Delta = IoTimeDelta();
        Info->NextAckNum = MBN->NextAckNum;
        Info->FirstAckReq = MBN->FirstAckReq + Delta;
        Info->LastAckReq = MBN->LastAckReq + Delta;
        Info->LastAckNum = MBN->LastAckNum;
        Info->LastAckRcv = MBN->LastAckRcv + Delta;
        Info->NumPackets = MBN->NumPackets;
        Info->HighWater = MBN->HighWater;
        Info->NumAckReqs = MBN->NumAckReqs;
        Info->NumFastReqs = MBN->NumFastReqs;
        Info->NumValidAcks = MBN->NumValidAcks;
        Info->NumInvalidAcks = MBN->NumInvalidAcks;
        KeReleaseSpinLock(&VA->MaintBuf->Lock, OldIrql);

        Irp->IoStatus.Information = sizeof *Info;
    }

    ReturnQueryVirtualAdapter(VA, &Info->Query.VA);
    Status = STATUS_SUCCESS;
ReturnReleaseVA:
#if 0
    ReleaseVA(VA);
#endif
Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoInformationRequest
//
//  Initiates an LQSR Information Request.
//
NTSTATUS
IoInformationRequest(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_VIRTUAL_ADAPTER *Query;
    MiniportAdapter *VA;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Query = (MCL_QUERY_VIRTUAL_ADAPTER *)
        Irp->AssociatedIrp.SystemBuffer;

    //
    // Use the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(Query);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    MiniportSendInfoRequest(VA);

    Status = STATUS_SUCCESS;
#if 0
    ReleaseVA(VA);
#endif
Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoResetStatistics
//
//  Resets our counters and statistics gathering.
//
NTSTATUS
IoResetStatistics(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_VIRTUAL_ADAPTER *Query;
    MiniportAdapter *VA;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Query = (MCL_QUERY_VIRTUAL_ADAPTER *)
        Irp->AssociatedIrp.SystemBuffer;

    //
    // Use the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(Query);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    MiniportResetStatistics(VA);

    Status = STATUS_SUCCESS;
#if 0
    ReleaseVA(VA);
#endif
Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoControlVirtualAdapter
//
//  Handles requests to change attributes of a virtual adapter.
//
NTSTATUS
IoControlVirtualAdapter(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp,
    IN boolint Persistent)
{
    MCL_CONTROL_VIRTUAL_ADAPTER *Control;
    MiniportAdapter *VA;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Control) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Control = (MCL_CONTROL_VIRTUAL_ADAPTER *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Get the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Control->This);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    if (Persistent)
        Status = MiniportPersistControl(VA, Control);
    else
        Status = MiniportControl(VA, Control);

Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoControlPhysicalAdapter
//
//  Handles requests to change attributes of a physical adapter.
//
NTSTATUS
IoControlPhysicalAdapter(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_CONTROL_PHYSICAL_ADAPTER *Control;
    MiniportAdapter *VA;
    ProtocolAdapter *PA;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Control) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Control = (MCL_CONTROL_PHYSICAL_ADAPTER *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Get the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Control->This.VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    //
    // Get the specified physical adapter.
    //
    PA = FindPhysicalAdapterFromQuery(VA, &Control->This);
    if (PA == NULL) {
        Status = STATUS_INVALID_PARAMETER_2;
        goto ReturnReleaseVA;
    }

    Status = ProtocolControl(PA, Control);

#if 0
    ReleasePA(PA);
#endif
ReturnReleaseVA:
#if 0
    ReleaseVA(VA);
#endif
Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoAddSourceRoute
//
//  Adds a static route to the link cache.
//
NTSTATUS
IoAddSourceRoute(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_INFO_SOURCE_ROUTE *StaticSR;
    MCL_INFO_SOURCE_ROUTE_HOP *Hop;
    MiniportAdapter *VA;
    SourceRoute *SR;
    NTSTATUS Status;
    uint i;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof *StaticSR) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    StaticSR = (MCL_INFO_SOURCE_ROUTE *) Irp->AssociatedIrp.SystemBuffer;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *StaticSR + StaticSR->NumHops * sizeof *Hop) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&StaticSR->Query.VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    //
    // We only accept static routes.
    //
    if (! StaticSR->Static) {
        Status = STATUS_INVALID_PARAMETER_2;
        goto ReleaseAndReturn;
    }

    //
    // Sanity-check the number of hops.
    //
    if ((StaticSR->NumHops > MAX_SR_LEN) || (StaticSR->NumHops <= 0)) {
        Status = STATUS_INVALID_PARAMETER_3;
        goto ReleaseAndReturn;
    }

    //
    // Sanity-check the interface indices.
    //
    if ((StaticSR->Hops[0].InIF != 0) ||
        (StaticSR->Hops[StaticSR->NumHops - 1].OutIF != 0)) {
        Status = STATUS_INVALID_PARAMETER_3;
        goto ReleaseAndReturn;
    }

    for (i = 0; i < StaticSR->NumHops; i++) {
        Hop = &StaticSR->Hops[i];
        if ((Hop->InIF > MAXUCHAR) || (Hop->OutIF > MAXUCHAR)) {
            Status = STATUS_INVALID_PARAMETER_3;
            goto ReleaseAndReturn;
        }
    }

    //
    // Sanity-check the first address.
    //
    if (! VirtualAddressEqual(StaticSR->Hops[0].Address, VA->Address)) {
        Status = STATUS_INVALID_PARAMETER_4;
        goto ReleaseAndReturn;
    }

    //
    // Sanity-check the last address.
    //
    if (! VirtualAddressEqual(StaticSR->Hops[StaticSR->NumHops - 1].Address,
                              StaticSR->Query.Address)) {
        Status = STATUS_INVALID_PARAMETER_5;
        goto ReleaseAndReturn;
    }

    //
    // Allocate the source route.
    //
    SR = ExAllocatePool(NonPagedPool,
                        sizeof *SR + sizeof(SRAddr)*StaticSR->NumHops);
    if (SR == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ReleaseAndReturn;
    }

    //
    // Initialize the source route.
    //
    SR->optionType = LQSR_OPTION_TYPE_SOURCERT;
    SR->optDataLen = SOURCE_ROUTE_LEN(StaticSR->NumHops);
    SR->reservedField = 0;
    SR->staticRoute = 1;
    SR->salvageCount = 0;
    SR->segmentsLeft = (uchar) (StaticSR->NumHops - 1);

    for (i = 0; i < StaticSR->NumHops; i++) {
        Hop = &StaticSR->Hops[i];

        RtlCopyMemory(SR->hopList[i].addr, Hop->Address,
                      sizeof(VirtualAddress));
        SR->hopList[i].inif = (LQSRIf) Hop->InIF;
        SR->hopList[i].outif = (LQSRIf) Hop->OutIF;
        SR->hopList[i].Metric = 0;
    }

    //
    // Insert the source route.
    //
    Status = LinkCacheAddRoute(VA, StaticSR->Query.Address, SR);

ReleaseAndReturn:
#if 0
    ReleaseVA(VA);
#endif
Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

#if CHANGELOGS
static void
ReturnRouteChangeRecord(
    MCL_INFO_ROUTE_CACHE_CHANGE *Info,
    struct RouteChange *Record)
{
    LQSROption *Opt = (LQSROption UNALIGNED *) Record->Buffer;
    uint Length;

    Info->TimeStamp = Record->TimeStamp;
    RtlCopyMemory(Info->Dest, Record->Dest, sizeof(VirtualAddress));
    Info->Metric = Record->Metric;
    Info->PrevMetric = Record->PrevMetric;

    if (Opt->optionType == 0)
        Length = 0;
    else
        Length = sizeof *Opt + Opt->optDataLen;
    ASSERT(Length <= sizeof Info->Buffer);

    RtlCopyMemory(Info->Buffer, Record->Buffer, Length);
    RtlZeroMemory(Info->Buffer + Length, sizeof Info->Buffer - Length);
}

//* IoQueryRouteCacheChangeLog
//
//  Handles queries for records from a route cache change log.
//
NTSTATUS
IoQueryRouteCacheChangeLog(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_QUERY_ROUTE_CACHE_CHANGE_LOG *Query;
    MCL_INFO_ROUTE_CACHE_CHANGE_LOG *Info;
    MiniportAdapter *VA;
    KIRQL OldIrql;
    NTSTATUS Status;
    uint Index;
    uint NumRecords;
    uint NumBytes;
    uint i;

    Irp->IoStatus.Information = 0;

    if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Query) ||
        (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof *Info)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Query = (MCL_QUERY_ROUTE_CACHE_CHANGE_LOG *)
        Irp->AssociatedIrp.SystemBuffer;
    Info = (MCL_INFO_ROUTE_CACHE_CHANGE_LOG *)
        Irp->AssociatedIrp.SystemBuffer;

    if ((Query->Index >= NUM_ROUTECHANGE_RECORDS) &&
        (Query->Index != (uint)-1)) {
        Status = STATUS_INVALID_PARAMETER_2;
        goto Return;
    }

    //
    // Return information about the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Query->VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    KeAcquireSpinLock(&VA->LC->Lock, &OldIrql);

    Index = Query->Index;
    Info->NextIndex = VA->LC->NextRouteRecord;

    //
    // If the requested Index is -1, we just return the current Index.
    //
    if (Index == (uint)-1) {
        NumBytes = sizeof *Info;
        goto ReturnInfo;
    }

    //
    // We return records from Index up to NextRouteRecord.
    // We have wrap-around if NextRouteRecord is smaller than Index.
    // For example (assuming NUM_ROUTECHANGE_RECORDS is 16),
    // If Index == 3 and NextRouteRecord == 7, return records 3,4,5,6.
    // If Index == 13 and NextRouteRecord == 2, return 13,14,15,0,1.
    //
    NumRecords = VA->LC->NextRouteRecord - Index;
    if ((int)NumRecords < 0)
        NumRecords += NUM_ROUTECHANGE_RECORDS;

    NumBytes = sizeof *Info + sizeof Info->Changes[0] * NumRecords;
    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < NumBytes) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto ReturnUnlock;
    }

    if (VA->LC->NextRouteRecord < Index) {
        uint FirstPart = NUM_ROUTECHANGE_RECORDS - Index;

        //
        // Return the records from the end of the change log.
        //
        for (i = 0; i < FirstPart; i++)
            ReturnRouteChangeRecord(&Info->Changes[i],
                                    &VA->LC->RouteChangeLog[Index + i]);

        //
        // Then the records from the beginning of the change log.
        //
        for (i = FirstPart; i < NumRecords; i++)
            ReturnRouteChangeRecord(&Info->Changes[i],
                                    &VA->LC->RouteChangeLog[i - FirstPart]);
    }
    else {
        //
        // No wrap-around, so this is easy.
        //
        for (i = 0; i < NumRecords; i++)
            ReturnRouteChangeRecord(&Info->Changes[i],
                                    &VA->LC->RouteChangeLog[Index + i]);
    }

ReturnInfo:
    Irp->IoStatus.Information = NumBytes;
    Status = STATUS_SUCCESS;

ReturnUnlock:
    KeReleaseSpinLock(&VA->LC->Lock, OldIrql);
#if 0
    ReleaseVA(VA);
#endif

Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}
#endif // CHANGELOGS

//* IoControlLink
//
//  Handles requests to change attributes of a link.
//
NTSTATUS
IoControlLink(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    MCL_CONTROL_LINK *Request;
    MiniportAdapter *VA = NULL;
    LQSRIf FromIF, ToIF;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != sizeof *Request) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Request = (MCL_CONTROL_LINK *) Irp->AssociatedIrp.SystemBuffer;

    //
    // Find the specified virtual adapter.
    //
    VA = FindVirtualAdapterFromQuery(&Request->Node.VA);
    if (VA == NULL) {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Return;
    }

    //
    // Convert the physical adapter indices to the internal
    // representation used in the link cache.
    //

    if (Request->FromIF > MAXUCHAR) {
        Status = STATUS_INVALID_PARAMETER_2;
        goto Return;
    }
    FromIF = (LQSRIf) Request->FromIF;

    if (Request->ToIF > MAXUCHAR) {
        Status = STATUS_INVALID_PARAMETER_3;
        goto Return;
    }
    ToIF = (LQSRIf) Request->ToIF;

    //
    // Modify link attributes.
    //
    Status = LinkCacheControlLink(VA,
                                  Request->Node.Address, Request->To,
                                  ToIF, FromIF, Request->DropRatio);

Return:
#if 0
    if (VA != NULL)
        ReleaseVA(VA);
#endif
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoGenerateRandom
//
//  Handles requests to generate random bits.
//
NTSTATUS
IoGenerateRandom(
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp)
{
    uchar *Buffer;
    uint Length;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength != 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Return;
    }

    Buffer = Irp->AssociatedIrp.SystemBuffer;
    Length = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (GetSystemRandomBits(Buffer, Length) != NDIS_STATUS_SUCCESS) {
        Status = STATUS_UNSUCCESSFUL;
        goto Return;
    }

    Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = Length;
Return:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

//* IoControl
//
//  Handles the IRP_MJ_DEVICE_CONTROL request for the MCL device.
//
NTSTATUS
IoControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_MCL_QUERY_VIRTUAL_ADAPTER:
        return IoQueryVirtualAdapter(Irp, IrpSp);
    case IOCTL_MCL_QUERY_PHYSICAL_ADAPTER:
        return IoQueryPhysicalAdapter(Irp, IrpSp);
    case IOCTL_MCL_QUERY_NEIGHBOR_CACHE:
        return IoQueryNeighborCache(Irp, IrpSp);
    case IOCTL_MCL_FLUSH_NEIGHBOR_CACHE:
        return IoFlushNeighborCache(Irp, IrpSp);
    case IOCTL_MCL_QUERY_CACHE_NODE:
        return IoQueryCacheNode(Irp, IrpSp);
    case IOCTL_MCL_ADD_LINK:
        return IoAddLink(Irp, IrpSp);
    case IOCTL_MCL_FLUSH_LINK_CACHE:
        return IoFlushLinkCache(Irp, IrpSp);
    case IOCTL_MCL_QUERY_SOURCE_ROUTE:
        return IoQuerySourceRoute(Irp, IrpSp);
    case IOCTL_MCL_QUERY_LINK_CACHE:
        return IoQueryLinkCache(Irp, IrpSp);
    case IOCTL_MCL_QUERY_MAINTENANCE_BUFFER:
        return IoQueryMaintenanceBuffer(Irp, IrpSp);
    case IOCTL_MCL_INFORMATION_REQUEST:
        return IoInformationRequest(Irp, IrpSp);
    case IOCTL_MCL_RESET_STATISTICS:
        return IoResetStatistics(Irp, IrpSp);
#if CHANGELOGS
    case IOCTL_MCL_QUERY_LINK_CACHE_CHANGE_LOG:
        return IoQueryLinkCacheChangeLog(Irp, IrpSp);
#endif
    case IOCTL_MCL_CONTROL_VIRTUAL_ADAPTER:
        return IoControlVirtualAdapter(Irp, IrpSp, FALSE);
    case IOCTL_MCL_PERSISTENT_CONTROL_VIRTUAL_ADAPTER:
        return IoControlVirtualAdapter(Irp, IrpSp, TRUE);
    case IOCTL_MCL_CONTROL_PHYSICAL_ADAPTER:
        return IoControlPhysicalAdapter(Irp, IrpSp);
    case IOCTL_MCL_ADD_SOURCE_ROUTE:
        return IoAddSourceRoute(Irp, IrpSp);
#if CHANGELOGS
    case IOCTL_MCL_QUERY_ROUTE_CACHE_CHANGE_LOG:
        return IoQueryRouteCacheChangeLog(Irp, IrpSp);
    case IOCTL_MCL_QUERY_ROUTE_USAGE:
        return IoQueryRouteUsage(Irp, IrpSp);
#endif
    case IOCTL_MCL_CONTROL_LINK:
        return IoControlLink(Irp, IrpSp);
    case IOCTL_MCL_GENERATE_RANDOM:
        return IoGenerateRandom(Irp, IrpSp);
    }

    Irp->IoStatus.Status = STATUS_NOT_IMPLEMENTED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    return STATUS_NOT_IMPLEMENTED;
}
