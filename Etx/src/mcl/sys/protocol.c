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

typedef struct ProtocolRequest {
    NDIS_REQUEST Request;
    KEVENT Event;
    NDIS_STATUS Status;
} ProtocolRequest;

//* FindPhysicalAdapterFromIndex
//
//  Given an index, returns the physical adapter.
//
ProtocolAdapter *
FindPhysicalAdapterFromIndex(MiniportAdapter *VA, uint Index)
{
    ProtocolAdapter *PA;
    KIRQL OldIrql;

    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    for (PA = VA->PhysicalAdapters;
         PA != NULL;
         PA = PA->Next) {
        //
        // Is this the desired physical adapter?
        //
        if (PA->Index == Index)
            break;
    }
    KeReleaseSpinLock(&VA->Lock, OldIrql);

    return PA;
}

//* FindPhysicalAdapterFromGuid
//
//  Given a guid, returns the physical adapter.
//
ProtocolAdapter *
FindPhysicalAdapterFromGuid(MiniportAdapter *VA, const GUID *Guid)
{
    ProtocolAdapter *PA;
    KIRQL OldIrql;

    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    for (PA = VA->PhysicalAdapters;
         PA != NULL;
         PA = PA->Next) {
        //
        // Is this the desired physical adapter?
        //
        if (RtlEqualMemory(Guid, &PA->Guid, sizeof(GUID)))
            break;
    }
    KeReleaseSpinLock(&VA->Lock, OldIrql);

    return PA;
}

//* ProtocolQueueFull
//
//  Returns TRUE if the transmit queue of the specified
//  interface is full.
//
boolint
ProtocolQueueFull(MiniportAdapter *VA, LQSRIf OutIf)
{
    ProtocolAdapter *PA;

    PA = FindPhysicalAdapterFromIndex(VA, OutIf);
    return (PA != NULL) && (PA->CountSentOutstanding >= PROTOCOL_MAX_QUEUE);
}

//* ProtocolResetStatistics
//
//  Resets all counters and statistics gathering for the physical adapter.
//
//  The caller must hold a reference for the physical adapter
//  or have the virtual adapter locked.
//
void
ProtocolResetStatistics(ProtocolAdapter *PA)
{
    PA->PacketsSent = 0;
    PA->PacketsReceived = 0;
    PA->PacketsReceivedTD = 0;
    PA->PacketsReceivedFlat = 0;

    PA->CountPacketPoolFailure = 0;

    PA->MaxRecvOutstanding = PA->CountRecvOutstanding;
    PA->MaxSentOutstanding = PA->MaxSentOutstanding;
}

//* ProtocolRequestHelper
//
//  This is a utility routine to submit a general request to an NDIS
//  driver.  The caller specifes the request code (OID), a buffer and
//  a length.  This routine allocates a request structure, fills it in,
//  and submits the request.
//
NDIS_STATUS
ProtocolRequestHelper(
    ProtocolAdapter *PA,
    NDIS_REQUEST_TYPE RT,   // Type of request to be done (Set or Query).
    NDIS_OID OID,           // Value to be set/queried.
    void *Info,             // Pointer to the buffer to be passed.
    uint Length,            // Length of data in above buffer.
    uint *Needed)           // Location to fill in with bytes needed in buffer.
{
    ProtocolRequest Request;
    NDIS_STATUS Status;

    // Now fill it in.
    Request.Request.RequestType = RT;
    if (RT == NdisRequestSetInformation) {
        Request.Request.DATA.SET_INFORMATION.Oid = OID;
        Request.Request.DATA.SET_INFORMATION.InformationBuffer = Info;
        Request.Request.DATA.SET_INFORMATION.InformationBufferLength = Length;
    } else {
        Request.Request.DATA.QUERY_INFORMATION.Oid = OID;
        Request.Request.DATA.QUERY_INFORMATION.InformationBuffer = Info;
        Request.Request.DATA.QUERY_INFORMATION.InformationBufferLength = Length;
    }

    //
    // Note that we can NOT use PA->Event and PA->Status here.
    // There may be multiple concurrent ProtocolRequestHelper calls.
    //

    KeInitializeEvent(&Request.Event, SynchronizationEvent, FALSE);

    NdisRequest(&Status, PA->Handle, &Request.Request);

    if (Status == NDIS_STATUS_PENDING) {
        (void) KeWaitForSingleObject(&Request.Event, UserRequest,
                                     KernelMode, FALSE, NULL);
        Status = Request.Status;
    }

    if (Needed != NULL)
        *Needed = Request.Request.DATA.QUERY_INFORMATION.BytesNeeded;

    return Status;
}

//* ProtocolQuery
//
//  Returns information about the physical adapter.
//
void
ProtocolQuery(
    ProtocolAdapter *PA,
    MCL_INFO_PHYSICAL_ADAPTER *Info)
{
    NDIS_STATUS Status;

    RtlCopyMemory(Info->Address, PA->Address, sizeof(PhysicalAddress));
    Info->MTU = PA->MaxFrameSize;
    Info->Promiscuous = PA->Promiscuous;
    Info->ReceiveOnly = PA->ReceiveOnly;
    Info->Channel = PA->Channel;
    Info->Bandwidth = PA->Bandwidth;

    Info->PacketsSent = PA->PacketsSent;
    Info->PacketsReceived = PA->PacketsReceived;
    Info->PacketsReceivedTD = PA->PacketsReceivedTD;
    Info->PacketsReceivedFlat = PA->PacketsReceivedFlat;

    Info->CountPacketPoolFailure = PA->CountPacketPoolFailure;

    Info->CountRecvOutstanding = PA->CountRecvOutstanding;
    Info->MaxRecvOutstanding = PA->MaxRecvOutstanding;
    Info->CountSentOutstanding = PA->CountSentOutstanding;
    Info->MaxSentOutstanding = PA->MaxSentOutstanding;

    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_802_11_BSSID,
                                   &Info->W.BSSID, sizeof Info->W.BSSID,
                                   NULL);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Ethernet;

    Info->Type = MCL_PHYSICAL_ADAPTER_802_11;

    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_802_11_SSID,
                                   &Info->W.SSID, sizeof Info->W.SSID,
                                   NULL);
    if (Status != NDIS_STATUS_SUCCESS) {
        KdPrint(("MCL!ProtocolQuery: OID_802_11_SSID: %x\n", Status));
        Info->W.SSID.SsidLength = 0;
    }

    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_802_11_INFRASTRUCTURE_MODE,
                                   &Info->W.Mode, sizeof Info->W.Mode,
                                   NULL);
    if (Status != NDIS_STATUS_SUCCESS) {
        KdPrint(("MCL!ProtocolQuery: OID_802_11_INFRASTRUCTURE_MODE: %x\n", Status));
        Info->W.Mode = (NDIS_802_11_NETWORK_INFRASTRUCTURE) -1;
    }

    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_802_11_CONFIGURATION,
                                   &Info->W.Radio, sizeof Info->W.Radio,
                                   NULL);
    if (Status != NDIS_STATUS_SUCCESS) {
        KdPrint(("MCL!ProtocolQuery: OID_802_11_CONFIGURATION: %x\n", Status));
        memset(&Info->W.Radio, 0, sizeof Info->W.Radio);
    }

    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_802_11_POWER_MODE,
                                   &Info->W.PowerMode,
                                   sizeof Info->W.PowerMode,
                                   NULL);
    if (Status != NDIS_STATUS_SUCCESS) {
        KdPrint(("MCL!ProtocolQuery: OID_802_11_POWER_MODE: %x\n", Status));
        Info->W.PowerMode = (NDIS_802_11_POWER_MODE) -1;
    }

    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_802_11_TX_POWER_LEVEL,
                                   &Info->W.TxPowerLevel,
                                   sizeof Info->W.TxPowerLevel,
                                   NULL);
    if (Status != NDIS_STATUS_SUCCESS) {
        KdPrint(("MCL!ProtocolQuery: OID_802_11_TX_POWER_LEVEL: %x\n", Status));
        Info->W.TxPowerLevel = 0;
    }

    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_802_11_RTS_THRESHOLD,
                                   &Info->W.RTSThreshold,
                                   sizeof Info->W.RTSThreshold,
                                   NULL);
    if (Status != NDIS_STATUS_SUCCESS) {
        KdPrint(("MCL!ProtocolQuery: OID_802_11_RTS_THRESHOLDL: %x\n", Status));
        Info->W.RTSThreshold = (NDIS_802_11_RTS_THRESHOLD) -1;
    }

    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_802_11_STATISTICS,
                                   &Info->W.Statistics,
                                   sizeof Info->W.Statistics,
                                   NULL);
    if (Status != NDIS_STATUS_SUCCESS) {
        KdPrint(("MCL!ProtocolQuery: OID_802_11_STATISTICS: %x\n", Status));
        memset(&Info->W.Statistics, 0, sizeof Info->W.Statistics);
    }

    return;

Ethernet:
    Info->Type = MCL_PHYSICAL_ADAPTER_ETHERNET;
}

//* ProtocolControl
//
//  Sets protocol configuration parameters from the structure.
//
//  Return values:
//      STATUS_INVALID_PARAMETER_3      Bad Channel.
//      STATUS_INVALID_PARAMETER_4      Bad Bandwidth.
//      STATUS_SUCCESS
//
NTSTATUS
ProtocolControl(
    ProtocolAdapter *PA,
    MCL_CONTROL_PHYSICAL_ADAPTER *Control)
{
    //
    // Check parameters before any updates.
    //

    if (Control->Channel != 0) {
        if (Control->Channel >= 256)
            return STATUS_INVALID_PARAMETER_3;
    }

    if (Control->Bandwidth != 0) {
        if (((Control->Bandwidth >> 2) >= 1024) ||
            ((Control->Bandwidth >> 2) == 0))
            return STATUS_INVALID_PARAMETER_4;
    }

    //
    // Update the attributes of the physical adapter.
    //

    ExAcquireFastMutex(&PA->Mutex);

    if (Control->ReceiveOnly != (boolint) -1)
        PA->ReceiveOnly = Control->ReceiveOnly;

    if (Control->Channel != 0) {
        PA->Channel = Control->Channel;
        PA->ChannelConfigured = TRUE;
    }

    if (Control->Bandwidth != 0) {
        PA->Bandwidth = Control->Bandwidth;
        PA->BandwidthConfigured = TRUE;
    }

    ExReleaseFastMutex(&PA->Mutex);
    return STATUS_SUCCESS;
}

//* ProtocolOpenAdapterComplete
//
//  Called by NDIS when an NdisOpenAdapter completes.
//
void
ProtocolOpenAdapterComplete(
    NDIS_HANDLE Handle,       // Binding handle.
    NDIS_STATUS Status,       // Final status of command.
    NDIS_STATUS ErrorStatus)  // Extra status for some errors.
{
    ProtocolAdapter *PA = (ProtocolAdapter *)Handle;

    UNREFERENCED_PARAMETER(ErrorStatus);

    KdPrint(("MCL!ProtocolOpenAdapterComplete(VA %p PA %p)\n",
             PA->VirtualAdapter, PA));

    //
    // Signal whoever is waiting and pass the final status.
    //
    PA->Status = Status;
    KeSetEvent(&PA->Event, 0, FALSE);
}

//* ProtocolCloseAdapterComplete
//
//  Called by NDIS when an NdisCloseAdapter completes.
//
//  At this point, NDIS guarantees that it has no other outstanding
//  calls to us.
//
void
ProtocolCloseAdapterComplete(
    NDIS_HANDLE Handle,  // Binding handle.
    NDIS_STATUS Status)  // Final status of command.
{
    ProtocolAdapter *PA = (ProtocolAdapter *)Handle;
    KdPrint(("MCL!ProtocolCloseAdapterComplete(VA %p PA %p)\n",
             PA->VirtualAdapter, PA));

    //
    // Signal whoever is waiting and pass the final status.
    //
    PA->Status = Status;
    KeSetEvent(&PA->Event, 0, FALSE);
}

//* ProtocolTransmitComplete
//
//  Called by NDIS when a send completes.
//
void
ProtocolTransmitComplete(
    NDIS_HANDLE Handle,   // Binding handle.
    PNDIS_PACKET Packet,  // Packet that was sent.
    NDIS_STATUS Status)   // Final status of send.
{
    ProtocolAdapter *PA = (ProtocolAdapter *)Handle;

    ASSERT(PC(Packet)->PA == PA);

    (*PC(Packet)->TransmitComplete)(PA->VirtualAdapter, Packet, Status);

    InterlockedDecrement((PLONG)&PA->CountSentOutstanding);

#if 0
    ReleasePA(PA);
#endif
}

//* ProtocolTransmit
//
//  Send a packet. The packet already has an Ethernet header.
//
void
ProtocolTransmit(
    ProtocolAdapter *PA,
    NDIS_PACKET *Packet)
{
    EtherHeader UNALIGNED *Ether;
    NDIS_STATUS Status;

    InterlockedIncrement((PLONG)&PA->VirtualAdapter->CountXmit);

    //
    // We never want the physical adapter to loopback.
    //
    Packet->Private.Flags = NDIS_FLAGS_DONT_LOOPBACK;

    //
    // Our sender initializes the Ethernet destination.
    //
    Ether = (EtherHeader UNALIGNED *) NdisFirstBuffer(Packet)->MappedSystemVa;
    RtlCopyMemory(Ether->Source, PA->Address, IEEE_802_ADDR_LENGTH);
    Ether->Type = ETYPE_MSFT;

    //
    // Our sender initializes PC(Packet)->TransmitComplete.
    // The packet holds a reference for the physical adapter.
    //
#if 0
    AddRefPA(PA);
#endif
    PC(Packet)->PA = PA;

    InterlockedIncrementHighWater((PLONG)&PA->CountSentOutstanding,
                                  (PLONG)&PA->MaxSentOutstanding);

    if (PA->ReceiveOnly) {
        //
        // This is adapter is configured to only receive.
        // This simplifies testing of asymmetric links.
        //
        Status = NDIS_STATUS_FAILURE;
    }
    else {
        //
        // Send the packet. NDIS will call ProtocolTransmitComplete
        // if the send completes asynchronously.
        //
        InterlockedIncrement((PLONG)&PA->PacketsSent);
        NdisSend(&Status, PA->Handle, Packet);
    }

    if (Status != NDIS_STATUS_PENDING) {
        //
        // The send finished synchronously,
        // so we call the completion handler.
        //
        ProtocolTransmitComplete((NDIS_HANDLE)PA, Packet, Status);
    }
}

//* ProtocolForwardRequestComplete
//
//  Completes an individual packet transmission for ProtocolForwardRequest.
//
void
ProtocolForwardRequestComplete(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet,
    NDIS_STATUS Status)
{
    SRPacket *srp = PC(Packet)->srp;

    //
    // Free the packet structure.
    //
    NdisFreePacketClone(Packet);

    //
    // Update the current cumulative status.
    // If the current status is NDIS_STATUS_SUCCESS,
    // we replace it with our new status.
    //
    InterlockedCompareExchange((PLONG)&srp->ForwardStatus,
                               Status,
                               NDIS_STATUS_SUCCESS);

    if (InterlockedDecrement((PLONG)&srp->ForwardCount) == 0) {
        //
        // Complete the ProtocolForwardRequest operation.
        //
        (*srp->TransmitComplete)(VA, srp, srp->ForwardStatus);
    }
}

//* ProtocolForwardRequest
//
//  Sends a Request packet via every physical adapter simultaneously.
//  The operation completes when the last underlying transmit completes.
//  The operation completes successfully only if every
//  underlying transmit is successful.
//
//  Our caller supplies srp->TransmitComplete, which always called
//  to consume the SRPacket.
//
//  The packet destination is presumably a broadcast/multicast
//  address, but that is not actually required.
//
void
ProtocolForwardRequest(
    MiniportAdapter *VA,
    SRPacket *srp)
{
    ProtocolAdapter *PA;
    NDIS_PACKET *PacketList = NULL;
    NDIS_PACKET *Packet;
    NDIS_STATUS Status;
    uint Hops;
    KIRQL OldIrql;

    //
    // We have already added ourselves to the hop list.
    //
    Hops = ROUTE_REQUEST_HOPS(srp->req->opt.optDataLen) - 1;
    ASSERT(RtlEqualMemory(srp->req->opt.hopList[Hops].addr,
                          VA->Address, SR_ADDR_LEN));

    //
    // Check for options that can be piggy-backed on this packet.
    // We can only piggy-back if we are originating the request.
    //
    if (Hops == 0)
        PbackSendPacket(VA, srp);

    //
    // We start with one reference (our own) for the SRPacket.
    //
    srp->ForwardCount = 1;
    srp->ForwardStatus = NDIS_STATUS_SUCCESS;

    //
    // First we build a temporary list of packet structures
    // and corresponding physical adapters. While doing this,
    // we also initialize the transmission count.
    // It starts with one, for our own reference for OrigPacket.
    //
    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    for (PA = VA->PhysicalAdapters;
         PA != NULL;
         PA = PA->Next) {

        srp->req->opt.hopList[Hops].outif = (LQSRIf) PA->Index;

        Status = SRPacketToPkt(VA, srp, &Packet);
        if (Status != NDIS_STATUS_SUCCESS) {
            srp->ForwardStatus = Status;
            break;
        }

        //
        // Remember the packet and corresponding physical adapter.
        // We temporarily use the OrigPacket field to build our list.
        //
        PC(Packet)->PA = PA;
        PC(Packet)->OrigPacket = PacketList;
        PacketList = Packet;
        srp->ForwardCount++;
    }
    KeReleaseSpinLock(&VA->Lock, OldIrql);

    //
    // Now we can transmit the packet via each physical adapter,
    // using the new packet structures.
    //
    while ((Packet = PacketList) != NULL) {
        PacketList = PC(Packet)->OrigPacket;
        PA = PC(Packet)->PA;

        //
        // Send the packet via a physical adapter.
        //
        PC(Packet)->srp = srp;
        PC(Packet)->TransmitComplete = ProtocolForwardRequestComplete;
        ProtocolTransmit(PA, Packet);
    }

    //
    // Release our reference for the original packet.
    //
    if (InterlockedDecrement((PLONG)&srp->ForwardCount) == 0) {
        //
        // Complete the ProtocolForwardRequest operation.
        //
        (*srp->TransmitComplete)(VA, srp, srp->ForwardStatus);
    }
}

//* ProtocolResetComplete
//
//  Called by NDIS when a reset completes.
//
void
ProtocolResetComplete(
    NDIS_HANDLE Handle,  // Binding handle.
    NDIS_STATUS Status)  // Final status of command.
{
    ProtocolAdapter *PA = (ProtocolAdapter *)Handle;

    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(PA);

    KdPrint(("MCL!ProtocolResetComplete(VA %p PA %p)\n",
             PA->VirtualAdapter, PA));
}

//* ProtocolRequestComplete
//
//  Called by NDIS when an NdisRequest completes.
//  We block on all requests, so we'll just wake up
//  whoever's blocked on this request.
//
void
ProtocolRequestComplete(
    NDIS_HANDLE Handle,     // Binding handle.
    PNDIS_REQUEST Context,  // Request that completed.
    NDIS_STATUS Status)     // Final status of requested command.
{
    ProtocolRequest *Request = (ProtocolRequest *) Context;

    UNREFERENCED_PARAMETER(Handle);

    //
    // Signal the completion of a generic synchronous request.
    // See ProtocolRequestHelper.
    //
    Request->Status = Status;
    KeSetEvent(&Request->Event, 0, FALSE);
}

//* ProtocolFreePacket
//
//  Frees a packet allocated by ProtocolReceive.
//
void
ProtocolFreePacket(
    ProtocolAdapter *PA,
    NDIS_PACKET *Packet)
{
    NDIS_BUFFER *Buffer;
    void *Data;

    Buffer = NdisFirstBuffer(Packet);
    ASSERT((Buffer != NULL) && (Buffer->Next == NULL));
    ASSERT(Buffer->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL);
    Data = Buffer->MappedSystemVa;

    NdisFreePacket(Packet);
    NdisFreeBuffer(Buffer);
    ExFreePool(Data);

    InterlockedDecrement((PLONG)&PA->CountRecvOutstanding);
}

//* ProtocolTransferDataComplete
//
//  Called by NDIS when a transfer data completes.
//  We now have a complete packet.
//
void
ProtocolTransferDataComplete(
    NDIS_HANDLE Handle,   // Binding handle.
    PNDIS_PACKET Packet,  // The packet used for the Transfer Data (TD).
    NDIS_STATUS Status,   // Final status of command.
    uint BytesCopied)     // Number of bytes copied.
{
    ProtocolAdapter *PA = (ProtocolAdapter *)Handle;
    NDIS_BUFFER *Buffer;

    UNREFERENCED_PARAMETER(BytesCopied);

    KdPrint(("MCL!ProtocolTransferDataComplete(VA %p PA %p) -> %x\n",
             PA->VirtualAdapter, PA, Status));

    //
    // Undo the NdisAdjustBuffer in ProtocolReceive.
    //
    Buffer = NdisFirstBuffer(Packet);
    NdisAdjustBuffer(Buffer,
                     (uchar *)Buffer->MappedSystemVa - sizeof(EtherHeader),
                     Buffer->ByteCount + sizeof(EtherHeader));

    if (Status == NDIS_STATUS_SUCCESS) {
        //
        // We have the packet data so receive the packet.
        //
        MiniportReceivePacket(PA->VirtualAdapter, PA,
                              Packet, ProtocolFreePacket);
    }
    else {
        //
        // Free the packet.
        //
        ProtocolFreePacket(PA, Packet);
    }
}

//* ProtocolReceive
//
//  Called by NDIS when data arrives.
//  Note that newer NDIS drivers are likely to call ProtocolReceivePacket to
//  indicate data arrival instead of this routine.
//
//  The status code tells NDIS whether or not we took the packet.
//
NDIS_STATUS
ProtocolReceive(
    NDIS_HANDLE ProtocolBindingContext,
    NDIS_HANDLE MacReceiveContext,
    void *HeaderBuffer,
    uint HeaderBufferSize,
    void *LookAheadBuffer,
    uint LookAheadBufferSize,
    uint PacketSize)
{
    ProtocolAdapter *PA = (ProtocolAdapter *) ProtocolBindingContext;
    EtherHeader UNALIGNED *Ether;
    NDIS_PACKET *Packet;
    NDIS_BUFFER *Buffer;
    void *Data;
    uint UNALIGNED *Code;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(HeaderBufferSize);

    //
    // Because we only bind to Ethernets.
    //
    ASSERT(HeaderBufferSize == sizeof *Ether);
    Ether = (EtherHeader UNALIGNED *) HeaderBuffer;

    //
    // Check both the EtherType and the following code value
    // to ensure that we only receive LQSR packets.
    //

    if (Ether->Type != ETYPE_MSFT)
        return NDIS_STATUS_NOT_RECOGNIZED;

    if ((PacketSize < sizeof *Code) || (LookAheadBufferSize < sizeof *Code))
        return NDIS_STATUS_NOT_RECOGNIZED;

    Code = (uint UNALIGNED *) LookAheadBuffer;
    if (*Code != LQSR_CODE)
        return NDIS_STATUS_NOT_RECOGNIZED;

    InterlockedIncrement((PLONG)&PA->PacketsReceived);

    //
    // Allocate non-paged pool to hold the packet data.
    //
    Data = ExAllocatePool(NonPagedPool, sizeof *Ether + PacketSize);
    if (Data == NULL)
        return NDIS_STATUS_RESOURCES;

    //
    // Allocate a packet structure.
    //
    NdisAllocatePacket(&Status, &Packet, PA->PacketPool);
    if (Status != NDIS_STATUS_SUCCESS) {
        InterlockedIncrement((PLONG)&PA->CountPacketPoolFailure);
        ExFreePool(Data);
        return Status;
    }

    //
    // Allocate a packet buffer.
    //
    NdisAllocateBuffer(&Status, &Buffer, PA->BufferPool,
                       Data, sizeof *Ether + PacketSize);
    if (Status != NDIS_STATUS_SUCCESS) {
        NdisFreePacket(Packet);
        ExFreePool(Data);
        return Status;
    }

    NdisChainBufferAtFront(Packet, Buffer);

    InterlockedIncrementHighWater((PLONG)&PA->CountRecvOutstanding,
                                  (PLONG)&PA->MaxRecvOutstanding);

    //
    // Do we have the entire packet?
    //
    if (LookAheadBufferSize < PacketSize) {
        uint Transferred;

        InterlockedIncrement((PLONG)&PA->PacketsReceivedTD);

        //
        // Copy the Ethernet header.
        //
        RtlCopyMemory(Data, HeaderBuffer, sizeof *Ether);

        //
        // We must asynchronously transfer the packet data.
        // To do this we must adjust the buffer to move past
        // the Ethernet header.
        //
        NdisAdjustBuffer(Buffer, (uchar *)Data + sizeof *Ether, PacketSize);
        NdisTransferData(&Status, PA->Handle, MacReceiveContext,
                         0, PacketSize, Packet, &Transferred);
        if (Status != NDIS_STATUS_PENDING) {
            //
            // The transfer completed synchronously.
            //
            ProtocolTransferDataComplete(ProtocolBindingContext,
                                         Packet, Status, Transferred);
        }
    }
    else {
        InterlockedIncrement((PLONG)&PA->PacketsReceivedFlat);

        //
        // We already have access to the packet data.
        //
        RtlCopyMemory(Data, HeaderBuffer, sizeof *Ether);
        RtlCopyMemory((uchar *)Data + sizeof *Ether,
                      LookAheadBuffer, PacketSize);
        MiniportReceivePacket(PA->VirtualAdapter, PA,
                              Packet, ProtocolFreePacket);
    }

    return NDIS_STATUS_SUCCESS;
}

//* ProtocolReceiveComplete
//
//  Called by NDIS after some number of receives.
//  In some sense, it indicates 'idle time'.
//
void
ProtocolReceiveComplete(
    NDIS_HANDLE Handle)  // Binding handle.
{
    UNREFERENCED_PARAMETER(Handle);
}

typedef struct ProtocolMediaConnectContext {
    PIO_WORKITEM Item;
    ProtocolAdapter *PA;
} ProtocolMediaConnectContext;

//* ProtocolQueryRadioConfiguration
//
//  Uses ProtocolRequestHelper to query the channel and bandwidth
//  of the physical adapter.
//
//  Called in a thread context, not at DPC level.
//
void
ProtocolQueryRadioConfiguration(
    ProtocolAdapter *PA)
{
    NDIS_STATUS Status;

    //
    // Note that a burst of media-connect events can lead to races
    // with multiple simultaneous ProtocolQueryRadioConfiguration
    // executions.  We need the mutex to ensure that after the last
    // media-connect, the adapter configuration stabilizes with
    // correct Channel and Bandwidth values.
    //
    ExAcquireFastMutex(&PA->Mutex);

    if (! PA->BandwidthConfigured) {
        uint Speed;

        //
        // Query the maximum link bandwidth.
        //
        Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                       OID_GEN_LINK_SPEED,
                                       &Speed, sizeof Speed,
                                       NULL);
        if (Status != NDIS_STATUS_SUCCESS) {
            //
            // Use 10Mbs as a default.
            //
            Speed = 100000;
        }

        //
        // The OID returns the speed value in 100 bps units.
        //
        PA->Bandwidth = WcettEncodeBandwidth(Speed * 100);
    }

    if (! PA->ChannelConfigured) {
        NDIS_802_11_CONFIGURATION Radio;

        //
        // Use Channel zero as a default.
        //
        PA->Channel = 0;

        //
        // Query the radio configuration.
        // NB: If the radio is still associating, this query will fail!
        //
        Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                       OID_802_11_CONFIGURATION,
                                       &Radio, sizeof Radio,
                                       NULL);
        if (Status == NDIS_STATUS_SUCCESS) {
            //
            // The OID returns the channel in kHz.
            // Convert to a channel number.
            //
            switch (Radio.DSConfig) {

#define Case24GhzChannel(ch) \
                case ((ch * 5) + 2407) * 1000: \
                    PA->Channel = ch; \
                    break

                Case24GhzChannel(1);
                Case24GhzChannel(2);
                Case24GhzChannel(3);
                Case24GhzChannel(4);
                Case24GhzChannel(5);
                Case24GhzChannel(6);
                Case24GhzChannel(7);
                Case24GhzChannel(8);
                Case24GhzChannel(9);
                Case24GhzChannel(10);
                Case24GhzChannel(11);

#define Case5GhzChannel(ch) \
                case ((ch * 5) + 5000) * 1000: \
                    PA->Channel = ch; \
                    break

                Case5GhzChannel(36);
                Case5GhzChannel(37);
                Case5GhzChannel(38);
                Case5GhzChannel(39);
                Case5GhzChannel(40);
                Case5GhzChannel(41);
                Case5GhzChannel(42);
                Case5GhzChannel(43);
                Case5GhzChannel(44);
                Case5GhzChannel(45);
                Case5GhzChannel(46);
                Case5GhzChannel(47);
                Case5GhzChannel(48);
                Case5GhzChannel(49);
                Case5GhzChannel(50);
                Case5GhzChannel(51);
                Case5GhzChannel(52);
                Case5GhzChannel(53);
                Case5GhzChannel(54);
                Case5GhzChannel(55);
                Case5GhzChannel(56);
                Case5GhzChannel(57);
                Case5GhzChannel(58);
                Case5GhzChannel(59);
                Case5GhzChannel(60);
                Case5GhzChannel(61);
                Case5GhzChannel(62);
                Case5GhzChannel(63);
                Case5GhzChannel(64);
                Case5GhzChannel(149);
                Case5GhzChannel(150);
                Case5GhzChannel(151);
                Case5GhzChannel(152);
                Case5GhzChannel(153);
                Case5GhzChannel(154);
                Case5GhzChannel(155);
                Case5GhzChannel(156);
                Case5GhzChannel(157);
                Case5GhzChannel(158);
                Case5GhzChannel(159);
                Case5GhzChannel(160);
                Case5GhzChannel(161);
                Case5GhzChannel(162);
                Case5GhzChannel(163);
                Case5GhzChannel(164);
                Case5GhzChannel(165);
            }
        }
    }

    ExReleaseFastMutex(&PA->Mutex);
}

//* ProtocolMediaConnect
//
//  Called as a work-item on a system thread,
//  after NDIS indicates a media-connect event.
//
void
ProtocolMediaConnect(
    DEVICE_OBJECT *DeviceObject,
    void *Context)
{
    ProtocolMediaConnectContext *PMCC =
        (ProtocolMediaConnectContext *) Context;
    PIO_WORKITEM Item = PMCC->Item;
    ProtocolAdapter *PA = PMCC->PA;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoFreeWorkItem(Item);
    ExFreePool(PMCC);

    ProtocolQueryRadioConfiguration(PA);
    LinkCacheDeleteInterface(PA->VirtualAdapter, (LQSRIf) PA->Index);
}

//* ProtocolStatus
//
//  Called by NDIS when some sort of status change occurs.
//  We can take action depending on the type of status.
//
//  Called at DPC level.
//
void
ProtocolStatus(
    NDIS_HANDLE Handle,   // Binding handle.
    NDIS_STATUS GStatus,  // General status type which caused the call.
    void *Status,         // Pointer to buffer of status specific info.
    uint StatusSize)      // Size of the above status buffer.
{
    ProtocolAdapter *PA = (ProtocolAdapter *)Handle;

    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(StatusSize);

    KdPrint(("MCL!ProtocolStatus(VA %p PA %p GStatus %x)\n",
             PA->VirtualAdapter, PA, GStatus));

    if (GStatus == NDIS_STATUS_MEDIA_CONNECT) {
        ProtocolMediaConnectContext *PMCC;
        PIO_WORKITEM Item;

        //
        // We can not use ProtocolRequestHelper at DPC level.
        // So we queue a work-item for a system thread.
        //

        PMCC = ExAllocatePool(NonPagedPool, sizeof *PMCC);
        if (PMCC == NULL)
            return;

        Item = IoAllocateWorkItem(PA->VirtualAdapter->DeviceObject);
        if (Item == NULL) {
            ExFreePool(PMCC);
            return;
        }

        PMCC->Item = Item;
        PMCC->PA = PA;

        IoQueueWorkItem(Item, ProtocolMediaConnect, DelayedWorkQueue, PMCC);
    }
}

//* ProtocolStatusComplete
//
//  Called by NDIS after a status event.
//
void
ProtocolStatusComplete(
    NDIS_HANDLE Handle)  // Binding handle.
{
    UNREFERENCED_PARAMETER(Handle);
}

//* ProtocolReturnPacket
//
//  Returns when we have finished processing a packet
//  from ProtocolReceivePacket.
//
void
ProtocolReturnPacket(
    ProtocolAdapter *PA,
    NDIS_PACKET *Packet)
{
    NdisReturnPackets(&Packet, 1);
    InterlockedDecrement((PLONG)&PA->CountRecvOutstanding);
}

//
//* ProtocolReceivePacket
//
//  Called by NDIS when data arrives.
//  Note that older NDIS drivers are likely to call ProtocolReceive to
//  indicate data arrival instead of this routine.
//
//  Returns the number of references we hold to Packet upon return.
//
int
ProtocolReceivePacket(
    NDIS_HANDLE Handle,   // Binding handle.
    PNDIS_PACKET Packet)  // Packet descriptor for incoming packet.
{
    ProtocolAdapter *PA = (ProtocolAdapter *)Handle;
    NDIS_BUFFER *Buffer;
    void *Address;
    uint Length;
    EtherHeader UNALIGNED *Ether;
    uint UNALIGNED *Code;

    //
    // Find out about the packet we've been handed.
    //
    Buffer = NdisFirstBuffer(Packet);
    NdisQueryBuffer(Buffer, &Address, &Length);

    if (Length < sizeof *Ether + sizeof *Code) {
        //
        // Header data too small to contain an Ethernet header + Code.
        //
        KdPrint(("MCL!ProtocolReceivePacket(VA %p PA %p): Length %u\n",
                 PA->VirtualAdapter, PA, Length));
        return 0;
    }

    Ether = (EtherHeader UNALIGNED *) Address;
    Code = (uint UNALIGNED *) (Ether + 1);

    if ((Ether->Type != ETYPE_MSFT) ||
        (*Code != LQSR_CODE))
        return 0;

    InterlockedIncrement((PLONG)&PA->PacketsReceived);
    InterlockedIncrementHighWater((PLONG)&PA->CountRecvOutstanding,
                                  (PLONG)&PA->MaxRecvOutstanding);

    MiniportReceivePacket(PA->VirtualAdapter, PA,
                          Packet, ProtocolReturnPacket);
    return 1;
}

//* ProtocolCloseAdapter
//
//  Closes our connection to NDIS and frees the adapter.
//  The adapter has already been removed
//  from the MiniportAdapter->PhysicalAdapters list.
//
//  Called from thread context, with no locks held.
//
void
ProtocolCloseAdapter(ProtocolAdapter *PA)
{
    NDIS_STATUS Status;

    KdPrint(("MCL!ProtocolCloseAdapter(VA %p PA %p)\n",
             PA->VirtualAdapter, PA));

    //
    // Initialize an event in case NdisCloseAdapter completes
    // asynchronously via ProtocolCloseAdapterComplete.
    //
    KeInitializeEvent(&PA->Event, SynchronizationEvent, FALSE);

    //
    // Close the connection to NDIS.
    //
    // NB: We must not use the Handle after we call NdisCloseAdapter.
    //
    NdisCloseAdapter(&Status, PA->Handle);

    //
    // Wait for the close to complete.
    //
    if (Status == NDIS_STATUS_PENDING) {
        (void) KeWaitForSingleObject(&PA->Event,
                                     UserRequest, KernelMode,
                                     FALSE, NULL);
        Status = PA->Status;
    }

    KdPrint(("MCL!NdisCloseAdapter(VA %p PA %p) -> %x\n",
             PA->VirtualAdapter, PA, Status));

    NdisFreeBufferPool(PA->BufferPool);
    NdisFreePacketPool(PA->PacketPool);
    ExFreePool(PA);
}

//* ProtocolOpenRegKey
//
//  Given an adapter, opens the registry key that holds
//  persistent configuration information for the adapter.
//
//  Callable from thread context, not DPC context.
//
NTSTATUS
ProtocolOpenRegKey(
    ProtocolAdapter *PA,
    OpenRegKeyAction Action,
    OUT HANDLE *RegKey)
{
    MiniportAdapter *VA = PA->VirtualAdapter;
    UNICODE_STRING GuidName;
    HANDLE DeviceKey;
    HANDLE ParamsKey;
    HANDLE AdaptersKey;
    NTSTATUS Status;

    PAGED_CODE();

    //
    // Open our configuration information in the registry.
    //
    Status = IoOpenDeviceRegistryKey(VA->PhysicalDeviceObject,
                                     PLUGPLAY_REGKEY_DRIVER,
                                     GENERIC_READ,
                                     &DeviceKey);
    if (! NT_SUCCESS(Status)) {
        KdPrint(("MCL!IoOpenDeviceRegistryKey -> %x\n", Status));
        return Status;
    }

    Status = OpenRegKey(&ParamsKey, DeviceKey,
                        L"Parameters",
                        ((Action == OpenRegKeyCreate) ?
                         OpenRegKeyCreate : OpenRegKeyRead));
    ZwClose(DeviceKey);
    if (! NT_SUCCESS(Status))
        return Status;

    Status = OpenRegKey(&AdaptersKey, ParamsKey,
                        L"Adapters",
                        ((Action == OpenRegKeyCreate) ?
                         OpenRegKeyCreate : OpenRegKeyRead));
    ZwClose(ParamsKey);
    if (! NT_SUCCESS(Status))
        return Status;

    //
    // Convert the guid to string form.
    //
    Status = RtlStringFromGUID(&PA->Guid, &GuidName);
    if (! NT_SUCCESS(Status)) {
        ZwClose(AdaptersKey);
        return Status;
    }

    //
    // It will be null-terminated.
    //
    ASSERT(GuidName.MaximumLength == GuidName.Length + sizeof(WCHAR));
    ASSERT(((WCHAR *)GuidName.Buffer)[GuidName.Length/sizeof(WCHAR)] == UNICODE_NULL);

    Status = OpenRegKey(RegKey, AdaptersKey,
                        (WCHAR *)GuidName.Buffer, Action);

    RtlFreeUnicodeString(&GuidName);
    ZwClose(AdaptersKey);
    return Status;
}

//* ProtocolInitAdapter
//
//  Initializes a physical adapter from the registry.
//
void
ProtocolInitAdapter(ProtocolAdapter *PA)
{
    MCL_CONTROL_PHYSICAL_ADAPTER Control;
    HANDLE Key;
    NTSTATUS Status;

    PAGED_CODE();

    //
    // Open our configuration information in the registry.
    //
    Status = ProtocolOpenRegKey(PA, OpenRegKeyCreate, &Key);
    if (! NT_SUCCESS(Status)) {
        KdPrint(("LQSR:ProtocolOpenRegKey: %x\n", Status));
        return;
    }

    MCL_INIT_CONTROL_PHYSICAL_ADAPTER(&Control);

    //
    // Read configuration parameters.
    //
    (void) GetRegDWORDValue(Key, L"ReceiveOnly", (PULONG)&Control.ReceiveOnly);
    (void) GetRegDWORDValue(Key, L"Channel", (PULONG)&Control.Channel);
    (void) GetRegDWORDValue(Key, L"Bandwidth", (PULONG)&Control.Bandwidth);
    ZwClose(Key);

    //
    // Update the adapter using the parameters.
    //
    (void) ProtocolControl(PA, &Control);
}

//* ProtocolBindAdapter
//
//  Called by NDIS to tell us about a new adapter.
//
//  Called in a thread context, not at DPC level.
//
void
ProtocolBindAdapter(
    PNDIS_STATUS RetStatus,    // Where to return status of this call.
    NDIS_HANDLE BindContext,   // Handle for calling NdisCompleteBindAdapter.
    PNDIS_STRING AdapterName,  // Name of adapter.
    PVOID SS1,                 // System specific parameter 1.
    PVOID SS2)                 // System specific parameter 2.
{
    //
    // Grovel through the BindContext to get the NDIS protocol handle,
    // and from that get our MiniportAdapter structure.
    // We do this because ProtocolBindAdapter does not give us context.
    //
    NDIS_HANDLE Protocol = NdisProtocolFromBindContext(BindContext);
    MiniportAdapter *VA = (MiniportAdapter *)
        *NdisReservedFieldInProtocol(Protocol);

    UNICODE_STRING GuidName;
    GUID Guid;
    uint BindPrefixLength;

    ProtocolAdapter *PA;
    uint MediaIndex;
    static NDIS_MEDIUM MediaArray[] = { NdisMedium802_3 };
    NDIS_STATUS Status, ErrorStatus;
    uint Filter;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(SS1);
    UNREFERENCED_PARAMETER(SS2);

    //
    // Parse past the NDIS binding prefix:
    // We skip the first character (a backslash) and look for a backslash.
    // If we don't find a backslash, RtlGUIDFromString will fail
    // when given a zero-length string.
    //
    BindPrefixLength = 1;
    while (sizeof(WCHAR)*BindPrefixLength < AdapterName->Length) {
        if (((WCHAR *)AdapterName->Buffer)[BindPrefixLength++] == L'\\')
            break;
    }
    BindPrefixLength *= sizeof(WCHAR); // Convert characters to bytes.

    //
    // Convert the NDIS AdapterName to a Guid.
    //
    GuidName.Buffer = (PVOID)((char *)AdapterName->Buffer + BindPrefixLength);
    GuidName.Length = (ushort)(AdapterName->Length - BindPrefixLength);
    GuidName.MaximumLength = (ushort)(AdapterName->MaximumLength - BindPrefixLength);

    if (! NT_SUCCESS(RtlGUIDFromString(&GuidName, &Guid))) {

        KdPrint(("MCL!ProtocolBindAdapter(%.*ls) - bad guid\n",
                 AdapterName->Length / sizeof(WCHAR),
                 AdapterName->Buffer));
        *RetStatus = NDIS_STATUS_FAILURE;
        return;
    }

    PA = (ProtocolAdapter *) ExAllocatePool(NonPagedPool, sizeof *PA);
    if (PA == NULL) {
        *RetStatus = NDIS_STATUS_FAILURE;
        return;
    }

    KdPrint(("MCL!ProtocolBindAdapter(VA %p, Name %.*ls) -> PA %p\n",
             VA,
             AdapterName->Length / sizeof(WCHAR),
             AdapterName->Buffer,
             PA));

    RtlZeroMemory(PA, sizeof *PA);
    PA->VirtualAdapter = VA;
    PA->Index = InterlockedIncrement((PLONG)&VA->NextPhysicalAdapterIndex);
    PA->Guid = Guid;

    NdisAllocatePacketPool(&Status, &PA->PacketPool, PACKET_POOL_SZ, 0);
    if (Status != NDIS_STATUS_SUCCESS) {
        ExFreePool(PA);
        *RetStatus = Status;
        return;
    }

    NdisAllocateBufferPool(&Status, &PA->BufferPool, PACKET_POOL_SZ);
    if (Status != NDIS_STATUS_SUCCESS) {
        NdisFreePacketPool(PA->PacketPool);
        ExFreePool(PA);
        *RetStatus = Status;
        return;
    }

    //
    // Initialize an event in case NdisOpenAdapter completes
    // asynchronously via ProtocolOpenAdapterComplete.
    //
    KeInitializeEvent(&PA->Event, SynchronizationEvent, FALSE);

    //
    // Initialize a mutex used to synchronize attribute retrieval
    // from NDIS.
    //
    ExInitializeFastMutex(&PA->Mutex);

    NdisOpenAdapter(&Status, &ErrorStatus, &PA->Handle,
                    &MediaIndex, MediaArray, sizeof MediaArray / sizeof MediaArray[0],
                    VA->ProtocolHandle, PA, AdapterName,
                    0, NULL);

    //
    // Wait for the open to complete.
    //
    if (Status == NDIS_STATUS_PENDING) {
        (void) KeWaitForSingleObject(&PA->Event, UserRequest, KernelMode,
                                     FALSE, NULL);
        Status = PA->Status;
    }

    if (Status != NDIS_STATUS_SUCCESS) {
        NdisFreeBufferPool(PA->BufferPool);
        NdisFreePacketPool(PA->PacketPool);
        ExFreePool(PA);
        *RetStatus = Status;
        return;
    }

    //
    // Query the maximum frame size.
    // We can only use the adapter if it has a sufficiently large frame,
    // because we need to insert headers.
    //
    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_GEN_MAXIMUM_FRAME_SIZE,
                                   &PA->MaxFrameSize, sizeof PA->MaxFrameSize,
                                   NULL);
    if ((Status != NDIS_STATUS_SUCCESS) ||
        (PA->MaxFrameSize < PROTOCOL_MIN_FRAME_SIZE)) {

        ProtocolCloseAdapter(PA);
        *RetStatus = NDIS_STATUS_FAILURE;
        return;
    }

    //
    // Query the adapter address.
    //
    Status = ProtocolRequestHelper(PA, NdisRequestQueryInformation,
                                   OID_802_3_CURRENT_ADDRESS,
                                   &PA->Address, sizeof PA->Address,
                                   NULL);
    if (Status != NDIS_STATUS_SUCCESS) {

        ProtocolCloseAdapter(PA);
        *RetStatus = NDIS_STATUS_FAILURE;
        return;
    }

    //
    // Initialize from the registry.
    //
    ProtocolInitAdapter(PA);

    //
    // Query the adapter bandwidth and channel,
    // if they were not configured from the registry.
    //
    ProtocolQueryRadioConfiguration(PA);

    //
    // Add the new physical adapter to our list.
    //
    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    PA->Prev = &VA->PhysicalAdapters;
    PA->Next = VA->PhysicalAdapters;
    if (VA->PhysicalAdapters != NULL)
        VA->PhysicalAdapters->Prev = &PA->Next;
    VA->PhysicalAdapters = PA;
    KeReleaseSpinLock(&VA->Lock, OldIrql);

    //
    // Set the receive filter so that we start receiving frames.
    // We want promiscuous mode but not all adapters support it.
    //
    Filter = NDIS_PACKET_TYPE_PROMISCUOUS;
    Status = ProtocolRequestHelper(PA, NdisRequestSetInformation,
                                   OID_GEN_CURRENT_PACKET_FILTER,
                                   &Filter, sizeof Filter, NULL);
    if (Status == NDIS_STATUS_SUCCESS) {
        //
        // We got promiscuous mode.
        //
        PA->Promiscuous = TRUE;
    }
    else {
        ASSERT(PA->Promiscuous == FALSE);

        Filter = (NDIS_PACKET_TYPE_BROADCAST |
                  NDIS_PACKET_TYPE_DIRECTED |
                  NDIS_PACKET_TYPE_MULTICAST);
        Status = ProtocolRequestHelper(PA, NdisRequestSetInformation,
                                       OID_GEN_CURRENT_PACKET_FILTER,
                                       &Filter, sizeof Filter, NULL);
        if (Status != NDIS_STATUS_SUCCESS)
            KdPrint(("MCL!ProtocolRequestHelper(PA %p, PF) -> %x\n",
                     PA, Status));
    }

    //
    // If this is the first physical adapter, indicate that
    // that our miniport is now connected.
    //
    if (PA->Next == NULL)
        MiniportIndicateStatusConnected(VA);

    *RetStatus = NDIS_STATUS_SUCCESS;
}

//* ProtocolUnbindAdapter
//
//  Called by NDIS when we need to unbind from an adapter.
//
void
ProtocolUnbindAdapter(
    PNDIS_STATUS RetStatus,       // Where to return status from this call.
    NDIS_HANDLE Handle,           // Context we gave NDIS earlier.
    NDIS_HANDLE UnbindContext)    // Context for completing this request.
{
    ProtocolAdapter *PA = (ProtocolAdapter *)Handle;
    MiniportAdapter *VA = PA->VirtualAdapter;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(UnbindContext);

    KdPrint(("MCL!ProtocolUnbindAdapter(VA %p PA %p)\n", VA, PA));

    //
    // Remove the adapter from our list.
    //
    KeAcquireSpinLock(&VA->Lock, &OldIrql);
    if (PA->Next != NULL)
        PA->Next->Prev = PA->Prev;
    *PA->Prev = PA->Next;
    KeReleaseSpinLock(&VA->Lock, OldIrql);

    //
    // Remove the adapter from the link cache.
    //
    LinkCacheDeleteInterface(VA, (LQSRIf) PA->Index);

    //
    // If this was the last physical adapter, indicate that
    // our miniport is no longer connected to a physical adapter.
    //
    // NB: We have a race with ProtocolBindAdapter and could
    // indicate disconnected after it indicates connected.
    //
    if (VA->PhysicalAdapters == NULL)
        MiniportIndicateStatusDisconnected(VA);

    ProtocolCloseAdapter(PA);
    *RetStatus = NDIS_STATUS_SUCCESS;
}

//* ProtocolPnPEvent
//
//  Called by NDIS for plug'n'play and power-management events.
//
//  Called in a thread context, not at DPC level.
//
NDIS_STATUS
ProtocolPnPEvent(
    NDIS_HANDLE Handle,   // Binding handle.
    PNET_PNP_EVENT NetPnPEvent)
{
    static char *NetEvents[] = {
        "set power",
        "query power",
        "query remove",
        "cancel remove",
        "reconfigure",
        "bind list",
        "binds complete",
        "pnp capabilities",
    };
    ProtocolAdapter *PA = (ProtocolAdapter *)Handle;

    KdPrint(("MCL!ProtocolPnPEvent(PA %p Event %s)\n", PA,
             (NetPnPEvent->NetEvent < sizeof NetEvents / sizeof NetEvents[0]) ?
             NetEvents[NetPnPEvent->NetEvent] : "unknown"));

    if (NetPnPEvent->NetEvent == NetEventSetPower) {
        NET_DEVICE_POWER_STATE PowerState;

        //
        // Get the power state of the interface.
        //
        ASSERT(NetPnPEvent->BufferLength >= sizeof PowerState);
        PowerState = * (NET_DEVICE_POWER_STATE *) NetPnPEvent->Buffer;

        if (PowerState == NetDeviceStateD0) {
            //
            // The device woke up.
            //
            ProtocolQueryRadioConfiguration(PA);
        }
        LinkCacheDeleteInterface(PA->VirtualAdapter, (LQSRIf) PA->Index);
    }

    return NDIS_STATUS_SUCCESS;
}

//* ProtocolInit
//
//  Registers an LQSR transport with NDIS.
//
NDIS_STATUS
ProtocolInit(
    IN const WCHAR *Name,
    IN MiniportAdapter *VA)
{
    NDIS_PROTOCOL_CHARACTERISTICS PC;
    NDIS_STATUS Status;

    RtlZeroMemory(&PC, sizeof PC);
    RtlInitUnicodeString(&PC.Name, Name);
    PC.MajorNdisVersion = NDIS_PROTOCOL_MAJOR_VERSION;
    PC.MinorNdisVersion = NDIS_PROTOCOL_MINOR_VERSION;
    PC.OpenAdapterCompleteHandler = ProtocolOpenAdapterComplete;
    PC.CloseAdapterCompleteHandler = ProtocolCloseAdapterComplete;
    PC.SendCompleteHandler = ProtocolTransmitComplete;
    PC.TransferDataCompleteHandler = ProtocolTransferDataComplete;
    PC.ResetCompleteHandler = ProtocolResetComplete;
    PC.RequestCompleteHandler = ProtocolRequestComplete;
    PC.ReceiveHandler = ProtocolReceive;
    PC.ReceiveCompleteHandler = ProtocolReceiveComplete;
    PC.StatusHandler = ProtocolStatus;
    PC.StatusCompleteHandler = ProtocolStatusComplete;
    PC.ReceivePacketHandler = ProtocolReceivePacket;
    PC.BindAdapterHandler = ProtocolBindAdapter;
    PC.UnbindAdapterHandler = ProtocolUnbindAdapter;
    PC.PnPEventHandler = ProtocolPnPEvent;

    NdisRegisterProtocol(&Status, &VA->ProtocolHandle, &PC, sizeof PC);
    KdPrint(("MCL!NdisRegisterProtocol(VA %p) -> %x\n", VA, Status));
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    //
    // Modify the NDIS protocol structure to point
    // to our MiniportAdapter structure. We need this because
    // ProtocolBindAdapter does not supply us with a context.
    //
    *NdisReservedFieldInProtocol(VA->ProtocolHandle) = VA;

    return NDIS_STATUS_SUCCESS;
}

//* ProtocolCleanup
//
//  Uninitializes an LQSR transport module.
//  Closes any remaining adapters.
//
void
ProtocolCleanup(MiniportAdapter *VA)
{
    NDIS_STATUS Status;

    //
    // NDIS will call ProtocolUnbindAdapter for each adapter,
    // so we are left with no physical adapters.
    //
    NdisDeregisterProtocol(&Status, VA->ProtocolHandle);
    KdPrint(("MCL!NdisDeregisterProtocol(VA %p) -> %x\n", VA, Status));
    ASSERT(VA->PhysicalAdapters == NULL);
}
