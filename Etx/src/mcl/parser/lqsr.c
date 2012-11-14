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

#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netmon.h>

#include "types.h"
#include "ether.h"
#include "lqsr.h"

#define FORMAT_BUFFER_SIZE      80
#define BIG_FORMAT_BUFFER_SIZE  1024

#define MIN(a, b) ((a) < (b) ? (a) : (b))

__inline uint
LQSRHeaderLength(LQSRHeader UNALIGNED *pLQSR)
{
    return pLQSR->HeaderLength;
}

__inline uint
LQSROptionLength(LQSROption UNALIGNED *pOption)
{
    return sizeof *pOption + pOption->optDataLen;
}

boolean
LQSRHeaderEncrypted(LQSRHeader UNALIGNED *pLQSR)
{
    uint UNALIGNED *IV = (uint UNALIGNED *) pLQSR->IV;

    return  (IV[0] != 0) || (IV[1] != 0) || (IV[2] != 0) || (IV[3] != 0);
}

//* LQSRFormatString
//
//  Formats a property using the supplied string for the text.
//
VOID WINAPIV
LQSRFormatString(LPPROPERTYINST lpPropertyInst)
{
    strcpy(lpPropertyInst->szPropertyText,
           lpPropertyInst->lpPropertyInstEx->Byte);
}

//* LQSRFormatNamedByte
//
//  Formats a property where the value is a byte
//  and there is a SET to translate the value to a string.
//
VOID WINAPIV
LQSRFormatNamedByte(LPPROPERTYINST lpPropertyInst)
{
    BYTE Byte;
    LPSET pSet;
    LPSTR pName;
    LPSTR p;

    Byte = * (BYTE *) GetPropertyInstanceData(lpPropertyInst);

    pSet = lpPropertyInst->lpPropertyInfo->lpSet;

    pName = LookupByteSetString(pSet, Byte);

    p = lpPropertyInst->szPropertyText;

    p += sprintf(p, "%s = 0x%02x",
                 lpPropertyInst->lpPropertyInfo->Label, Byte);

    if (pName != NULL)
        p += sprintf(p, " (%s)", pName);
}

//* LQSRFormatNamedWord
//
//  Formats a property where the value is a word
//  and there is a SET to translate the value to a string.
//  Byte-swaps the word.
//
VOID WINAPIV
LQSRFormatNamedWord(LPPROPERTYINST lpPropertyInst)
{
    WORD Word;
    LPSET pSet;
    LPSTR pName;
    LPSTR p;

    Word = * (WORD *) GetPropertyInstanceData(lpPropertyInst);
    Word = XCHG(Word);

    pSet = lpPropertyInst->lpPropertyInfo->lpSet;

    pName = LookupWordSetString(pSet, Word);

    p = lpPropertyInst->szPropertyText;

    p += sprintf(p, "%s = 0x%04X",
                 lpPropertyInst->lpPropertyInfo->Label, Word);

    if (pName != NULL)
        p += sprintf(p, " (%s)", pName);
}

//* LQSRFormatNamedDword
//
//  Formats a property where the value is a dword
//  and there is a SET to translate the value to a string.
//  Does NOT byte-swap the dword.
//
VOID WINAPIV
LQSRFormatNamedDword(LPPROPERTYINST lpPropertyInst)
{
    DWORD Dword;
    LPSET pSet;
    LPSTR pName;
    LPSTR p;

    Dword = * (DWORD *) GetPropertyInstanceData(lpPropertyInst);

    pSet = lpPropertyInst->lpPropertyInfo->lpSet;

    pName = LookupDwordSetString(pSet, Dword);

    p = lpPropertyInst->szPropertyText;

    p += sprintf(p, "%s = 0x%X",
                 lpPropertyInst->lpPropertyInfo->Label, Dword);

    if (pName != NULL)
        p += sprintf(p, " (%s)", pName);
}

//=============================================================================
//  LQSR property database.
//=============================================================================

LABELED_BYTE LQSROptions[] =
{
    { LQSR_OPTION_TYPE_PAD1, "Pad1" },
    { LQSR_OPTION_TYPE_PADN, "PadN" },
    { LQSR_OPTION_TYPE_REQUEST, "Route Request" },
    { LQSR_OPTION_TYPE_REPLY, "Route Reply" },
    { LQSR_OPTION_TYPE_ERROR, "Route Error" },
    { LQSR_OPTION_TYPE_ACKREQ, "Ack Request" },
    { LQSR_OPTION_TYPE_ACK, "Ack" },
    { LQSR_OPTION_TYPE_SOURCERT, "Source Route" },
    { LQSR_OPTION_TYPE_INFOREQ, "Info Request" },
    { LQSR_OPTION_TYPE_INFO, "Info" },
    { LQSR_OPTION_TYPE_PROBE, "Probe" },
    { LQSR_OPTION_TYPE_PROBEREPLY, "ProbeReply" },
};

SET LQSROptionsSet =
{
   sizeof( LQSROptions) / sizeof(LABELED_BYTE),
   LQSROptions
};

LABELED_DWORD LQSRMetricTypes[] =
{
    { METRIC_TYPE_HOP, "HOP" },
    { METRIC_TYPE_RTT, "RTT" },
    { METRIC_TYPE_PKTPAIR, "Packet Pair" },
    { METRIC_TYPE_ETX, "ETX" },
    { METRIC_TYPE_WCETT, "WCETT" },
};

SET LQSRMetricTypesSet =
{
   sizeof( LQSRMetricTypes) / sizeof(LABELED_DWORD),
   LQSRMetricTypes
};

SET LQSREtherTypeSet; // Initialized in LQSRRegister.

enum LQSR_PROP_IDS {
    LQSR_FRAME = 0,
    LQSR_ENCRYPTED_DATA,
    LQSR_DUMMY_DATA,
    LQSR_SUMMARY,
    LQSR_CODE_FIELD,
    LQSR_MAC,
    LQSR_IV,
    LQSR_NEXT_HEADER,
    LQSR_HEADER_LENGTH,
    LQSR_MALFORMED_OPTION,
    LQSR_INCOMPLETE_OPTION,
    LQSR_OPTION_TYPE,
    LQSR_OPTION_LENGTH,
    LQSR_OPTION_DATA,
    LQSR_OPTION_PADDING,
    LQSR_ROUTE_REQUEST,
    LQSR_REQ_ID,
    LQSR_ACK_ID,
    LQSR_ROUTE_REPLY,
    LQSR_ROUTE_ERROR,
    LQSR_ACKNOWLEDGEMENT_REQUEST,
    LQSR_ACKNOWLEDGEMENT,
    LQSR_SOURCERT,
    LQSR_UNKNOWN,
    LQSR_RESERVED_BITS,
    LQSR_STATIC_ROUTE,
    LQSR_SALVAGE_COUNT,
    LQSR_SEGMENTS_LEFT,
    LQSR_HOP_LIST,
    LQSR_SRADDR,
    LQSR_SRADDR_INIF,
    LQSR_SRADDR_OUTIF,
    LQSR_SRADDR_METRIC,
    MCL_INFO_REQ,
    MCL_INFO,
    LQSR_VERSION,
    MCL_INFO_FIELD,
    LQSR_PROBE,
    LQSR_PROBE_METRIC_TYPE,
    LQSR_PROBE_PROBE_TYPE,
    LQSR_PROBE_SEQNUM,
    LQSR_PROBE_TIMESTAMP,
    LQSR_PROBE_ETX_NUM_ENTRIES,
    LQSR_PROBE_ETX_ENTRY,
    LQSR_PROBE_ETX_RCVD,
    LQSR_PROBE_DATA,
    LQSR_PROBE_REPLY,
    LQSR_PROBE_REPLY_OUTDELTA,
    LQSR_PROBE_REPLY_DATA,
    LQSR_LINKINFO,
};

PROPERTYINFO LQSRDatabase[] =
{
    {   // LQSR_FRAME
        0,0, 
        "Frame",
        "LQSR Frame.", 
        PROP_TYPE_COMMENT,    
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_ENCRYPTED_DATA
        0,0,
        "Encrypted Data",
        "LQSR Encrypted Data.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_DUMMY_DATA
        0,0,
        "Dummy Data",
        "LQSR Dummy Data in Packet-Pair Probe.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_SUMMARY
        0,0, 
        "Header",
        "LQSR Header.",
        PROP_TYPE_COMMENT,    
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_CODE_FIELD
        0,0, 
        "Code",
        "MSFT EtherType Code.", 
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_MAC
        0,0, 
        "MAC",
        "LQSR Message Authentication Code.", 
        PROP_TYPE_COMMENT,    
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_IV
        0,0, 
        "IV",
        "LQSR Initialization Vector.", 
        PROP_TYPE_COMMENT,    
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_NEXT_HEADER
        0,0,
        "Next Header",
        "LQSR Next Header (EtherType of following header).",
        PROP_TYPE_BYTESWAPPED_WORD,
        PROP_QUAL_LABELED_SET,
        &LQSREtherTypeSet,
        FORMAT_BUFFER_SIZE,
        LQSRFormatNamedWord},

    {   // LQSR_HEADER_LENGTH
        // Formatted as a DWORD because we use LQSRHeaderLength.
        0,0,
        "Header Length",
        "LQSR Header Length.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_MALFORMED_OPTION
        0,0,
        "Malformed Option",
        "LQSR Malformed Option.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        LQSRFormatString},

    {   // LQSR_INCOMPLETE_OPTION
        0,0,
        "Incomplete Option",
        "LQSR Incomplete Option.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        LQSRFormatString},

    {   // LQSR_OPTION_TYPE
        0,0,
        "Type",
        "LQSR Option Type.",
        PROP_TYPE_BYTE,
        PROP_QUAL_LABELED_SET,
        &LQSROptionsSet,
        FORMAT_BUFFER_SIZE,
        LQSRFormatNamedByte},

    {   // LQSR_OPTION_LENGTH
        0,0,
        "Length",
        "LQSR Option Length.",
        PROP_TYPE_WORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_OPTION_DATA
        0,0,
        "Data",
        "LQSR Option Data.",
        PROP_TYPE_BYTE,
        PROP_QUAL_ARRAY,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_OPTION_PADDING
        0,0,
        "Padding",
        "LQSR Option Padding.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        LQSRFormatString},

    {   // LQSR_ROUTE_REQUEST
        0,0,
        "Route Request",
        "LQSR Route Request.", 
        PROP_TYPE_COMMENT,    
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_REQ_ID
        0,0,
        "Id",
        "LQSR Route Request Identification.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_ACK_ID
        0,0,
        "Id",
        "LQSR Acknowledgement Identification.",
        PROP_TYPE_WORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_ROUTE_REPLY
        0,0,
        "Route Reply",
        "LQSR Route Reply.", 
        PROP_TYPE_COMMENT,    
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_ROUTE_ERROR
        0,0,
        "Route Error",
        "LQSR Route Error.", 
        PROP_TYPE_COMMENT,    
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_ACKNOWLEDGEMENT_REQUEST
        0,0,
        "Ack Request",
        "LQSR Ack Request.", 
        PROP_TYPE_COMMENT,    
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        LQSRFormatString},

    {   // LQSR_ACKNOWLEDGEMENT
        0,0,
        "Ack",
        "LQSR Ack.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        LQSRFormatString},

    {   // LQSR_SOURCERT
        0,0, 
        "Source Route",
        "LQSR Source Route.", 
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        LQSRFormatString},

    {   // LQSR_UNKNOWN
        0,0, 
        "Unknown Option",
        "LQSR Unknown Option.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_RESERVED_BITS
        // Formatted as a DWORD because we extract the bits.
        0,0,
        "Reserved",
        "LQSR Reserved Bits.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_STATIC_ROUTE
        // Formatted as a DWORD because we extract the bits.
        0,0,
        "Static Route",
        "LQSR Static Route.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_SALVAGE_COUNT
        // Formatted as a DWORD because we extract the bits.
        0,0,
        "Salvage Count",
        "LQSR Salvage Count.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_SEGMENTS_LEFT
        // Formatted as a DWORD because we extract the bits.
        0,0,
        "Segments Left",
        "LQSR Segments Left.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_HOP_LIST
        0,0,
        "Hop List",
        "LQSR Hop List.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        LQSRFormatString},

    {   // LQSR_SRADDR
        0,0,
        "Address",
        "LQSR Address.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        LQSRFormatString},

    {   // LQSR_SRADDR_INIF
        0,0,
        "Ingress Interface Index",
        "LQSR Ingress Interface Index.",
        PROP_TYPE_BYTE,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_SRADDR_OUTIF
        0,0,
        "Egress Interface Index",
        "LQSR Egress Interface Index.",
        PROP_TYPE_BYTE,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_SRADDR_METRIC
        0,0,
        "Link Metric",
        "LQSR Link Metric.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // MCL_INFO_REQ
        0,0,
        "Info Req",
        "LQSR Information Request.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        LQSRFormatString},

    {   // MCL_INFO
        0,0,
        "Info",
        "LQSR Information.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        LQSRFormatString},

    {   // LQSR_VERSION
        0,0,
        "Version",
        "LQSR Version.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // MCL_INFO_FIELD
        0,0,
        "Info",
        "LQSR Information.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_PROBE
        0,0,
        "Probe",
        "LQSR Probe.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        LQSRFormatString},

    {   // LQSR_PROBE_METRIC_TYPE
        0,0,
        "MetricType",
        "LQSR Metric Type.",
        PROP_TYPE_DWORD,
        PROP_QUAL_LABELED_SET,
        &LQSRMetricTypesSet,
        FORMAT_BUFFER_SIZE,
        LQSRFormatNamedDword},

    {   // LQSR_PROBE_PROBE_TYPE
        0,0,
        "ProbeType",
        "LQSR Probe Type.",
        PROP_TYPE_DWORD,
        PROP_QUAL_LABELED_SET,
        &LQSRMetricTypesSet,
        FORMAT_BUFFER_SIZE,
        LQSRFormatNamedDword},

    {   // LQSR_PROBE_SEQNUM
        0,0,
        "Sequence Number",
        "LQSR Sequence Number.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_PROBE_TIMESTAMP
        0,0,
        "Timestamp",
        "LQSR Timestamp.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_PROBE_ETX_NUM_ENTRIES
        0,0,
        "Num Entries",
        "LQSR Number of Valid Entries.",
        PROP_TYPE_DWORD,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_PROBE_ETX_ENTRY
        0,0,
        "Entry",
        "LQSR ETX Probe Entry.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        LQSRFormatString},

    {   // LQSR_PROBE_ETX_RCVD
        0,0,
        "Received",
        "LQSR ETX Probe Received Count.",
        PROP_TYPE_WORD,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_PROBE_DATA
        0,0,
        "Probe Data",
        "LQSR Probe Data.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_PROBE_REPLY
        0,0,
        "Probe Reply",
        "LQSR Probe Reply.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        LQSRFormatString},

    {   // LQSR_PROBE_REPLY_OUTDELTA
        0,0,
        "OutDelta",
        "LQSR Probe Reply OutDelta.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},

    {   // LQSR_PROBE_REPLY_DATA
        0,0,
        "Probe Reply Data",
        "LQSR Probe Reply Data.",
        PROP_TYPE_COMMENT,
        PROP_QUAL_NONE,
        NULL,
        FORMAT_BUFFER_SIZE,
        FormatPropertyInstance},

    {   // LQSR_LINKINFO
        0,0,
        "Link Info",
        "LQSR Link Information.", 
        PROP_TYPE_COMMENT,    
        PROP_QUAL_NONE, 
        NULL,
        FORMAT_BUFFER_SIZE, 
        FormatPropertyInstance},
};

DWORD nLQSRProperties = ((sizeof LQSRDatabase) / PROPERTYINFO_SIZE);


LPHANDOFFTABLE lpLQSRHandoffTable;

//* LQSRRegister
//
//  Called by netmon to register the protocol.
//
VOID WINAPI
LQSRRegister(HPROTOCOL hLQSRProtocol)
{
    char IniFile[80];
    DWORD nLQSRHandoffSets;
    DWORD i;

    //
    // Create the EtherType table, using the predefined table in parser.lib.
    //
    LQSREtherTypeSet.lpLabeledWordTable = 
        GetProtocolDescriptionTable(&LQSREtherTypeSet.nEntries);

    //
    // Create the property database.
    //

    CreatePropertyDatabase(hLQSRProtocol, nLQSRProperties);

    for(i = 0; i < nLQSRProperties; ++i)
        AddProperty(hLQSRProtocol, &LQSRDatabase[i]);

    //
    // Create the protocol hand-off table.
    // We use the same hand-off table as ethernet.
    //

    BuildINIPath(IniFile, "mac.dll");

    nLQSRHandoffSets = CreateHandoffTable(
        "ETYPES", IniFile,
        &lpLQSRHandoffTable, 50, 16);
}


//* LQSRDeregister
//
//  Called by netmon to deregister the protocol.
//
VOID WINAPI
LQSRDeregister(HPROTOCOL hLQSRProtocol)
{
    DestroyPropertyDatabase(hLQSRProtocol);
    DestroyHandoffTable(lpLQSRHandoffTable);
}


//* LQSRRecognizeFrame
//
//  Called by netmon to recognize a frame.
//
LPBYTE WINAPI
LQSRRecognizeFrame(
    HFRAME          hFrame,                     //... frame handle.
    LPBYTE          MacFrame,                   //... Frame pointer.
    LPBYTE          LQSRFrame,                   //... Relative pointer.
    DWORD           MacType,                    //... MAC type.
    DWORD           BytesLeft,                  //... Bytes left.
    HPROTOCOL       hPreviousProtocol,          //... Previous protocol or NULL if none.
    DWORD           nPreviousProtocolOffset,    //... Offset of previous protocol.
    LPDWORD         ProtocolStatusCode,         //... Pointer to return status code in.
    LPHPROTOCOL     hNextProtocol,              //... Next protocol to call (optional).
    LPDWORD         pInstData)                   //... Next protocol instance data.
{
    LQSRHeader UNALIGNED *pLQSR = (LQSRHeader UNALIGNED *) LQSRFrame;
    LQSRTrailer UNALIGNED *pLQSRTrailer;
    uint HeaderLength;
    uint i;

    //
    // We must have the base LQSR header to recognize the frame.
    //
    if ((BytesLeft < sizeof *pLQSR) ||
        (pLQSR->Code != LQSR_CODE)) {
        *ProtocolStatusCode = PROTOCOL_STATUS_NOT_RECOGNIZED;
        return LQSRFrame;
    }

    //
    // If this frame is encrypted, just claim it.
    //
    if (LQSRHeaderEncrypted(pLQSR)) {
        *ProtocolStatusCode = PROTOCOL_STATUS_CLAIMED;
        return NULL;
    }

    //
    // If we do not have the complete header, just claim the frame.
    //
    HeaderLength = LQSRHeaderLength(pLQSR);
    if (BytesLeft < sizeof *pLQSR + HeaderLength + sizeof *pLQSRTrailer) {
        *ProtocolStatusCode = PROTOCOL_STATUS_CLAIMED;
        return NULL;
    }

    //
    // Also claim the frame if there is no next protocol.
    //
    pLQSRTrailer = (LQSRTrailer UNALIGNED *) ((uchar *)(pLQSR + 1) + HeaderLength);
    if ((pLQSRTrailer->NextHeader == ETYPE_MSFT) ||
        (pLQSRTrailer->NextHeader == 0)) {
        *ProtocolStatusCode = PROTOCOL_STATUS_CLAIMED;
        return NULL;
    }

    //
    // Check if we recognize the following protocol.
    //
    *hNextProtocol = GetProtocolFromTable(lpLQSRHandoffTable,
                                          XCHG(pLQSRTrailer->NextHeader),
                                          pInstData);
    if (*hNextProtocol != NULL) {
        //
        // Netmon will continue parsing with the next protocol.
        //
        *ProtocolStatusCode = PROTOCOL_STATUS_NEXT_PROTOCOL;
    }
    else {
        //
        // We don't recognize the following protocol.
        // Netmon will consult our follow set to continue parsing.
        //
        *ProtocolStatusCode = PROTOCOL_STATUS_RECOGNIZED;
    }

    return (LPBYTE)(pLQSRTrailer + 1);
}

VOID WINAPI
LQSRAttachOptionPadding(HFRAME hFrame,
                        LQSROption UNALIGNED *pOption, uint Bytes)
{
    char Text[FORMAT_BUFFER_SIZE];

    sprintf(Text, "Padding (%u bytes)", Bytes);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_OPTION_PADDING].hProperty,
        Bytes,
        pOption,
        strlen(Text),
        Text,
        0, 1, 0);
}

VOID WINAPI
LQSRAttachOptionType(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_OPTION_TYPE].hProperty,
        sizeof pOption->optionType,
        &pOption->optionType,
        0, 2, 0);
}

VOID WINAPI
LQSRAttachOptionLength(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_OPTION_LENGTH].hProperty,
        sizeof pOption->optDataLen,
        &pOption->optDataLen,
        0, 2, 0);
}

VOID WINAPI
LQSRAttachOptionPad1(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    LQSRAttachOptionPadding(hFrame, pOption, 1);
    LQSRAttachOptionType(hFrame, pOption);
}

VOID WINAPI
LQSRAttachOptionPadN(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    LQSRAttachOptionPadding(hFrame, pOption, LQSROptionLength(pOption));
    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    if (pOption->optDataLen != 0) {
        AttachPropertyInstance(hFrame,
            LQSRDatabase[LQSR_OPTION_DATA].hProperty,
            pOption->optDataLen,
            pOption->payload,
            0, 2, 0);
    }
}

VOID WINAPI
LQSRAttachSRAddr(HFRAME hFrame, SRAddr UNALIGNED *pSRA)
{
    char Text[FORMAT_BUFFER_SIZE];

    sprintf(Text, "%02x-%02x-%02x-%02x-%02x-%02x",
            pSRA->addr[0], pSRA->addr[1], pSRA->addr[2],
            pSRA->addr[3], pSRA->addr[4], pSRA->addr[5]);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_SRADDR].hProperty,
        sizeof *pSRA,
        pSRA,
        strlen(Text),
        Text,
        0, 3, 0);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_SRADDR].hProperty,
        sizeof pSRA->addr,
        pSRA->addr,
        strlen(Text),
        Text,
        0, 4, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_INIF].hProperty,
        sizeof pSRA->inif,
        &pSRA->inif,
        0, 4, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_OUTIF].hProperty,
        sizeof pSRA->outif,
        &pSRA->outif,
        0, 4, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_METRIC].hProperty,
        sizeof pSRA->Metric,
        &pSRA->Metric,
        0, 4, 0);
}

VOID WINAPI
LQSRAttachHopList(HFRAME hFrame,
                  SRAddr UNALIGNED *pSRA,
                  SRAddr UNALIGNED *pSRAEnd)
{
    char Text[128];
    uint NumAddrs = pSRAEnd - pSRA;

    switch (NumAddrs) {
    case 0:
        //
        // This is bizarre. We should never see this.
        //
        strcpy(Text, "Hop List (no addresses!)");
        break;

    case 1:
        //
        // We should only see this in Route Requests.
        //
        sprintf(Text, "Hop List (from %02x-%02x-%02x-%02x-%02x-%02x)",
                pSRA->addr[0], pSRA->addr[1], pSRA->addr[2],
                pSRA->addr[3], pSRA->addr[4], pSRA->addr[5]);
        break;

    case 2:
        sprintf(Text, "Hop List (from "
                "%02x-%02x-%02x-%02x-%02x-%02x to "
                "%02x-%02x-%02x-%02x-%02x-%02x)",
                pSRA->addr[0], pSRA->addr[1], pSRA->addr[2],
                pSRA->addr[3], pSRA->addr[4], pSRA->addr[5],
                pSRAEnd[-1].addr[0], pSRAEnd[-1].addr[1], pSRAEnd[-1].addr[2],
                pSRAEnd[-1].addr[3], pSRAEnd[-1].addr[4], pSRAEnd[-1].addr[5]);
        break;

    default:
        sprintf(Text, "Hop List (%u hops from "
                "%02x-%02x-%02x-%02x-%02x-%02x to "
                "%02x-%02x-%02x-%02x-%02x-%02x)",
                NumAddrs - 1,
                pSRA->addr[0], pSRA->addr[1], pSRA->addr[2],
                pSRA->addr[3], pSRA->addr[4], pSRA->addr[5],
                pSRAEnd[-1].addr[0], pSRAEnd[-1].addr[1], pSRAEnd[-1].addr[2],
                pSRAEnd[-1].addr[3], pSRAEnd[-1].addr[4], pSRAEnd[-1].addr[5]);
        break;
    }

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_HOP_LIST].hProperty,
        (LPBYTE)pSRAEnd - (LPBYTE)pSRA,
        pSRA,
        strlen(Text),
        Text,
        0, 2, 0);

    while (pSRA < pSRAEnd)
        LQSRAttachSRAddr(hFrame, pSRA++);
}

VOID WINAPI
LQSRAttachAddress(HFRAME hFrame, const char *Name, uint Level,
                  VirtualAddress Address)
{
    char Text[FORMAT_BUFFER_SIZE];

    sprintf(Text, "%s = %02x-%02x-%02x-%02x-%02x-%02x",
            Name,
            Address[0], Address[1], Address[2],
            Address[3], Address[4], Address[5]);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_SRADDR].hProperty,
        sizeof(VirtualAddress),
        Address,
        strlen(Text),
        Text,
        0, Level, 0);
}

VOID WINAPI
LQSRAttachRouteRequest(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    uint OptionLength = LQSROptionLength(pOption);
    RouteRequest UNALIGNED *pRR = (RouteRequest UNALIGNED *) pOption;

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_ROUTE_REQUEST].hProperty,
        OptionLength,
        pOption,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_REQ_ID].hProperty,
        sizeof pRR->identification,
        &pRR->identification,
        0, 2, 0);

    LQSRAttachAddress(hFrame, "Target", 2, pRR->targetAddress);
    LQSRAttachHopList(hFrame, pRR->hopList,
                      (SRAddr UNALIGNED *)((LPBYTE)pOption + OptionLength));
}

VOID WINAPI
LQSRAttachRouteReply(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    uint OptionLength = LQSROptionLength(pOption);
    RouteReply UNALIGNED *pRR = (RouteReply UNALIGNED *) pOption;
    uint ReservedBits;

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_ROUTE_REPLY].hProperty,
        OptionLength,
        pOption,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    ReservedBits = pRR->Reserved;
    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_RESERVED_BITS].hProperty,
        sizeof pRR->Reserved,
        &pRR->Reserved,
        sizeof ReservedBits,
        &ReservedBits,
        0, 2, 0);

    LQSRAttachHopList(hFrame, pRR->hopList,
                      (SRAddr UNALIGNED *)((LPBYTE)pOption + OptionLength));
}

VOID WINAPI
LQSRAttachRouteError(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    RouteError UNALIGNED *pRE = (RouteError UNALIGNED *) pOption;

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_ROUTE_ERROR].hProperty,
        sizeof *pRE,
        pRE,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    LQSRAttachAddress(hFrame, "Error Source", 2, pRE->errorSrc);
    LQSRAttachAddress(hFrame, "Error Destination", 2, pRE->errorDst);
    LQSRAttachAddress(hFrame, "Unreachable Node", 2, pRE->unreachNode);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_INIF].hProperty,
        sizeof pRE->inIf,
        &pRE->inIf,
        0, 3, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_OUTIF].hProperty,
        sizeof pRE->outIf,
        &pRE->outIf,
        0, 3, 0);
}

VOID WINAPI
LQSRAttachAcknowledgementRequest(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    char Text[64];
    AcknowledgementRequest UNALIGNED *pAckReq =
        (AcknowledgementRequest UNALIGNED *) pOption;

    sprintf(Text, "Ack Request (Id = %u)", pAckReq->identification);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_ACKNOWLEDGEMENT_REQUEST].hProperty,
        sizeof *pAckReq,
        pAckReq,
        strlen(Text),
        Text,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_ACK_ID].hProperty,
        sizeof pAckReq->identification,
        &pAckReq->identification,
        0, 2, 0);
}

VOID WINAPI
LQSRAttachAcknowledgement(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    char Text[64];
    Acknowledgement UNALIGNED *pAck =
        (Acknowledgement UNALIGNED *) pOption;

    sprintf(Text, "Ack (Id = %u)", pAck->identification);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_ACKNOWLEDGEMENT].hProperty,
        sizeof *pAck,
        pAck,
        strlen(Text),
        Text,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_ACK_ID].hProperty,
        sizeof pAck->identification,
        &pAck->identification,
        0, 2, 0);

    LQSRAttachAddress(hFrame, "From", 2, pAck->from);
    LQSRAttachAddress(hFrame, "To", 2, pAck->to);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_INIF].hProperty,
        sizeof pAck->inif,
        &pAck->inif,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_OUTIF].hProperty,
        sizeof pAck->outif,
        &pAck->outif,
        0, 2, 0);
}

VOID WINAPI
LQSRAttachSourceRoute(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    char Text[64];
    uint OptionLength = LQSROptionLength(pOption);
    SourceRoute UNALIGNED *pSR = (SourceRoute UNALIGNED *) pOption;
    uint ReservedBits, StaticRoute, SalvageCount, SegmentsLeft;
    uint NumAddrs;

    SegmentsLeft = pSR->segmentsLeft;
    NumAddrs = ((SRAddr UNALIGNED *)((LPBYTE)pOption + OptionLength)
                - pSR->hopList);

    switch (NumAddrs) {
    case 0:
        strcpy(Text, "Source Route (no addresses!)");
        break;
    case 1:
        strcpy(Text, "Source Route (no hops!)");
        break;
    default:
        sprintf(Text, "Source Route (%u of %u hops left)",
                SegmentsLeft, NumAddrs - 1);
        break;
    }

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_SOURCERT].hProperty,
        OptionLength,
        pOption,
        strlen(Text),
        Text,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    ReservedBits = pSR->reservedField;
    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_RESERVED_BITS].hProperty,
        sizeof pSR->misc,
        &pSR->misc,
        sizeof ReservedBits,
        &ReservedBits,
        0, 2, 0);

    StaticRoute = pSR->staticRoute;
    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_STATIC_ROUTE].hProperty,
        sizeof pSR->misc,
        &pSR->misc,
        sizeof StaticRoute,
        &StaticRoute,
        0, 2, 0);

    SalvageCount = pSR->salvageCount;
    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_SALVAGE_COUNT].hProperty,
        sizeof pSR->misc,
        &pSR->misc,
        sizeof SalvageCount,
        &SalvageCount,
        0, 2, 0);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_SEGMENTS_LEFT].hProperty,
        sizeof pSR->misc,
        &pSR->misc,
        sizeof SegmentsLeft,
        &SegmentsLeft,
        0, 2, 0);

    LQSRAttachHopList(hFrame, pSR->hopList,
                      (SRAddr UNALIGNED *)((LPBYTE)pOption + OptionLength));
}

VOID WINAPI
LQSRAttachInfoRequest(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    char Text[64];
    InfoRequest UNALIGNED *pInfoReq =
        (InfoRequest UNALIGNED *) pOption;

    sprintf(Text, "Info Request (Id = %u)", pInfoReq->identification);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[MCL_INFO_REQ].hProperty,
        sizeof *pInfoReq,
        pInfoReq,
        strlen(Text),
        Text,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_REQ_ID].hProperty,
        sizeof pInfoReq->identification,
        &pInfoReq->identification,
        0, 2, 0);

    LQSRAttachAddress(hFrame, "Source", 2, pInfoReq->sourceAddress);
}

VOID WINAPI
LQSRAttachInfoReply(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    char Text[64];
    InfoReply UNALIGNED *pInfo = (InfoReply UNALIGNED *) pOption;
    uint OptionLength = LQSROptionLength(pOption);

    sprintf(Text, "Info (Id = %u, Version = %u)",
            pInfo->identification, pInfo->version);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[MCL_INFO].hProperty,
        OptionLength,
        pInfo,
        strlen(Text),
        Text,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_REQ_ID].hProperty,
        sizeof pInfo->identification,
        &pInfo->identification,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_VERSION].hProperty,
        sizeof pInfo->version,
        &pInfo->version,
        0, 2, 0);

    if (OptionLength != sizeof *pInfo) {
        AttachPropertyInstance(hFrame,
            LQSRDatabase[MCL_INFO_FIELD].hProperty,
            OptionLength - sizeof *pInfo,
            pInfo->info,
            0, 2, 0);
    }
}

VOID WINAPI
LQSRAttachProbe(HFRAME hFrame, LQSROption UNALIGNED *pOption,
                DWORD BytesLeft)
{
    char Text[128];
    Probe UNALIGNED *pProbe =
        (Probe UNALIGNED *) pOption;
    DWORD SpecialSize;

    switch (pProbe->ProbeType) {
    case METRIC_TYPE_RTT:
        sprintf(Text, "RTT Probe");
        SpecialSize = 0;
        break;
    case METRIC_TYPE_PKTPAIR:
        sprintf(Text, "PktPair Probe");
        SpecialSize = 0;
        break;
    case METRIC_TYPE_ETX:
        sprintf(Text, "ETX Probe");
        SpecialSize = sizeof(EtxProbe);
        break;
    default:
        sprintf(Text, "Unknown Probe");
        SpecialSize = BytesLeft;
        break;
    }

    if (SpecialSize != BytesLeft)
        sprintf(Text, "Malformed Probe");

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_PROBE].hProperty,
        sizeof *pProbe,
        pProbe,
        strlen(Text),
        Text,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_PROBE_METRIC_TYPE].hProperty,
        sizeof pProbe->MetricType,
        &pProbe->MetricType,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_PROBE_PROBE_TYPE].hProperty,
        sizeof pProbe->ProbeType,
        &pProbe->ProbeType,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_PROBE_SEQNUM].hProperty,
        sizeof pProbe->Seq,
        &pProbe->Seq,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_PROBE_TIMESTAMP].hProperty,
        sizeof pProbe->Timestamp,
        &pProbe->Timestamp,
        0, 2, 0);

    LQSRAttachAddress(hFrame, "From", 2, pProbe->From);
    LQSRAttachAddress(hFrame, "To", 2, pProbe->To);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_INIF].hProperty,
        sizeof pProbe->InIf,
        &pProbe->InIf,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_OUTIF].hProperty,
        sizeof pProbe->OutIf,
        &pProbe->OutIf,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_METRIC].hProperty,
        sizeof pProbe->Metric,
        &pProbe->Metric,
        0, 2, 0);

    if ((pProbe->ProbeType == METRIC_TYPE_ETX) &&
        (BytesLeft == sizeof(EtxProbe))) {
        EtxProbe UNALIGNED *pEtxProbe =
            (EtxProbe UNALIGNED *)(pProbe + 1);
        uint i;
        uint NumEntries;

        AttachPropertyInstance(hFrame,
            LQSRDatabase[LQSR_PROBE_ETX_NUM_ENTRIES].hProperty,
            sizeof pEtxProbe->NumEntries,
            &pEtxProbe->NumEntries,
            0, 2, 0);

        NumEntries = MIN(pEtxProbe->NumEntries, MAX_ETX_ENTRIES);

        for (i = 0; i < NumEntries; i++) {

            sprintf(Text,
                    "From %u/%02x-%02x-%02x-%02x-%02x-%02x via %u Rcvd %u",
                    pEtxProbe->Entry[i].OutIf,
                    pEtxProbe->Entry[i].From[0],
                    pEtxProbe->Entry[i].From[1],
                    pEtxProbe->Entry[i].From[2],
                    pEtxProbe->Entry[i].From[3],
                    pEtxProbe->Entry[i].From[4],
                    pEtxProbe->Entry[i].From[5],
                    pEtxProbe->Entry[i].InIf,
                    pEtxProbe->Entry[i].Rcvd);

            AttachPropertyInstanceEx(hFrame,
                LQSRDatabase[LQSR_PROBE_ETX_ENTRY].hProperty,
                sizeof pEtxProbe->Entry[i],
                &pEtxProbe->Entry[i],
                strlen(Text),
                Text,
                0, 2, 0);

            LQSRAttachAddress(hFrame, "From", 3,
                              pEtxProbe->Entry[i].From);

            AttachPropertyInstance(hFrame,
                LQSRDatabase[LQSR_SRADDR_OUTIF].hProperty,
                sizeof pEtxProbe->Entry[i].OutIf,
                &pEtxProbe->Entry[i].OutIf,
                0, 3, 0);

            AttachPropertyInstance(hFrame,
                LQSRDatabase[LQSR_SRADDR_INIF].hProperty,
                sizeof pEtxProbe->Entry[i].InIf,
                &pEtxProbe->Entry[i].InIf,
                0, 3, 0);

            AttachPropertyInstance(hFrame,
                LQSRDatabase[LQSR_PROBE_ETX_RCVD].hProperty,
                sizeof pEtxProbe->Entry[i].Rcvd,
                &pEtxProbe->Entry[i].Rcvd,
                0, 3, 0);
        }

        if (NumEntries < MAX_ETX_ENTRIES) {
            AttachPropertyInstance(hFrame,
                LQSRDatabase[LQSR_PROBE_DATA].hProperty,
                (sizeof pEtxProbe->Entry[0]) * (MAX_ETX_ENTRIES - NumEntries),
                &pEtxProbe->Entry[NumEntries],
                0, 2, 0);
        }
    }
    else if (BytesLeft != 0) {
        AttachPropertyInstance(hFrame,
            LQSRDatabase[LQSR_PROBE_DATA].hProperty,
            BytesLeft,
            pProbe + 1,
            0, 2, 0);
    }
}

VOID WINAPI
LQSRAttachProbeReply(HFRAME hFrame, LQSROption UNALIGNED *pOption,
                     DWORD BytesLeft)
{
    char Text[64];
    ProbeReply UNALIGNED *pProbeReply =
        (ProbeReply UNALIGNED *) pOption;
    DWORD SpecialSize;

    switch (pProbeReply->ProbeType) {
    case METRIC_TYPE_RTT:
        sprintf(Text, "RTT Probe Reply");
        SpecialSize = 0;
        break;
    case METRIC_TYPE_PKTPAIR:
        sprintf(Text, "PktPair Probe Reply");
        SpecialSize = sizeof(PRPktPair);
        break;
    default:
        sprintf(Text, "Unknown Probe Reply");
        SpecialSize = BytesLeft;
        break;
    }

    if (SpecialSize != BytesLeft)
        sprintf(Text, "Malformed Probe Reply");

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_PROBE_REPLY].hProperty,
        sizeof *pProbeReply,
        pProbeReply,
        strlen(Text),
        Text,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_PROBE_METRIC_TYPE].hProperty,
        sizeof pProbeReply->MetricType,
        &pProbeReply->MetricType,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_PROBE_PROBE_TYPE].hProperty,
        sizeof pProbeReply->ProbeType,
        &pProbeReply->ProbeType,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_PROBE_SEQNUM].hProperty,
        sizeof pProbeReply->Seq,
        &pProbeReply->Seq,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_PROBE_TIMESTAMP].hProperty,
        sizeof pProbeReply->Timestamp,
        &pProbeReply->Timestamp,
        0, 2, 0);

    LQSRAttachAddress(hFrame, "From", 2, pProbeReply->From);
    LQSRAttachAddress(hFrame, "To", 2, pProbeReply->To);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_INIF].hProperty,
        sizeof pProbeReply->InIf,
        &pProbeReply->InIf,
        0, 2, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SRADDR_OUTIF].hProperty,
        sizeof pProbeReply->OutIf,
        &pProbeReply->OutIf,
        0, 2, 0);

    if ((pProbeReply->ProbeType == METRIC_TYPE_PKTPAIR) &&
        (BytesLeft == sizeof(PRPktPair))) {
        PRPktPair UNALIGNED *pPRPktPair =
            (PRPktPair UNALIGNED *)(pProbeReply + 1);

        AttachPropertyInstance(hFrame,
            LQSRDatabase[LQSR_PROBE_REPLY_OUTDELTA].hProperty,
            sizeof pPRPktPair->OutDelta,
            &pPRPktPair->OutDelta,
            0, 2, 0);
    }
    else if (BytesLeft != 0) {
        AttachPropertyInstance(hFrame,
            LQSRDatabase[LQSR_PROBE_REPLY_DATA].hProperty,
            BytesLeft,
            pProbeReply + 1,
            0, 2, 0);
    }
}

VOID WINAPI
LQSRAttachLinkInfo(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    uint OptionLength = LQSROptionLength(pOption);
    LinkInfo UNALIGNED *pLI = (LinkInfo UNALIGNED *) pOption;

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_LINKINFO].hProperty,
        OptionLength,
        pOption,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    LQSRAttachAddress(hFrame, "From", 2, pLI->From);

    LQSRAttachHopList(hFrame, pLI->Links,
                      (SRAddr UNALIGNED *)((LPBYTE)pOption + OptionLength));
}

VOID WINAPI
LQSRAttachUnknownOption(HFRAME hFrame, LQSROption UNALIGNED *pOption)
{
    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_UNKNOWN].hProperty,
        LQSROptionLength(pOption),
        pOption,
        0, 1, 0);

    LQSRAttachOptionType(hFrame, pOption);
    LQSRAttachOptionLength(hFrame, pOption);

    if (pOption->optDataLen != 0) {
        AttachPropertyInstance(hFrame,
            LQSRDatabase[LQSR_OPTION_DATA].hProperty,
            pOption->optDataLen,
            pOption->payload,
            0, 2, 0);
    }
}

VOID WINAPI
LQSRAttachMalformedOption(HFRAME hFrame,
                          LQSROption UNALIGNED *pOption,
                          uint OptionLength)
{
    char Text[FORMAT_BUFFER_SIZE];

    sprintf(Text, "Malformed %s",
            LookupByteSetString(&LQSROptionsSet,
                                pOption->optionType));

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_MALFORMED_OPTION].hProperty,
        OptionLength,
        pOption,
        strlen(Text),
        Text,
        0, 1, IFLAG_ERROR);
}

VOID WINAPI
LQSRAttachIncompleteOption(HFRAME hFrame,
                           LQSROption UNALIGNED *pOption,
                           uint OptionLength)
{
    char Text[FORMAT_BUFFER_SIZE];

    sprintf(Text, "Incomplete %s",
            LookupByteSetString(&LQSROptionsSet,
                                pOption->optionType));

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_INCOMPLETE_OPTION].hProperty,
        OptionLength,
        pOption,
        strlen(Text),
        Text,
        0, 1, IFLAG_ERROR);
}

//* LQSRAttachProperties
//
//  Called by netmon to attach properties.
//
LPBYTE WINAPI
LQSRAttachProperties(
    HFRAME    hFrame,
    LPBYTE    Frame,
    LPBYTE    LQSRFrame,
    DWORD     MacType,
    DWORD     BytesLeft,
    HPROTOCOL hPreviousProtocol,
    DWORD     nPreviousProtocolOffset,
    DWORD     InstData)
{
    LQSRHeader UNALIGNED *pLQSR = (LQSRHeader UNALIGNED *) LQSRFrame;
    uint HeaderLength = LQSRHeaderLength(pLQSR);
    LQSRTrailer UNALIGNED *pLQSRTrailer = (LQSRTrailer UNALIGNED *)
        ((uchar *)(pLQSR + 1) + HeaderLength);
    LQSROption UNALIGNED *pOption;

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_SUMMARY].hProperty,
        MIN(BytesLeft, sizeof *pLQSR + HeaderLength + sizeof *pLQSRTrailer),
        pLQSR,
        0, 0, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_CODE_FIELD].hProperty,
        sizeof pLQSR->Code,
        &pLQSR->Code,
        0, 1, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_MAC].hProperty,
        sizeof pLQSR->MAC,
        pLQSR->MAC,
        0, 1, 0);

    AttachPropertyInstance(hFrame,
        LQSRDatabase[LQSR_IV].hProperty,
        sizeof pLQSR->IV,
        pLQSR->IV,
        0, 1, 0);

    AttachPropertyInstanceEx(hFrame,
        LQSRDatabase[LQSR_HEADER_LENGTH].hProperty,
        sizeof pLQSR->HeaderLength,
        &pLQSR->HeaderLength,
        sizeof HeaderLength,
        &HeaderLength,
        0, 1, 0);

    BytesLeft -= sizeof *pLQSR;
    pOption = (LQSROption UNALIGNED *) (pLQSR + 1);

    while (HeaderLength != 0) {
        uint OptionLength;

        if (pOption->optionType == LQSR_OPTION_TYPE_PAD1)
            OptionLength = 1;
        else if (sizeof *pOption > HeaderLength) {
            LQSRAttachMalformedOption(hFrame, pOption, 1);
            break;
        }
        else if (sizeof *pOption > BytesLeft) {
            LQSRAttachIncompleteOption(hFrame, pOption, 1);
            break;
        }
        else
            OptionLength = LQSROptionLength(pOption);

        if (OptionLength > HeaderLength) {
            LQSRAttachMalformedOption(hFrame, pOption,
                MIN(OptionLength, MIN(BytesLeft, HeaderLength)));
            break;
        }

        if (OptionLength > BytesLeft) {
            LQSRAttachIncompleteOption(hFrame, pOption,
                MIN(OptionLength, MIN(BytesLeft, HeaderLength)));
            break;
        }

        switch (pOption->optionType) {
        case LQSR_OPTION_TYPE_PAD1:
            LQSRAttachOptionPad1(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_PADN:
            LQSRAttachOptionPadN(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_REQUEST:
            if ((OptionLength < sizeof(RouteRequest)) ||
                ((OptionLength - sizeof(RouteRequest)) % sizeof(SRAddr) != 0))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachRouteRequest(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_REPLY:
            if ((OptionLength < sizeof(RouteReply)) ||
                ((OptionLength - sizeof(RouteReply)) % sizeof(SRAddr) != 0))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachRouteReply(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_ERROR:
            if (OptionLength != sizeof(RouteError))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachRouteError(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_ACKREQ:
            if (OptionLength != sizeof(AcknowledgementRequest))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachAcknowledgementRequest(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_ACK:
            if (OptionLength != sizeof(Acknowledgement))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachAcknowledgement(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_SOURCERT:
            if ((OptionLength < sizeof(SourceRoute)) ||
                ((OptionLength - sizeof(SourceRoute)) % sizeof(SRAddr) != 0))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachSourceRoute(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_INFOREQ:
            if (OptionLength != sizeof(InfoRequest))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachInfoRequest(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_INFO:
            if (OptionLength < sizeof(InfoReply))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachInfoReply(hFrame, pOption);
            break;

        case LQSR_OPTION_TYPE_PROBE:
            if (OptionLength < sizeof(Probe))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachProbe(hFrame, pOption,
                                OptionLength - sizeof(Probe));
            break;

        case LQSR_OPTION_TYPE_PROBEREPLY:
            if (OptionLength < sizeof(ProbeReply))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachProbeReply(hFrame, pOption,
                                     OptionLength - sizeof(ProbeReply));
            break;

        case LQSR_OPTION_TYPE_LINKINFO:
            if ((OptionLength < sizeof(LinkInfo)) ||
                ((OptionLength - sizeof(LinkInfo)) % sizeof(SRAddr) != 0))
                LQSRAttachMalformedOption(hFrame, pOption, OptionLength);
            else
                LQSRAttachLinkInfo(hFrame, pOption);
            break;

        default:
            LQSRAttachUnknownOption(hFrame, pOption);
            break;
        }

        BytesLeft -= OptionLength;
        HeaderLength -= OptionLength;
        (LPBYTE)pOption += OptionLength;
    }

    if (LQSRHeaderEncrypted(pLQSR)) {
        //
        // The LQSR trailer and payload bytes following are encrypted.
        //
        if (BytesLeft != 0) {
            AttachPropertyInstance(hFrame,
                LQSRDatabase[LQSR_ENCRYPTED_DATA].hProperty,
                BytesLeft,
                pOption,
                0, 1, 0);
        }
    }
    else {
        //
        // If present, the LQSR trailer is not encrypted.
        //
        if (BytesLeft >= sizeof *pLQSRTrailer) {
            if (pLQSRTrailer->NextHeader == 0) {
                //
                // This is dummy data in a large packet-pair probe packet.
                //
                AttachPropertyInstance(hFrame,
                    LQSRDatabase[LQSR_DUMMY_DATA].hProperty,
                    BytesLeft,
                    pOption,
                    0, 1, 0);
            }
            else {
                AttachPropertyInstance(hFrame,
                    LQSRDatabase[LQSR_NEXT_HEADER].hProperty,
                    sizeof pLQSRTrailer->NextHeader,
                    &pLQSRTrailer->NextHeader,
                    0, 1, 0);
            }
        }
    }

    return NULL;
}

//* LQSRFormatProperties
//
//  Called by netmon to format the properties.
//
DWORD WINAPI
LQSRFormatProperties(
    HFRAME hFrame,
    LPBYTE MacFrame,
    LPBYTE FrameData,
    DWORD nPropertyInsts,
    LPPROPERTYINST pPropertyInsts)
{
    uint i;

    //
    // Format each property in the property instance table.
    //
    // The property-specific instance data was used to store the address of a
    // property-specific formatting function so all we do here is call each
    // function via the instance data pointer.
    //

    for (i = 0; i < nPropertyInsts; i++) {
        LPPROPERTYINST p = &pPropertyInsts[i];

        ((FORMAT) p->lpPropertyInfo->InstanceData)(p);
    }

    return NMERR_SUCCESS;
}

ENTRYPOINTS LQSREntryPoints = {
    LQSRRegister,
    LQSRDeregister,
    LQSRRecognizeFrame,
    LQSRAttachProperties,
    LQSRFormatProperties
};

HPROTOCOL hLQSR = NULL;

//* DllMain
//
//  Called when this DLL loads or unloads.
//
BOOL WINAPI
DllMain(HANDLE hInstance, ULONG Command, LPVOID Reserved)
{
    switch (Command) {
    case DLL_PROCESS_ATTACH: {
        hLQSR = CreateProtocol("LQSR", &LQSREntryPoints, ENTRYPOINTS_SIZE);
        break;
    }

    case DLL_PROCESS_DETACH:
        DestroyProtocol(hLQSR);
        break;
    }

    return TRUE;
}
