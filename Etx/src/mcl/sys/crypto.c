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

typedef unsigned char BYTE;
#include <crypto/sha.h>
#include <crypto/aes.h>
#include <crypto/modes.h>

//* CryptoKeyMACModify
//
//  Modifies the MAC key with the Version and MetricType,
//  so that any mismatch will prevent communication.
//
//  This transformation must be a self-inverse,
//  so that doing it again restores the original key.
//  This allos IoQueryVirtualAdapter to return the original key.
//
void
CryptoKeyMACModify(
    MiniportAdapter *VA,
    uchar Key[LQSR_KEY_SIZE])
{
    ((uint UNALIGNED *) Key)[0] ^= Version;
    ((uint UNALIGNED *) Key)[1] ^= VA->MetricType;
}

//* CryptoMAC
//
//  Computes a MAC (Message Authentication Code) for the packet data.
//  The MAC is computed starting at Offset bytes into the Packet.
//  Uses HMAC-SHA1. See RFC 2104.
//
void
CryptoMAC(
    uchar CryptoKeyMAC[CRYPTO_KEY_MAC_LENGTH],
    uchar *MAC,
    uint Length,
    NDIS_PACKET *Packet,
    uint Offset)
{
    uchar Padding[64];
    uchar Hash[A_SHA_DIGEST_LEN];
    A_SHA_CTX Context;
    NDIS_BUFFER *Buffer;
    uchar *Data;
    uint Size;
    uint i;

    //
    // The code below relies on these restrictions,
    // but it could be generalized if desired.
    //
    ASSERT(Length <= A_SHA_DIGEST_LEN);
    ASSERT(CRYPTO_KEY_MAC_LENGTH <= sizeof Padding);

    //
    // Start off the inner hash.  I.e. "SHA1(Key XOR inner pad, ...".
    // The inner pad is 64 bytes of 0x36. Note our key is smaller.
    //
    A_SHAInit(&Context);
    for (i = 0; i < CRYPTO_KEY_MAC_LENGTH; i++)
        Padding[i] = CryptoKeyMAC[i] ^ 0x36;
    RtlFillMemory(Padding + CRYPTO_KEY_MAC_LENGTH,
                  sizeof Padding - CRYPTO_KEY_MAC_LENGTH,
                  0x36);
    A_SHAUpdate(&Context, Padding, sizeof Padding);

    //
    // Skip Offset bytes into the packet.
    //
    for (Buffer = NdisFirstBuffer(Packet); ; Buffer = Buffer->Next) {
        ASSERT(Buffer != NULL);
        NdisQueryBuffer(Buffer, &Data, &Size);

        if (Offset < Size) {
            Data += Offset;
            Size -= Offset;
            break;
        }

        Offset -= Size;
    }

    //
    // Continue the inner hash with data from the packet.
    //
    for (;;) {
        A_SHAUpdate(&Context, Data, Size);
        Buffer = Buffer->Next;
        if (Buffer == NULL)
            break;
        NdisQueryBuffer(Buffer, &Data, &Size);
    }

    //
    // Finish the inner hash.
    //
    A_SHAFinal(&Context, Hash);

    //
    // Start off the outer hash.  I.e. "SHA1(Key XOR outer pad, ...".
    // The outer pad is 64 bytes of 0x5c. Note our key is smaller.
    //
    A_SHAInit(&Context);
    for (i = 0; i < CRYPTO_KEY_MAC_LENGTH; i++)
        Padding[i] = CryptoKeyMAC[i] ^ 0x5c;
    RtlFillMemory(Padding + CRYPTO_KEY_MAC_LENGTH,
                  sizeof Padding - CRYPTO_KEY_MAC_LENGTH,
                  0x5c);
    A_SHAUpdate(&Context, Padding, sizeof Padding);

    //
    // Finish the outer hash.
    //
    A_SHAUpdate(&Context, Hash, sizeof Hash);
    A_SHAFinal(&Context, Hash);

    //
    // Copy the desired prefix of the HMAC-SHA1 result to our caller.
    //
    RtlCopyMemory(MAC, Hash, Length);

    //
    // Zero sensitive information.
    //
    RtlSecureZeroMemory(Padding, sizeof Padding);
}

//* CryptoEncryptDecryptData
//
//  Encrypts/decrypts a flat data buffer using AES.
//  The caller specifies the IV and the key.
//  The buffer length must be a block multiple.
//
static void
CryptoEncryptDecryptData(
    int Operation,
    uchar CryptoKeyAES[AES_KEYSIZE_128],
    uchar IV[LQSR_IV_LENGTH],
    void *Data,
    uint Length)
{
    AESTable_128 Key;
    uchar Feedback[AES_BLOCKLEN];

    ASSERT(LQSR_IV_LENGTH == AES_BLOCKLEN);
    ASSERT((Length & (AES_BLOCKLEN - 1)) == 0);

    aeskey((AESTable *)&Key, CryptoKeyAES, AES_ROUNDS_128);

    RtlCopyMemory(Feedback, IV, AES_BLOCKLEN);

    while (Length != 0) {
        CBC(aes128,
            AES_BLOCKLEN,
            Data,               // Output buffer.
            Data,               // Input buffer.
            &Key,
            Operation,
            Feedback);

        (uchar *)Data += AES_BLOCKLEN;
        Length -= AES_BLOCKLEN;
    }

    RtlSecureZeroMemory(&Key, sizeof Key);
}

//* CryptoEncryptPacket
//
//  Encrypts data, starting at Offset bytes into OrigPacket,
//  and generates an IV and a packet that holds the encrypted data.
//  The new packet can be released with NdisFreePacketClone.
//
NDIS_STATUS
CryptoEncryptPacket(
    MiniportAdapter *VA,
    NDIS_PACKET *OrigPacket,
    uint Offset,
    uchar IV[LQSR_IV_LENGTH],
    NDIS_PACKET **pEncryptedPacket)
{
    NDIS_STATUS Status;

    //
    // Actually, it is possible to disable crypto for testing purposes.
    //
    if (VA->Crypto) {
        NDIS_PACKET *Packet;
        NDIS_BUFFER *Buffer;
        void *Data;
        NDIS_BUFFER *OrigBuffer;
        void *OrigData;
        uint OrigBufferLength;
        uint OrigPacketLength;
        uint PlainTextLength;
        uint CryptoTextLength;
        uint NewPacketLength;

        //
        // Check the first buffer.
        // It must have at least Offset bytes.
        //
        NdisGetFirstBufferFromPacket(OrigPacket, &OrigBuffer, &OrigData,
                                     &OrigBufferLength, &OrigPacketLength);
        ASSERT(OrigPacketLength >= OrigBufferLength);
        if (OrigBufferLength < Offset)
            return NDIS_STATUS_BUFFER_TOO_SHORT;

        //
        // Round up to the block size.
        //
        PlainTextLength = OrigPacketLength - Offset;
        CryptoTextLength = ROUND_UP_COUNT(PlainTextLength, AES_BLOCKLEN);

        //
        // And be sure there is at least one padding byte.
        //
        if (PlainTextLength == CryptoTextLength)
            NewPacketLength = CryptoTextLength + AES_BLOCKLEN;
        else
            NewPacketLength = CryptoTextLength;

        //
        // Allocate the new packet.
        //
        MiniportAllocatePacket(VA, &Packet, &Status);
        if (Status != NDIS_STATUS_SUCCESS)
            return Status;

#if DBG
        RtlFillMemory(PC(Packet), sizeof(PacketContext), 0xcc);
#endif

        //
        // Allocate memory for the encrypted data from non-paged pool.
        //
        Data = ExAllocatePool(NonPagedPool, NewPacketLength);
        if (Data == NULL) {
            Status = NDIS_STATUS_RESOURCES;
            NdisFreePacket(Packet);
            return Status;
        }

        //
        // Allocate a buffer to describe the encrypted data region.
        //
        NdisAllocateBuffer(&Status, &Buffer, VA->BufferPool,
                           Data, NewPacketLength);
        if (Status != NDIS_STATUS_SUCCESS) {
            ExFreePool(Data);
            NdisFreePacket(Packet);
            return Status;
        }

        //
        // Generate an IV.
        //
        GetRandom(IV, LQSR_IV_LENGTH);

        //
        // Copy into the data buffer, first skipping Offset bytes.
        //
        (uchar *)OrigData += Offset;
        OrigBufferLength -= Offset;
        for (;;) {
            RtlCopyMemory(Data, OrigData, OrigBufferLength);

            (uchar *)Data += OrigBufferLength;

            OrigBuffer = OrigBuffer->Next;
            if (OrigBuffer == NULL)
                break;
            NdisQueryBuffer(OrigBuffer, &OrigData, &OrigBufferLength);
        }

        //
        // Fill the padding bytes.
        //
        RtlFillMemory(Data, NewPacketLength - PlainTextLength,
                      (uchar)(NewPacketLength - PlainTextLength));

        //
        // Encrypt the data.
        //
        CryptoEncryptDecryptData(ENCRYPT, VA->CryptoKeyAES, IV,
                                 Buffer->MappedSystemVa, NewPacketLength);

        //
        // Chain the new buffer to the new packet.
        //
        NdisChainBufferAtFront(Packet, Buffer);

        //
        // Initialize for NdisFreePacketClone.
        //
        PC(Packet)->OrigBuffer = NULL;
        PC(Packet)->CloneData = Buffer->MappedSystemVa;

        *pEncryptedPacket = Packet;
        return NDIS_STATUS_SUCCESS;
    }
    else {
        void *OrigHeader;
        void *CloneHeader;

        //
        // Instead of encrypting the packet, we create a clone
        // which omits Offset bytes at the beginning.
        // There is no padding to a block size.
        //
        Status = MiniportClonePacket(VA, OrigPacket,
                                     Offset, 0, 0,
                                     &OrigHeader,
                                     pEncryptedPacket,
                                     &CloneHeader);

        if (Status == NDIS_STATUS_SUCCESS) {
            //
            // Generate a dummy IV.
            //
            RtlZeroMemory(IV, LQSR_IV_LENGTH);
        }

        return Status;
    }
}

//* CryptoDecryptPacket
//
//  Decrypts data, using the supplied IV,
//  and generates a packet that has Offset bytes of space
//  followed by the decrypted data.
//  The new packet can be released with NdisFreePacketClone.
//
NDIS_STATUS
CryptoDecryptPacket(
    MiniportAdapter *VA,
    NDIS_PACKET *OrigPacket,
    uint OrigOffset,
    uint NewOffset,
    uchar IV[LQSR_IV_LENGTH],
    NDIS_PACKET **pDecryptedPacket)
{
    NDIS_STATUS Status;

    //
    // Actually, it is possible to disable crypto for testing purposes.
    //
    if (VA->Crypto) {
        NDIS_PACKET *Packet;
        NDIS_BUFFER *Buffer;
        void *Data;
        NDIS_BUFFER *OrigBuffer;
        void *OrigData;
        uint OrigBufferLength;
        uint OrigPacketLength;
        uint NewPacketLength;
        uint Padding;

        //
        // Get the first buffer.
        //
        NdisGetFirstBufferFromPacket(OrigPacket, &OrigBuffer, &OrigData,
                                     &OrigBufferLength, &OrigPacketLength);
        ASSERT(OrigPacketLength >= OrigBufferLength);
        ASSERT(OrigOffset <= OrigBufferLength);

        //
        // The encrypted data must be a block multiple.
        //
        if (((OrigPacketLength - OrigOffset) & (AES_BLOCKLEN - 1)) != 0) {
            KdPrint(("MCL!CryptoDecryptPacket: not block multiple\n"));
            return NDIS_STATUS_INVALID_PACKET;
        }

        //
        // Allocate the new packet.
        //
        MiniportAllocatePacket(VA, &Packet, &Status);
        if (Status != NDIS_STATUS_SUCCESS)
            return Status;

#if DBG
        RtlFillMemory(PC(Packet), sizeof(PacketContext), 0xcc);
#endif

        //
        // Allocate memory for the decrypted data from non-paged pool.
        //
        NewPacketLength = NewOffset + OrigPacketLength - OrigOffset;
        Data = ExAllocatePool(NonPagedPool, NewPacketLength);
        if (Data == NULL) {
            Status = NDIS_STATUS_RESOURCES;
            NdisFreePacket(Packet);
            return Status;
        }

        //
        // Allocate a buffer to describe the decrypted data region.
        //
        NdisAllocateBuffer(&Status, &Buffer, VA->BufferPool,
                           Data, NewPacketLength);
        if (Status != NDIS_STATUS_SUCCESS) {
            ExFreePool(Data);
            NdisFreePacket(Packet);
            return Status;
        }

        if (OrigOffset != OrigPacketLength) {
            //
            // Copy into the data buffer.
            //
            (uchar *)Data += NewOffset;
            (uchar *)OrigData += OrigOffset;
            OrigBufferLength -= OrigOffset;
            for (;;) {
                RtlCopyMemory(Data, OrigData, OrigBufferLength);

                (uchar *)Data += OrigBufferLength;

                OrigBuffer = OrigBuffer->Next;
                if (OrigBuffer == NULL)
                    break;
                NdisQueryBuffer(OrigBuffer, &OrigData, &OrigBufferLength);
            }

            //
            // Decrypt the data.
            //
            CryptoEncryptDecryptData(DECRYPT, VA->CryptoKeyAES, IV,
                                (uchar *)Buffer->MappedSystemVa + NewOffset,
                                OrigPacketLength - OrigOffset);

            //
            // There is always padding added at the end, and the padding bytes
            // tell us how much there is. But we must sanity-check the sender.
            //
            Padding = ((uchar *)Data)[-1];

            if ((Padding <= AES_BLOCKLEN) &&
                (OrigOffset + Padding <= OrigPacketLength)) {
                //
                // Shrink the buffer to eliminate the trailing padding bytes.
                //
                NdisAdjustBuffer(Buffer, Buffer->MappedSystemVa,
                                 NewPacketLength - Padding);
            }
            else {
                KdPrint(("MCL!CryptoDecryptPacket: bad padding %u\n", Padding));
                ExFreePool(Buffer->MappedSystemVa);
                NdisFreeBuffer(Buffer);
                NdisFreePacket(Packet);
                return NDIS_STATUS_INVALID_PACKET;
            }
        }

        //
        // Chain the new buffer to the new packet.
        //
        NdisChainBufferAtFront(Packet, Buffer);

        //
        // Initialize for NdisFreePacketClone.
        //
        PC(Packet)->OrigBuffer = NULL;
        PC(Packet)->CloneData = Buffer->MappedSystemVa;

        *pDecryptedPacket = Packet;
        return NDIS_STATUS_SUCCESS;
    }
    else {
        void *OrigHeader;
        void *CloneHeader;

        //
        // Instead of decrypting the packet, we create a clone.
        // There is no padding to remove.
        //
        Status = MiniportClonePacket(VA, OrigPacket,
                                     OrigOffset, NewOffset, VA->LookAhead,
                                     &OrigHeader,
                                     pDecryptedPacket,
                                     &CloneHeader);

        return Status;
    }
}
