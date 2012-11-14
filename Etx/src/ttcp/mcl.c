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

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>

#include "types.h"
#include "ether.h"
#include "lqsr.h"
#include "ntddndis.h"
#include "ntddmcl.h"

extern int ConvertHostName;

extern int
Translate(struct sockaddr *sa, UCHAR *phys);

__inline uint
SizeofNode(uint NumLinks)
{
    return sizeof(MCL_INFO_CACHE_NODE) + NumLinks * sizeof(MCL_INFO_LINK);
}

void
GetRouteChangesHelper(
    HANDLE Handle,
    MCL_QUERY_CACHE_NODE *Query,
    uint *pNumChanges)
{
    uint BufferSize;
    MCL_INFO_CACHE_NODE *Node;
    uint BytesReturned;
    uint NumAttempts = 0;

    //
    // Start with space for zero links.
    //
    BufferSize = SizeofNode(0);
    Node = (MCL_INFO_CACHE_NODE *) malloc(BufferSize);
    if (Node == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    for (;;) {
        if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_CACHE_NODE,
                             Query, sizeof *Query,
                             Node, BufferSize, &BytesReturned,
                             NULL)) {
            *pNumChanges = 0;
            return;
        }

        if ((BytesReturned < sizeof *Node) ||
            (BytesReturned > SizeofNode(Node->LinkCount))) {
            *pNumChanges = 0;
            return;
        }

        if (BytesReturned == SizeofNode(Node->LinkCount))
            break;

        if (++NumAttempts == 4) {
            *pNumChanges = 0;
            return;
        }

        BufferSize = SizeofNode(Node->LinkCount);
        free(Node);
        Node = (MCL_INFO_CACHE_NODE *) malloc(BufferSize);
        if (Node == NULL) {
            fprintf(stderr, "malloc failed\n");
            exit(1);
        }
    }

    *pNumChanges = Node->RouteChanges;
    free(Node);
}

void
GetRouteUsageHelper(
    HANDLE Handle,
    MCL_QUERY_CACHE_NODE *Node,
    uint *pNumRecords,
    MCL_INFO_ROUTE_USAGE **pHistory)
{
    uint BufferSize;
    MCL_INFO_ROUTE_USAGE *History;
    uint BytesReturned;
    uint NumRecords;

    for (BufferSize = 16;;) {
        History = (MCL_INFO_ROUTE_USAGE *) malloc(BufferSize);
        if (History == NULL) {
            fprintf(stderr, "malloc failed\n");
            exit(1);
        }

        if (DeviceIoControl(Handle, IOCTL_MCL_QUERY_ROUTE_USAGE,
                            Node, sizeof *Node,
                            History, BufferSize, &BytesReturned,
                            NULL))
            break;

        if (GetLastError() != ERROR_MORE_DATA) {
            *pNumRecords = 0;
            return;
        }

        BufferSize *= 2;
        free(History);
    }

    *pHistory = History;

    NumRecords = 0;
    while (BytesReturned != 0) {
        uint Size;
        NumRecords++;
        Size = sizeof *History + History->NumHops * sizeof History->Hops[0];
        (uchar *)History += Size;
        BytesReturned -= Size;
    }

    *pNumRecords = NumRecords;
}

void
GetRouteUsage(struct sockaddr *Dest,
              uint *pNumChanges,
              uint *pNumRecords,
              MCL_INFO_ROUTE_USAGE **pHistory)
{
    MCL_QUERY_CACHE_NODE Node;
    HANDLE Handle;

    Handle = CreateFileW(WIN_MCL_DEVICE_NAME,
                         0,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,   // security attributes
                         OPEN_EXISTING,
                         0,      // flags & attributes
                         NULL);  // template file
    if (Handle == INVALID_HANDLE_VALUE) {
        *pNumRecords = 0;
        return;
    }

    Node.VA.Index = 1;

    if (Translate(Dest, Node.Address)) {
        GetRouteChangesHelper(Handle, &Node, pNumChanges);
        GetRouteUsageHelper(Handle, &Node, pNumRecords, pHistory);
    }
    else
        *pNumRecords = 0;

    CloseHandle(Handle);
}

char *
FormatVirtualAddress(VirtualAddress Address)
{
    static char buffer[128];

    sprintf(buffer, "%02x-%02x-%02x-%02x-%02x-%02x",
            Address[0], Address[1], Address[2],
            Address[3], Address[4], Address[5]);

    return buffer;
}

//* FormatBandwidth
//  
//  Returns a formatted string containing the bandwidth value.
//
char *
FormatBandwidth(uint Bandwidth)
{
    static char Formatted[100];
    const char *Units;

    switch (Bandwidth & 3) {
    case 3:
        Units = "Tbps";
        break;
    case 2:
        Units = "Gbps";
        break;
    case 1:
        Units = "Mbps";
        break;
    case 0:
        Units = "Kbps";
        break;
    }

    sprintf(Formatted, "%u%s",
            Bandwidth >> 2, Units);
    return Formatted;
}

//* FormatLinkMetric
//  
//  Returns a formatted string containing the metric value. 
//
char *
FormatLinkMetric(
    uint Metric, 
    uint MetricType)
{
    static char Formatted[100];

    if (Metric == (uint)-1)
        strcpy(Formatted, "inf");
    else if (Metric == 0)
        strcpy(Formatted, "none");
    else {
        switch (MetricType) {
        case METRIC_TYPE_HOP:
            sprintf(Formatted, "%u", Metric);
            break;
        case METRIC_TYPE_ETX: {
            WCETTMetric *wcett = (WCETTMetric *)&Metric;
            sprintf(Formatted, "%.2f", wcett->LossProb / (double)4096);
            break;
        }
        case METRIC_TYPE_WCETT: {
            WCETTMetric *wcett = (WCETTMetric *)&Metric;
            sprintf(Formatted, "%.2f-%s-%u",
                    wcett->LossProb / (double)4096,
                    FormatBandwidth(wcett->Bandwidth),
                    wcett->Channel);
            break;
        }
        case METRIC_TYPE_RTT:
        case METRIC_TYPE_PKTPAIR:
            sprintf(Formatted, "%.2fms", (double)Metric/10000);
            break;
        default:
            fprintf(stderr, "Invalid metric type %u\n", MetricType);
            exit(1);
        }
    }
    return Formatted;
}

char *
FormatHostName(VirtualAddress Address)
{
    static char Host[NI_MAXHOST];
    struct sockaddr_in6 sin6;

    if (!ConvertHostName)
        return NULL;

    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr.s6_bytes[0] = 0x3f;
    sin6.sin6_addr.s6_bytes[1] = 0xfe;
    sin6.sin6_addr.s6_bytes[2] = 0x83;
    sin6.sin6_addr.s6_bytes[3] = 0x11;
    sin6.sin6_addr.s6_bytes[4] = 0xff;
    sin6.sin6_addr.s6_bytes[5] = 0xff;
    sin6.sin6_addr.s6_bytes[6] = 0xf7;
    sin6.sin6_addr.s6_bytes[7] = 0x1f;
    sin6.sin6_addr.s6_bytes[8] = Address[0] ^ 0x2;
    sin6.sin6_addr.s6_bytes[9] = Address[1];
    sin6.sin6_addr.s6_bytes[10] = Address[2];
    sin6.sin6_addr.s6_bytes[11] = 0xff;
    sin6.sin6_addr.s6_bytes[12] = 0xfe;
    sin6.sin6_addr.s6_bytes[13] = Address[3];
    sin6.sin6_addr.s6_bytes[14] = Address[4];
    sin6.sin6_addr.s6_bytes[15] = Address[5];
    
    if (getnameinfo((struct sockaddr *)&sin6,
                    sizeof sin6,
                    Host, sizeof Host,
                    NULL, 0,
                    NI_NOFQDN|NI_NAMEREQD) != 0)
        return NULL;

    return Host;
}

MCL_INFO_VIRTUAL_ADAPTER *
GetVirtualAdapterInfo(HANDLE Handle, MCL_QUERY_VIRTUAL_ADAPTER *Query)
{
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    uint BytesReturned;

    VA = malloc(sizeof *VA);
    if (VA == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    if (!DeviceIoControl(Handle,
                         IOCTL_MCL_QUERY_VIRTUAL_ADAPTER,
                         Query, sizeof *Query,
                         VA, sizeof *VA, &BytesReturned,
                         NULL)) {
        fprintf(stderr, "bad index %u\n", Query->Index);
        exit(1);
    }

    if (BytesReturned != sizeof *VA) {
        fprintf(stderr, "inconsistent adapter info length\n");
        exit(1);
    }

    return VA;
}

void
PrintRouteUsageHelper(FILE *File,
                      MCL_INFO_VIRTUAL_ADAPTER *VA,
                      MCL_INFO_ROUTE_USAGE *RU)
{
    char *Host;
    uint Hop;

    fprintf(File, "        %u:", RU->Usage);
    if (RU->NumHops == 0)
        fprintf(File, " no route");
    else for (Hop = 0; Hop < RU->NumHops; Hop++) {
        if (Hop != 0)
            fprintf(File, " %s",
                    FormatLinkMetric(RU->Hops[Hop].Metric, VA->MetricType));
        Host = FormatHostName(RU->Hops[Hop].Address);
        fprintf(File, " %s/%u/%u", (Host != NULL) ?
                Host : FormatVirtualAddress(RU->Hops[Hop].Address),
                RU->Hops[Hop].InIF, RU->Hops[Hop].OutIF);
    }
    fprintf(File, "\n");
}

void
PrintRouteUsage(
    FILE *File,
    uint NumChanges,
    uint NumBefore,
    MCL_INFO_ROUTE_USAGE *Before,
    uint NumAfter,
    MCL_INFO_ROUTE_USAGE *After)
{
    HANDLE Handle;
    MCL_QUERY_VIRTUAL_ADAPTER Query;
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    uint TotalPackets = 0;
    uint TotalHops = 0;
    uint NumRoutes = 0;
    uint i;

    if (NumAfter < NumBefore) {
        fprintf(File, "before/after route usage inconsistent\n");
        return;
    }

    Handle = CreateFileW(WIN_MCL_DEVICE_NAME,
                         0,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,   // security attributes
                         OPEN_EXISTING,
                         0,      // flags & attributes
                         NULL);  // template file
    if (Handle == INVALID_HANDLE_VALUE)
        return;

    Query.Index = 1;
    VA = GetVirtualAdapterInfo(Handle, &Query);
    if (VA == NULL) {
        CloseHandle(Handle);
        return;
    }

    fprintf(File, "    Route Usage:\n");

    for (i = 0; i < NumAfter; i++) {
        if (i >= NumAfter - NumBefore) {
            if (Before->NumHops != After->NumHops) {
                fprintf(File, "before/after route usage inconsistent\n");
                return;
            }
            After->Usage -= Before->Usage;
            (uchar *)Before += sizeof *Before +
                Before->NumHops * sizeof Before->Hops[0];
        }

        if (After->Usage != 0) {
            if (After->NumHops != 0) {
                NumRoutes++;
                TotalPackets += After->Usage;
                TotalHops += After->Usage * After->NumHops;
            }

            PrintRouteUsageHelper(File, VA, After);
        }

        (uchar *)After += sizeof *After +
            After->NumHops * sizeof After->Hops[0];
    }

    fprintf(File, "      num routes %u total packets %u num changes %u\n",
            NumRoutes, TotalPackets, NumChanges);
    fprintf(File, "      average path length %.2f hops\n",
            ((TotalPackets == 0) ?
             0.0 : ((double)TotalHops / TotalPackets) - 1.0));

    free(VA);
    CloseHandle(Handle);
}
