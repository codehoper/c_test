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
#include <string.h>
#include <wchar.h>

//
// These are internal NDIS data structures.
// We use them in NdisProtocolFromBindContext and
// NdisReservedFieldInProtocol. This hack is necessary
// because NDIS does not support a per-protocol context,
// supplied to NDIS in NdisRegisterProtocol and passed
// back to ProtocolBindAdapter. As an alternative we could
// do some machine-specific hacking to make per-protocol
// ProtocolBindAdapter closures.
//

typedef struct _NDIS_PROTOCOL_BLOCK
{
    PNDIS_OPEN_BLOCK                OpenQueue;              // queue of opens for this protocol
    REFERENCE                       Ref;                    // contains spinlock for OpenQueue
    PKEVENT                         DeregEvent;             // Used by NdisDeregisterProtocol
    struct _NDIS_PROTOCOL_BLOCK *   NextProtocol;           // Link to next
    NDIS50_PROTOCOL_CHARACTERISTICS ProtocolCharacteristics;// handler addresses

    WORK_QUEUE_ITEM                 WorkItem;               // Used during NdisRegisterProtocol to
                                                            // notify protocols of existing drivers.
    KMUTEX                          Mutex;                  // For serialization of Bind/Unbind requests
    ULONG                           MutexOwner;             // For debugging
    PNDIS_STRING                    BindDeviceName;
    PNDIS_STRING                    RootDeviceName;
    PNDIS_M_DRIVER_BLOCK            AssociatedMiniDriver;
    PNDIS_MINIPORT_BLOCK            BindingAdapter;
} NDIS_PROTOCOL_BLOCK, *PNDIS_PROTOCOL_BLOCK;

typedef struct _NDIS_BIND_CONTEXT
{
    struct _NDIS_BIND_CONTEXT   *   Next;
    PNDIS_PROTOCOL_BLOCK            Protocol;
    PNDIS_MINIPORT_BLOCK            Miniport;
    NDIS_STRING                     ProtocolSection;
    PNDIS_STRING                    DeviceName;
    WORK_QUEUE_ITEM                 WorkItem;
    NDIS_STATUS                     BindStatus;
    KEVENT                          Event;
    KEVENT                          ThreadDoneEvent;
} NDIS_BIND_CONTEXT, *PNDIS_BIND_CONTEXT;

//* NdisProtocolFromBindContext
//
//  Given an NDIS ProtocolBindAdapter context,
//  returns the NDIS protocol block.
//
NDIS_HANDLE
NdisProtocolFromBindContext(NDIS_HANDLE BindContext)
{
    return (NDIS_HANDLE) ((PNDIS_BIND_CONTEXT) BindContext)->Protocol;
}

//* NdisReservedFieldInProtocol
//
//  Given an NDIS protocol block, returns the address
//  of a reserved field in the protocol block.
//  This reserved field can hold a pointer to our context.
//
void **
NdisReservedFieldInProtocol(NDIS_HANDLE Protocol)
{
    return &((PNDIS_PROTOCOL_BLOCK)Protocol)->ProtocolCharacteristics.ReservedHandlers[3];
}

//* NdisAdjustBuffer
//
//  Changes the virtual address and length described by a buffer.
//  The buffer structure must be large enough to accommodate the PFNs.
//
void
NdisAdjustBuffer(
    NDIS_BUFFER *Buffer,
    void *Address,
    uint Length)
{
    MmInitializeMdl(Buffer, Address, Length);
    MmBuildMdlForNonPagedPool(Buffer);
}

//* NdisClonePacket
//
//  Create a clone copy of a packet. The clone packet
//  has (mostly) the same data as the original packet,
//  but it has a new NDIS_PACKET structure, the first
//  one or two NDIS_BUFFER structures are new, and
//  it has a new data area at the beginning.
//
//  The clone packet has a new data area (CloneHeaderLength),
//  which replaces a prefix of the original (OrigHeaderLength).
//
//  The first buffer of the clone packet has at least
//  CloneHeaderLength + LookAhead bytes of data,
//  limited of course by the total size of the packet.
//
NDIS_STATUS
NdisClonePacket(
    NDIS_PACKET *OrigPacket,
    NDIS_HANDLE PacketPool,
    NDIS_HANDLE BufferPool,
    uint OrigHeaderLength,
    uint CloneHeaderLength,
    uint LookAhead,
    void **pOrigHeader,
    NDIS_PACKET **pClonePacket,
    void **pCloneHeader)
{
    NDIS_PACKET *ClonePacket;
    NDIS_BUFFER *CloneDataBuffer;
    void *OrigData, *CloneData;
    void *OrigHeader;
    NDIS_BUFFER *OrigBuffer;
    uint OrigBufferLength;
    NDIS_STATUS Status;
    uint CloneOffset, CopyAmount;

    //
    // Check the first buffer.
    // It must be at least OrigHeaderLength.
    //
    OrigBuffer = NdisFirstBuffer(OrigPacket);
    NdisQueryBuffer(OrigBuffer, &OrigData, &OrigBufferLength);
    if (OrigBufferLength < OrigHeaderLength)
        return NDIS_STATUS_BUFFER_TOO_SHORT;

    OrigHeader = OrigData;

    //
    // Allocate the clone packet.
    //
    NdisAllocatePacket(&Status, &ClonePacket, PacketPool);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

#if DBG
    RtlFillMemory(PC(ClonePacket), sizeof(PacketContext), 0xcc);
#endif

    if (CloneHeaderLength + LookAhead == 0) {
        //
        // The clone packet just skips OrigHeaderLength bytes
        // into the original packet. We have no clone data region.
        //
        CloneData = NULL;

        if (OrigHeaderLength == 0) {
            //
            // The clone packet has exactly the same data
            // as the original packet.
            //
            CloneDataBuffer = OrigBuffer;
            goto ChainCloneBuffer;
        }
        else if (OrigHeaderLength == OrigBufferLength) {
            //
            // The clone packet skips the entire first buffer
            // of the original packet.
            //
            OrigBuffer = OrigBuffer->Next;
            CloneDataBuffer = OrigBuffer;
            goto ChainCloneBuffer;
        }
        else {
            //
            // The clone packet skips some data in the first buffer.
            // Allocate a clone buffer to describe the remaining data.
            //
            NdisAllocateBuffer(&Status, &CloneDataBuffer, BufferPool,
                               (uchar *)OrigData + OrigHeaderLength,
                               OrigBufferLength - OrigHeaderLength);
            if (Status != NDIS_STATUS_SUCCESS) {
                NdisFreePacket(ClonePacket);
                return Status;
            }

            //
            // Initialize the clone packet with the clone buffer
            // and the remaining original buffers.
            //
            goto ChainOrigBuffers;
        }
    }

    //
    // Allocate the clone data region from non-paged pool.
    // NB: We may not use the entire region, if OrigPacket
    // is smaller than OrigHeaderLength + LookAhead.
    //
    CloneData = ExAllocatePool(NonPagedPool, CloneHeaderLength + LookAhead);
    if (CloneData == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        NdisFreePacket(ClonePacket);
        return Status;
    }

    //
    // Allocate the clone buffer, which describes the clone data region.
    //
    NdisAllocateBuffer(&Status, &CloneDataBuffer, BufferPool,
                       CloneData, CloneHeaderLength + LookAhead);
    if (Status != NDIS_STATUS_SUCCESS) {
        ExFreePool(CloneData);
        NdisFreePacket(ClonePacket);
        return Status;
    }

    //
    // Copy LookAhead data to the clone buffer.
    //
    (uchar *)OrigData += OrigHeaderLength;
    OrigBufferLength -= OrigHeaderLength;
    CloneOffset = CloneHeaderLength;
    for (;;) {
        //
        // Calculate how much lookahead data we will copy
        // from this buffer.
        //
        if (LookAhead < OrigBufferLength)
            CopyAmount = LookAhead;
        else
            CopyAmount = OrigBufferLength;

        RtlCopyMemory((uchar *)CloneData + CloneOffset, OrigData, CopyAmount);

        CloneOffset += CopyAmount;
        LookAhead -= CopyAmount;

        if (LookAhead == 0) {
            //
            // Fix OrigData and OrigBufferLength
            // for bridge buffer calculation below.
            //
            (uchar *)OrigData += CopyAmount;
            OrigBufferLength -= CopyAmount;
            break;
        }

        OrigBuffer = OrigBuffer->Next;
        if (OrigBuffer == NULL) {
            //
            // The packet was smaller than OrigHeaderLength + LookAhead.
            // This means CloneDataBuffer desribes too much memory.
            // So fix it now.
            //
            NdisAdjustBuffer(CloneDataBuffer, CloneData, CloneOffset);
            goto ChainCloneBuffer;
        }

        NdisQueryBuffer(OrigBuffer, &OrigData, &OrigBufferLength);
    }

    //
    // Do we need a bridge buffer? The bridge buffer describes
    // any remaining data in the original buffer.
    //
    if (OrigBufferLength != 0) {
        NDIS_BUFFER *CloneBridgeBuffer;

        //
        // What if OrigData is not non-paged pool??
        //
        NdisAllocateBuffer(&Status, &CloneBridgeBuffer, BufferPool,
                           OrigData, OrigBufferLength);
        if (Status != NDIS_STATUS_SUCCESS) {
            NdisFreeBuffer(CloneDataBuffer);
            ExFreePool(CloneData);
            NdisFreePacket(ClonePacket);
            return Status;
        }

        CloneDataBuffer->Next = CloneBridgeBuffer;
    }

ChainOrigBuffers:

    //
    // Initialize the clone packet with the original buffers
    // that we are not touching.
    //
    OrigBuffer = OrigBuffer->Next;
    if (OrigBuffer != NULL)
        NdisChainBufferAtFront(ClonePacket, OrigBuffer);

ChainCloneBuffer:

    //
    // Chain the clone buffers to the clone packet.
    //
    NdisChainBufferAtFront(ClonePacket, CloneDataBuffer);

    //
    // Initialize for NdisFreePacketClone.
    //
    PC(ClonePacket)->OrigBuffer = OrigBuffer;
    PC(ClonePacket)->CloneData = CloneData;

    *pOrigHeader = OrigHeader;
    *pCloneHeader = CloneData;
    *pClonePacket = ClonePacket;

    return NDIS_STATUS_SUCCESS;
}

//* NdisFreePacketClone
//
//  Free a packet clone created by NdisClonePacket.
//
void
NdisFreePacketClone(NDIS_PACKET *ClonePacket)
{
    NDIS_BUFFER *Buffer, *NextBuffer;

    //
    // Free the clone data region, if we have one.
    //
    if (PC(ClonePacket)->CloneData != NULL)
        ExFreePool(PC(ClonePacket)->CloneData);

    //
    // Free the clone buffers.
    //
    for (Buffer = NdisFirstBuffer(ClonePacket);
         Buffer != PC(ClonePacket)->OrigBuffer;
         Buffer = NextBuffer) {

        NextBuffer = Buffer->Next;
        NdisFreeBuffer(Buffer);
    }

    //
    // Free the clone packet.
    //
    NdisFreePacket(ClonePacket);
}

//* NdisMakeEmptyPacket
//
//  Creates a packet with a single buffer and data region of specified size.
//  The packet can be freed with NdisFreePacketClone.
//
NDIS_STATUS
NdisMakeEmptyPacket(
    NDIS_HANDLE PacketPool,
    NDIS_HANDLE BufferPool,
    uint Size,
    NDIS_PACKET **pPacket,
    void **pData)
{
    NDIS_STATUS Status;
    NDIS_PACKET *Packet;
    NDIS_BUFFER *Buffer;
    void *Data;

    //
    // Allocate the packet to return...
    //
    NdisAllocatePacket(&Status, &Packet, PacketPool);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

#if DBG
    RtlFillMemory(PC(Packet), sizeof(PacketContext), 0xcc);
#endif

    //
    // Allocate the data region from non-paged pool.
    //
    Data = ExAllocatePool(NonPagedPool, Size);
    if (Data == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        NdisFreePacket(Packet);
        return Status;
    }

    //
    // Allocate the buffer, which describes the data region.
    //
    NdisAllocateBuffer(&Status, &Buffer, BufferPool, Data, Size);
    if (Status != NDIS_STATUS_SUCCESS) {
        ExFreePool(Data);
        NdisFreePacket(Packet);
        return Status;
    }

    NdisChainBufferAtFront(Packet, Buffer);

    //
    // Initialize for NdisFreePacketClone.
    //
    PC(Packet)->OrigBuffer = NULL;
    PC(Packet)->CloneData = Data;

    *pPacket = Packet;
    *pData = Data;
    return NDIS_STATUS_SUCCESS;
}
