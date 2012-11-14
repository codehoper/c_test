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

//
// Prototypes for internal helper functions.
//
static SRPacket *FillSRPacket(SRPacket *SRP, LQSRHeader *LQSR);
static uint SROptionLength(const SRPacket *SRP);

//* SRFreeOptionList
//
//  Helper function for SRPacketFree.
//  Frees a list of options.
//
void
SRFreeOptionList(InternalOption *List)
{
    InternalOption *Opt;

    while ((Opt = List) != NULL) {
        List = Opt->Next;
        ExFreePool(Opt);
    }
}

//* SRPacketFree
//
//  Deallocate a SRPacket and all its sub-allocations.
//
void
SRPacketFree(SRPacket *SRP)
{
    SRFreeOptionList((InternalOption *)SRP->req);
    SRFreeOptionList((InternalOption *)SRP->rep);
    SRFreeOptionList((InternalOption *)SRP->err);
    SRFreeOptionList((InternalOption *)SRP->ackreq);
    SRFreeOptionList((InternalOption *)SRP->ack);
    SRFreeOptionList((InternalOption *)SRP->sr);
    SRFreeOptionList((InternalOption *)SRP->inforeq);
    SRFreeOptionList((InternalOption *)SRP->inforep);
    SRFreeOptionList((InternalOption *)SRP->Probe);
    SRFreeOptionList((InternalOption *)SRP->ProbeReply);
    SRFreeOptionList((InternalOption *)SRP->LinkInfo);

    if (SRP->Packet != NULL) {
        ASSERT(SRP->FreePacket != NULL);
        (*SRP->FreePacket)(SRP->PA, SRP->Packet);
    }

    ExFreePool(SRP);
}

//* SRPacketMAC
//
//  Calculates the MAC value for an LQSR packet.
//
static void
SRPacketMAC(
    MiniportAdapter *VA,
    uchar *MAC,
    NDIS_PACKET *Packet)
{
    if (VA->Crypto) {
        //
        // The normal case - real security.
        // We truncate the HMAC-SHA1 output to LQSR_MAC_LENGTH bytes.
        //
        CryptoMAC(VA->CryptoKeyMAC, MAC, LQSR_MAC_LENGTH, 
                  Packet, sizeof(EtherHeader) + LQSR_MAC_SKIP);
    }
    else {
        //
        // Security is disabled, but we still want version checking.
        // We just send the per-adapter key (which is not a strong secret).
        //
        ASSERT(CRYPTO_KEY_MAC_LENGTH == LQSR_MAC_LENGTH);
        RtlCopyMemory(MAC, VA->CryptoKeyMAC, LQSR_MAC_LENGTH);
    }
}

//* SRPacketFromPkt
//
//  Parses an LQSR packet into an SRPacket.
//
NDIS_STATUS
SRPacketFromPkt(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet,
    SRPacket **pSRP)
{
    uchar MAC[LQSR_MAC_LENGTH];
    SRPacket *SRP;
    void *OrigHeader;
    EtherHeader *OrigEther;
    LQSRHeader *OrigLQSR;
    NDIS_BUFFER *OrigBuffer;
    uint OrigBufferLength;
    uint HeaderLength;
    uint OrigPacketLength;
    InternalRouteRequest *Req;
    InternalRouteReply *Rep;
    InternalRouteError *Err;
    InternalAcknowledgementRequest *AckReq;
    InternalAcknowledgement *Ack;
    InternalSourceRoute *SR;
    InternalInfoRequest *InfoReq;
    InternalInfoReply *InfoRep;
    InternalProbe *Pro;
    InternalProbeReply *ProRep;
    InternalLinkInfo *LI;

    //
    // NB: Below code assumes the entire LQSR header is contiguous.
    //

    NdisGetFirstBufferFromPacket(Packet, &OrigBuffer, &OrigHeader,
                                 &OrigBufferLength, &OrigPacketLength);
    ASSERT(OrigBufferLength <= OrigPacketLength);
    if (OrigBufferLength < sizeof(EtherHeader) + sizeof(LQSRHeader))
        return NDIS_STATUS_BUFFER_TOO_SHORT;

    OrigEther = (EtherHeader *) OrigHeader;
    OrigLQSR = (LQSRHeader *) (OrigEther + 1);

    HeaderLength = (sizeof(EtherHeader) +
                    sizeof(LQSRHeader) +
                    OrigLQSR->HeaderLength);

    if (OrigBufferLength < HeaderLength)
        return NDIS_STATUS_BUFFER_TOO_SHORT;

    //
    // Verify the MAC.
    //
    SRPacketMAC(VA, MAC, Packet);
    if (! RtlEqualMemory(MAC, OrigLQSR->MAC, LQSR_MAC_LENGTH)) {
        InterlockedIncrement((PLONG)&VA->CountRecvBadMAC); 
        return NDIS_STATUS_INVALID_PACKET; 
    }

    SRP = ExAllocatePool(NonPagedPool, sizeof *SRP);
    if (SRP == NULL)
        return NDIS_STATUS_RESOURCES;
    RtlZeroMemory(SRP, sizeof *SRP);

    //
    // Parse the LQSR header options.
    //
    SRP = FillSRPacket(SRP, OrigLQSR);
    if (SRP == NULL) {
        //
        // Illegal packet format. FillSRPacket freed SRP.
        //
        return NDIS_STATUS_INVALID_PACKET;
    }

    //
    // Extract virtual source/destination addresses from the LQSR header.
    //
    if (SRP->sr != NULL) {
        uint Hops = SOURCE_ROUTE_HOPS(SRP->sr->opt.optDataLen);
        RtlCopyMemory(SRP->Source, SRP->sr->opt.hopList[0].addr, SR_ADDR_LEN);
        RtlCopyMemory(SRP->Dest, SRP->sr->opt.hopList[Hops - 1].addr,
                      SR_ADDR_LEN);
    }
    else if (SRP->req != NULL) {
        RtlCopyMemory(SRP->Source, SRP->req->opt.hopList[0].addr, SR_ADDR_LEN);
        RtlCopyMemory(SRP->Dest, SRP->req->opt.targetAddress, SR_ADDR_LEN);
    }
    else if ((SRP->ack != NULL) || (SRP->Probe != NULL) ||
             (SRP->ProbeReply != NULL)) {
        //
        // We don't have virtual source/destination addresses.
        //
        RtlZeroMemory(SRP->Source, SR_ADDR_LEN);
        RtlZeroMemory(SRP->Dest, SR_ADDR_LEN);
    }
    else {
        KdPrint(("MCL!Packet missing RR/SR/Ack!\n"));
        SRPacketFree(SRP);
        return NDIS_STATUS_INVALID_PACKET;
    }

    //
    // Acknowledgement Requests need a Source Route.
    //
    if ((SRP->ackreq != NULL) && (SRP->sr == NULL)) {
        KdPrint(("MCL!Packet with AckReq missing SR!\n"));
        SRPacketFree(SRP);
        return NDIS_STATUS_INVALID_PACKET;
    }

    //
    // Save information about the original packet.
    // NB: Calling SRPacketFree after doing this
    // and before our caller initializes SRP->FreePacket
    // will cause a bugcheck.
    //
    SRP->Packet = Packet;
    SRP->PacketLength = OrigPacketLength;
    SRP->PayloadOffset = HeaderLength;

    //
    // Save some info for later.
    //
    RtlCopyMemory(SRP->EtherDest, OrigEther->Dest, SR_ADDR_LEN);
    RtlCopyMemory(SRP->EtherSource, OrigEther->Source, SR_ADDR_LEN);
    RtlCopyMemory(SRP->IV, OrigLQSR->IV, LQSR_IV_LENGTH);

    //
    // Increment our counters.
    //
    for (Req = SRP->req; Req != NULL; Req = Req->next)
        InterlockedIncrement((PLONG)&VA->CountRecvRouteRequest);
    for (Rep = SRP->rep; Rep != NULL; Rep = Rep->next)
        InterlockedIncrement((PLONG)&VA->CountRecvRouteReply);
    for (Err = SRP->err; Err != NULL; Err = Err->next)
        InterlockedIncrement((PLONG)&VA->CountRecvRouteError);
    for (AckReq = SRP->ackreq; AckReq != NULL; AckReq = AckReq->next)
        InterlockedIncrement((PLONG)&VA->CountRecvAckRequest);
    for (Ack = SRP->ack; Ack != NULL; Ack = Ack->next)
        InterlockedIncrement((PLONG)&VA->CountRecvAck);
    for (SR = SRP->sr; SR != NULL; SR = SR->next)
        InterlockedIncrement((PLONG)&VA->CountRecvSourceRoute);
    for (InfoReq = SRP->inforeq; InfoReq != NULL; InfoReq = InfoReq->next)
        InterlockedIncrement((PLONG)&VA->CountRecvInfoRequest);
    for (InfoRep = SRP->inforep; InfoRep != NULL; InfoRep = InfoRep->next)
        InterlockedIncrement((PLONG)&VA->CountRecvInfoReply);
    for (Pro = SRP->Probe; Pro != NULL; Pro = Pro->Next)
        InterlockedIncrement((PLONG)&VA->CountRecvProbe);
    for (ProRep = SRP->ProbeReply; ProRep != NULL; ProRep = ProRep->Next)
        InterlockedIncrement((PLONG)&VA->CountRecvProbeReply);
    for (LI = SRP->LinkInfo; LI != NULL; LI = LI->Next)
        InterlockedIncrement((PLONG)&VA->CountRecvLinkInfo);

    *pSRP = SRP;
    return NDIS_STATUS_SUCCESS;
}

//* CheckPacket
//
//  This is a combination of SRPacketFromPkt and FillSRPacket
//  that just sanity-checks the packet, for debugging purposes.
//
NDIS_STATUS
CheckPacket(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet)
{
    uchar MAC[LQSR_MAC_LENGTH];
    void *OrigHeader;
    EtherHeader *OrigEther;
    LQSRHeader *OrigLQSR;
    NDIS_BUFFER *OrigBuffer;
    uint OrigBufferLength;
    uint HeaderLength;
    uint OrigPacketLength;

    LQSROption *Walk;
    uint Left;

    //
    // NB: Below code assumes the entire LQSR header is contiguous.
    //

    NdisGetFirstBufferFromPacket(Packet, &OrigBuffer, &OrigHeader,
                                 &OrigBufferLength, &OrigPacketLength);
    ASSERT(OrigBufferLength <= OrigPacketLength);
    if (OrigBufferLength < sizeof(EtherHeader) + sizeof(LQSRHeader)) {
        KdPrint(("MCL!CheckPacket: header truncated\n"));
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    OrigEther = (EtherHeader *) OrigHeader;
    OrigLQSR = (LQSRHeader *) (OrigEther + 1);

    HeaderLength = (sizeof(EtherHeader) +
                    sizeof(LQSRHeader) +
                    OrigLQSR->HeaderLength);

    if (OrigBufferLength < HeaderLength) {
        KdPrint(("MCL!CheckPacket: options truncated\n"));
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    //
    // Verify the MAC.
    //
    SRPacketMAC(VA, MAC, Packet);
    if (! RtlEqualMemory(MAC, OrigLQSR->MAC, LQSR_MAC_LENGTH)) {
        KdPrint(("MCL!CheckPacket: bad MAC\n"));
        return NDIS_STATUS_INVALID_PACKET;
    }

    //
    // Walk the options and verify them.
    //

    Walk = (LQSROption *)(OrigLQSR + 1);
    Left = OrigLQSR->HeaderLength;

    while (Left != 0) {
        //
        // Verify that we have enough data to match what the option
        // itself claims is present.
        //
        if ((Left < sizeof *Walk) ||
            (Left < sizeof *Walk + Walk->optDataLen)) {
            KdPrint(("MCL!CheckPacket: bad option length\n"));
            return NDIS_STATUS_INVALID_PACKET;
        }

        switch (Walk->optionType) {

        case LQSR_OPTION_TYPE_REQUEST:
            //
            // Route Request options must have at least one hop,
            // because the sending node includes itself.
            //
            if (Walk->optDataLen < ROUTE_REQUEST_LEN(1)) {
                KdPrint(("MCL!CheckPacket: bad request\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_REPLY:
            //
            // Route Replies must have at least two hops.
            //
            if (Walk->optDataLen < ROUTE_REPLY_LEN(2)) {
                KdPrint(("MCL!CheckPacket: bad reply\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_ERROR:
            if (Walk->optDataLen != ROUTE_ERROR_LENGTH) {
                KdPrint(("MCL!CheckPacket: bad error\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_SOURCERT:
            //
            // Source Routes must have at least two hops.
            //
            if (Walk->optDataLen < SOURCE_ROUTE_LEN(2)) {
                KdPrint(("MCL!CheckPacket: bad SR\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }

            //
            // Sanity-check segmentsLeft.
            //
            if ((((SourceRoute *)Walk)->segmentsLeft == 0) ||
                (((SourceRoute *)Walk)->segmentsLeft >=
                 SOURCE_ROUTE_HOPS(Walk->optDataLen))) {
                KdPrint(("MCL!CheckPacket: bad segmentsLeft\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_ACKREQ:
            if (Walk->optDataLen != ACK_REQUEST_LEN) {
                KdPrint(("MCL!CheckPacket: bad ack req\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_ACK:
            if (Walk->optDataLen != ACKNOWLEDGEMENT_LEN) {
                KdPrint(("MCL!CheckPacket: bad ack\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_INFOREQ:
            if (Walk->optDataLen != INFO_REQUEST_LEN) {
                KdPrint(("MCL!CheckPacket: bad info req\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_INFO:
            if (Walk->optDataLen != INFO_REPLY_LEN) {
                KdPrint(("MCL!CheckPacket: bad info reply\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_PROBE:
            //
            // First, make sure that we have enough
            // data left to read ProbeType correctly. 
            //
            if (Walk->optDataLen < PROBE_LEN) {
                KdPrint(("MCL!CheckPacket: bad probe\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }

            switch (((Probe *)Walk)->ProbeType) {
            case METRIC_TYPE_RTT:
                if (Walk->optDataLen != PROBE_LEN) {
                    KdPrint(("MCL!CheckPacket: bad rtt\n"));
                    return NDIS_STATUS_INVALID_PACKET;
                }
                break;
            case METRIC_TYPE_PKTPAIR:
                if (Walk->optDataLen != PROBE_LEN) {
                    KdPrint(("MCL!CheckPacket: bad pktpair\n"));
                    return NDIS_STATUS_INVALID_PACKET;
                }
                break;
            case METRIC_TYPE_ETX:
                if (Walk->optDataLen != PROBE_LEN + sizeof(EtxProbe)) {
                    KdPrint(("MCL!CheckPacket: bad etx\n"));
                    return NDIS_STATUS_INVALID_PACKET;
                }
                break;
            default:
                KdPrint(("MCL!CheckPacket: bad probe type\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_PROBEREPLY:
            //
            // First, make sure that we have enough
            // data left to read ProbeType correctly. 
            //
            if (Walk->optDataLen < PROBE_REPLY_LEN) {
                KdPrint(("MCL!CheckPacket: bad probe reply\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }

            switch (((Probe *)Walk)->ProbeType) {
            case METRIC_TYPE_RTT:
                if (Walk->optDataLen != PROBE_REPLY_LEN) {
                    KdPrint(("MCL!CheckPacket: bad rtt reply\n"));
                    return NDIS_STATUS_INVALID_PACKET;
                }
            case METRIC_TYPE_PKTPAIR:
                if (Walk->optDataLen != PROBE_REPLY_LEN + sizeof(PRPktPair)) {
                    KdPrint(("MCL!CheckPacket: bad pktpair reply\n"));
                    return NDIS_STATUS_INVALID_PACKET;
                }
                break;
            default:
                KdPrint(("MCL!CheckPacket: bad probe reply type\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        case LQSR_OPTION_TYPE_LINKINFO:
            //
            // LinkInfo options must contain at least one entry.
            //
            if (Walk->optDataLen < LINKINFO_LEN(1)) {
                KdPrint(("MCL!CheckPacket: bad linkinfo\n"));
                return NDIS_STATUS_INVALID_PACKET;
            }
            break;

        default:
            KdPrint(("MCL!CheckPacket: bad option type\n"));
            return NDIS_STATUS_INVALID_PACKET;
        }

        //
        // Move on to the next option (if any).
        //
        Left -= sizeof(LQSROption) + Walk->optDataLen;
        (uchar *)Walk += sizeof(LQSROption) + Walk->optDataLen;
    }

    return NDIS_STATUS_SUCCESS;
}

//* InsertOptions
//
//  Helper function for SRPacketToPkt.
//
static void
InsertOptions(LQSROption **Walk, void *Field, uchar Type)
{
    InternalOption *IntOpt;
    SIZE_T Amount;

    UNREFERENCED_PARAMETER(Type);  // Only used in checked builds.

    for (IntOpt = Field; IntOpt != NULL; IntOpt = IntOpt->Next) {

        ASSERT(IntOpt->Opt.optionType == Type);

        Amount = sizeof(LQSROption) + IntOpt->Opt.optDataLen;
        RtlCopyMemory(*Walk, &IntOpt->Opt, Amount);
        *Walk = (LQSROption *)(((char *)*Walk) + Amount);
    }
}

//* SRPacketToPkt
//
//  Converts the SRPacket to an NDIS packet that can be transmitted.
//  The NDIS packet should be deallocated with NdisFreePacketClone.
//
//  If the packet will be transmitted via a single physical adapter,
//  then PC(Packet)->PA is initialized.
//
NDIS_STATUS
SRPacketToPkt(
    MiniportAdapter *VA,
    const SRPacket *SRP,
    NDIS_PACKET **pPacket)
{
    uint HeaderLength = SROptionLength(SRP);
    NDIS_PACKET *Packet;
#if DBG
    uint PacketLength;
#endif
    void *CloneHeader;
    EtherHeader *CloneEther;
    LQSRHeader *CloneLQSR;
    LQSROption *Walk;
    NDIS_STATUS Status;
    const uchar *Dest;
    LQSRIf OutIf;
    LQSRIf InIf;
    ProtocolAdapter *PA;

    if (SRP->Packet == NULL) {
        ASSERT(SRP->PayloadOffset == 0);

        Status = MiniportMakeEmptyPacket(VA,
                        sizeof(EtherHeader) + sizeof(LQSRHeader) + HeaderLength,
                        &Packet, &CloneHeader);
    }
    else {
        void *OrigHeader;

        Status = MiniportClonePacket(VA, SRP->Packet,
                                     SRP->PayloadOffset,
                                     (sizeof(EtherHeader) +
                                      sizeof(LQSRHeader) +
                                      HeaderLength),
                                     0, // No lookahead needed.
                                     &OrigHeader, &Packet, &CloneHeader);
    }
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

#if DBG
    //
    // Check that we are not exceeding the physical link MTU.
    // The basic LQSR header fits in PROTOCOL_MIN_FRAME_SIZE (1500) -
    // MINIPORT_MAX_FRAME_SIZE (1280) bytes, and
    // the piggy-backing code will avoid adding too many options.
    //
    NdisQueryPacketLength(Packet, &PacketLength);
    ASSERT(PacketLength <= sizeof(EtherHeader) + PROTOCOL_MIN_FRAME_SIZE);
#endif

    CloneEther = (EtherHeader *) CloneHeader;
    CloneLQSR = (LQSRHeader *) (CloneEther + 1);
    Walk = (LQSROption *) (CloneLQSR + 1);

    CloneEther->Type = ETYPE_MSFT;
    CloneLQSR->Code = LQSR_CODE;

    // CloneLQSR->MAC computed below.
    RtlCopyMemory(CloneLQSR->IV, SRP->IV, LQSR_IV_LENGTH);
    CloneLQSR->HeaderLength = (ushort) HeaderLength;

    //
    // Figure out the packet's destination.
    //
    if (SRP->sr != NULL) {
        uint Index = (SOURCE_ROUTE_HOPS(SRP->sr->opt.optDataLen) -
                      SRP->sr->opt.segmentsLeft - 1);
        Dest = SRP->sr->opt.hopList[Index + 1].addr;
        //
        // If override is on, select the interface
        // based on RTT's advice. Otherwise use the
        // static route. 
        //
        if ((VA->MetricType == METRIC_TYPE_RTT) &&
            VA->MetricParams.Rtt.OutIfOverride) {
            RttSelectOutIf(VA, Dest, &OutIf, &InIf);
            SRP->sr->opt.hopList[Index].outif = OutIf;
            SRP->sr->opt.hopList[Index+1].inif = InIf;
        }
        else {
            OutIf = SRP->sr->opt.hopList[Index].outif;
            InIf = SRP->sr->opt.hopList[Index+1].inif;
        }
    }
    else if (SRP->Probe != NULL) {
        if (SRP->Probe->Opt.ProbeType == METRIC_TYPE_ETX) {
            PA = NULL;
            goto LinkLayerBroadcastPacket;
        }
        Dest = SRP->Probe->Opt.To;
        OutIf = SRP->Probe->Opt.OutIf;
        InIf = SRP->Probe->Opt.InIf;
    }
    else if (SRP->ProbeReply != NULL) {
        Dest = SRP->ProbeReply->Opt.To;
        OutIf = SRP->ProbeReply->Opt.OutIf;
        InIf = SRP->ProbeReply->Opt.InIf;
    }
    else {
        ASSERT(SRP->req != NULL);
        PA = NULL;
        goto LinkLayerBroadcastPacket;
    }

    //
    // Find the physical adapter with this index,
    // then check the neighbor cache.
    //
    PA = FindPhysicalAdapterFromIndex(VA, OutIf);
    if ((PA == NULL) ||
        ! NeighborFindPhysical(&VA->NC, Dest, InIf, CloneEther->Dest)) {
        //
        // We do not have a physical destination address.
        // Broadcast the packet on the physical link.
        //
    LinkLayerBroadcastPacket:
        RtlFillMemory(CloneEther->Dest, IEEE_802_ADDR_LENGTH, (uchar)0xff);
    }
    PC(Packet)->PA = PA;

    //
    // Reassembly order doesn't matter.
    //
    InsertOptions(&Walk, SRP->sr, LQSR_OPTION_TYPE_SOURCERT);
    InsertOptions(&Walk, SRP->req, LQSR_OPTION_TYPE_REQUEST);
    InsertOptions(&Walk, SRP->rep, LQSR_OPTION_TYPE_REPLY);
    InsertOptions(&Walk, SRP->err, LQSR_OPTION_TYPE_ERROR);
    InsertOptions(&Walk, SRP->ackreq, LQSR_OPTION_TYPE_ACKREQ);
    InsertOptions(&Walk, SRP->ack, LQSR_OPTION_TYPE_ACK);
    InsertOptions(&Walk, SRP->inforeq, LQSR_OPTION_TYPE_INFOREQ);
    InsertOptions(&Walk, SRP->inforep, LQSR_OPTION_TYPE_INFO);
    InsertOptions(&Walk, SRP->Probe, LQSR_OPTION_TYPE_PROBE);
    InsertOptions(&Walk, SRP->ProbeReply, LQSR_OPTION_TYPE_PROBEREPLY);
    InsertOptions(&Walk, SRP->LinkInfo, LQSR_OPTION_TYPE_LINKINFO);

    //
    // Compute the MAC.
    //
    SRPacketMAC(VA, CloneLQSR->MAC, Packet);

    *pPacket = Packet;
    return NDIS_STATUS_SUCCESS;
}


//* FillSRPacket
//
//  Parse LQSRHeader options into SRPacket representation.
//
static SRPacket *
FillSRPacket(SRPacket *SRP, LQSRHeader *LQSR)
{
    LQSROption *Walk = (LQSROption *)(LQSR + 1);
    uint Left = LQSR->HeaderLength;
    void **Field;
    SIZE_T Extra;
    void *Entry;

    while (Left != 0) {
        //
        // The PAD1 option is the only one that doesn't conform
        // to the generic LQSROption format.
        //
        if (Walk->optionType == LQSR_OPTION_TYPE_PAD1) {
            Left -= 1;
            (uchar *)Walk += 1;
            continue;
        }

        //
        // Verify that we have enough data to match what the option
        // itself claims is present.
        //
        if ((Left < sizeof *Walk) ||
            (Left < sizeof *Walk + Walk->optDataLen)) {
            KdPrint(("MCL!FillSRPacket: malformed option (type %u)\n",
                     Walk->optionType));
            goto Fail;
        }

        switch (Walk->optionType) {

        case LQSR_OPTION_TYPE_PADN:
            goto SkipForward;

        case LQSR_OPTION_TYPE_REQUEST:
            //
            // There should only be one Route Request option.
            //
            if (SRP->req != NULL) {
                KdPrint(("MCL!FillSRPacket: multiple Requests\n"));
                goto Fail;
            }

            //
            // Route Request options must have at least one hop,
            // because the sending node includes itself.
            //
            if (Walk->optDataLen < ROUTE_REQUEST_LEN(1)) {
                KdPrint(("MCL!FillSRPacket: bad Request length\n"));
                goto Fail;
            }

            Field = &SRP->req;
            Extra = ROUTE_REQUEST_LEN(MAX_SR_LEN);
            break;

        case LQSR_OPTION_TYPE_REPLY:
            //
            // Route Replies must have at least two hops.
            //
            if (Walk->optDataLen < ROUTE_REPLY_LEN(2)) {
                KdPrint(("MCL!FillSRPacket: small Reply\n"));
                goto Fail;
            }

            Field = &SRP->rep;
            Extra = ROUTE_REPLY_LEN(MAX_SR_LEN);
            break;

        case LQSR_OPTION_TYPE_ERROR:
            Field = &SRP->err;
            Extra = ROUTE_ERROR_LENGTH;
            break;

        case LQSR_OPTION_TYPE_SOURCERT:
            //
            // There should only be one Source Route option.
            //
            if (SRP->sr != NULL) {
                KdPrint(("MCL!FillSRPacket: multiple Source Routes\n"));
                goto Fail;
            }

            //
            // Source Routes must have at least two hops.
            //
            if (Walk->optDataLen < SOURCE_ROUTE_LEN(2)) {
                KdPrint(("MCL!FillSRPacket: small Source Route\n"));
                goto Fail;
            }

            //
            // Sanity-check segmentsLeft.
            //
            // REVIEW: This is a content, rather than a format check.
            // Shouldn't it be done elsewhere?
            //
            if ((((SourceRoute *)Walk)->segmentsLeft == 0) ||
                (((SourceRoute *)Walk)->segmentsLeft >=
                 SOURCE_ROUTE_HOPS(Walk->optDataLen))) {
                KdPrint(("MCL!FillSRPacket: bad segmentsLeft\n"));
                goto Fail;
            }

            Field = &SRP->sr;
            Extra = SOURCE_ROUTE_LEN(MAX_SR_LEN);
            break;

        case LQSR_OPTION_TYPE_ACKREQ:
            //
            // There should only be one AcknowledgementRequest option.
            //
            if (SRP->ackreq != NULL) {
                KdPrint(("MCL!FillSRPacket: multiple Ack Requests\n"));
                goto Fail;
            }

            Field = &SRP->ackreq;
            Extra = ACK_REQUEST_LEN;
            break;

        case LQSR_OPTION_TYPE_ACK:
            Field = &SRP->ack;
            Extra = ACKNOWLEDGEMENT_LEN;
            break;

        case LQSR_OPTION_TYPE_INFOREQ:
            Field = &SRP->inforeq;
            Extra = INFO_REQUEST_LEN;
            break;

        case LQSR_OPTION_TYPE_INFO:
            Field = &SRP->inforep;
            Extra = Walk->optDataLen;
            break;

        case LQSR_OPTION_TYPE_PROBE:
            //
            // There should only be one Probe option.
            //
            if (SRP->Probe != NULL) {
                KdPrint(("MCL!FillSRPacket: multiple Probes\n"));
                goto Fail;
            }

            Field = &SRP->Probe;

            //
            // The value of Extra depends on metric type. 
            // But first, make sure that we have enough
            // data left to read ProbeType correctly. 
            //
            if (Walk->optDataLen < PROBE_LEN) {
                KdPrint(("MCL!FillSRPacket: not enough data in probe packet.\n"));
            }

            // 
            // These happen to the same now, but this
            // is just for consistency. 
            // 
            if (((Probe *)Walk)->ProbeType == METRIC_TYPE_RTT) {
                Extra = PROBE_LEN;
            }
            else if (((Probe *)Walk)->ProbeType == METRIC_TYPE_PKTPAIR) {
                Extra = PROBE_LEN;
            }
            else if ((((Probe *)Walk)->ProbeType == METRIC_TYPE_ETX) &&
                     (Walk->optDataLen == PROBE_LEN + sizeof (EtxProbe))) {
                Extra = PROBE_LEN + sizeof (EtxProbe);
            }
            else {
                KdPrint(("MCL!FillSRPacket: unknown metric in Probe\n"));
                goto Fail;
            }
            break;

        case LQSR_OPTION_TYPE_PROBEREPLY:
            //
            // There should only be one ProbeReply option.
            //
            if (SRP->ProbeReply != NULL) {
                KdPrint(("MCL!FillSRPacket: multiple ProbeReplys\n"));
                goto Fail;
            }

            Field = &SRP->ProbeReply;

            //
            // The value of Extra depends on metric type. 
            // But first, make sure that we have enough
            // data left to read ProbeType correctly. 
            //
            if (Walk->optDataLen < PROBE_REPLY_LEN) {
                KdPrint(("MCL!FillSRPacket: not enough data in probe packet.\n"));
            }

            if (((Probe *)Walk)->ProbeType == METRIC_TYPE_RTT) {
                Extra = PROBE_REPLY_LEN;
            }
            else if ((((Probe *)Walk)->ProbeType == METRIC_TYPE_PKTPAIR) &&
                     (Walk->optDataLen == PROBE_REPLY_LEN + sizeof (PRPktPair))) {
                Extra = PROBE_REPLY_LEN + sizeof (PRPktPair);
            }
            else {
                KdPrint(("MCL!FillSRPacket: unknown metric in ProbeReply\n"));
                goto Fail;
            }
            break;

        case LQSR_OPTION_TYPE_LINKINFO:
            //
            // LinkInfo options must contain at least one entry.
            //
            if (Walk->optDataLen < LINKINFO_LEN(1)) {
                KdPrint(("MCL!FillSRPacket: bad LinkInfo length\n"));
                goto Fail;
            }

            Field = &SRP->LinkInfo;
            Extra = LINKINFO_LEN(LINKINFO_HOPS(Walk->optDataLen));
            break;

        default:
            //
            // REVIEW - Handle unknown options in some better way?
            //
            KdPrint(("MCL!FillSRPacket: unknown option %u\n",
                     Walk->optionType));
            goto Fail;
        }

        //
        // Verify that we don't have too much option data to fit
        // into our SRPacket representation.
        //
        if (Walk->optDataLen > Extra) {
            KdPrint(("MCL!FillSRPacket: option too big (type %u, len %u)\n",
                     Walk->optionType, Walk->optDataLen));
            goto Fail;
        }

        //
        // Allocate a new list entry for this field.
        //
        Entry = (InternalOption *)
            ExAllocatePool(NonPagedPool, sizeof(InternalOption) + Extra);
        if (Entry == NULL) {
            goto Fail;
        }

        //
        // Initialize the new list entry with the option data.
        //
        ((InternalOption *)Entry)->Next = *Field;
        *Field = Entry;
        RtlCopyMemory(&((InternalOption *)Entry)->Opt, Walk,
                      sizeof(LQSROption) + Walk->optDataLen);

      SkipForward:
        //
        // Move on to the next option (if any).
        //
        Left -= sizeof(LQSROption) + Walk->optDataLen;
        (uchar *)Walk += sizeof(LQSROption) + Walk->optDataLen;
    }

    return SRP;

 Fail:
    SRPacketFree(SRP);
    return NULL;
}

//* SROptionListLength
//
//  Helper function for SROptionLength.
//  Calculates the length (in on-wire packet terms) of a list of LQSR options.
//
uint
SROptionListLength(InternalOption *List)
{
    InternalOption *Opt;
    uint Length = 0;

    for (Opt = List; Opt != NULL; Opt = Opt->Next)
        Length += sizeof(LQSROption) + Opt->Opt.optDataLen;

    return Length;
}

//* SROptionLength
//
//  Calculate the length (in on-wire packet terms) of LQSR options as
//  represented in a SRPacket.
//
static uint
SROptionLength(const SRPacket *SRP)
{
    uint Length = 0;

    Length += SROptionListLength((InternalOption *)SRP->req);
    Length += SROptionListLength((InternalOption *)SRP->rep);
    Length += SROptionListLength((InternalOption *)SRP->err);
    Length += SROptionListLength((InternalOption *)SRP->ackreq);
    Length += SROptionListLength((InternalOption *)SRP->ack);
    Length += SROptionListLength((InternalOption *)SRP->sr);
    Length += SROptionListLength((InternalOption *)SRP->inforeq);
    Length += SROptionListLength((InternalOption *)SRP->inforep);
    Length += SROptionListLength((InternalOption *)SRP->Probe);
    Length += SROptionListLength((InternalOption *)SRP->ProbeReply);
    Length += SROptionListLength((InternalOption *)SRP->LinkInfo);

    return Length;
}    
