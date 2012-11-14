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
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#include <ntddip6.h>
#include <tdiinfo.h>
#include <tdistat.h>
#include <ipinfo.h>
#include <ntddtcp.h>
#include <stdio.h>
#include <stdlib.h>

unsigned int
GetIPv6InterfaceIndex(struct sockaddr_in6 *addr)
{
    UCHAR Buffer[FIELD_OFFSET(TCP_REQUEST_QUERY_INFORMATION_EX, Context) +
                 sizeof(TDI_ADDRESS_IP6)];
    TCP_REQUEST_QUERY_INFORMATION_EX *Query =
        (TCP_REQUEST_QUERY_INFORMATION_EX *) Buffer;
    TDI_ADDRESS_IP6 *Addr = (TDI_ADDRESS_IP6 *) Query->Context;
    IP6RouteEntry routeEntry;
    ULONG BytesReturned;
    HANDLE Handle;
    NTSTATUS            status;
    UNICODE_STRING      DeviceName;
    OBJECT_ATTRIBUTES   objectAttributes;
    IO_STATUS_BLOCK     iosb;

    RtlInitUnicodeString(&DeviceName, DD_TCPV6_DEVICE_NAME);

    InitializeObjectAttributes(
        &objectAttributes,
        &DeviceName,
        OBJ_CASE_INSENSITIVE,       // attributes
        NULL,
        NULL
        );

    status = NtCreateFile(&Handle,
                          GENERIC_READ, // desired access
                          &objectAttributes,
                          &iosb,
                          NULL, // allocation size
                          0, // flags & attributes
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN, // create disposition
                          0, // create flags
                          NULL, // EaBuffer
                          0); // EaSize
    if (! NT_SUCCESS(status))
        return 0;

    Query->ID.toi_entity.tei_entity = CL_NL_ENTITY;
    Query->ID.toi_entity.tei_instance = 0;
    Query->ID.toi_class = INFO_CLASS_PROTOCOL;
    Query->ID.toi_type = INFO_TYPE_PROVIDER;
    Query->ID.toi_id =  IP6_GET_BEST_ROUTE_ID;

    Addr->sin6_port = 0;
    Addr->sin6_flowinfo = 0;
    memcpy(Addr->sin6_addr, &addr->sin6_addr, sizeof(struct in6_addr));
    Addr->sin6_scope_id = addr->sin6_scope_id;

    if (!DeviceIoControl(Handle, IOCTL_TCP_QUERY_INFORMATION_EX,
                         Buffer, sizeof Buffer,
                         &routeEntry, sizeof routeEntry,
                         &BytesReturned, NULL)) {
        routeEntry.ire_IfIndex = 0;
    }

    CloseHandle(Handle);

    return routeEntry.ire_IfIndex;
}

int
TranslateIPv6NC(struct sockaddr_in6 *addr, UCHAR *phys)
{
    uchar buffer[sizeof(IPV6_INFO_NEIGHBOR_CACHE) + MAX_LINK_LAYER_ADDRESS_LENGTH];
    IPV6_INFO_NEIGHBOR_CACHE *NCE = (IPV6_INFO_NEIGHBOR_CACHE *) buffer;
    IPV6_QUERY_NEIGHBOR_CACHE Query;
    uint BytesReturned;
    HANDLE Handle;
    int ret;

    Query.IF.Index = GetIPv6InterfaceIndex(addr);
    if (Query.IF.Index == 0)
        return FALSE;

    memcpy(&Query.Address, &addr->sin6_addr, sizeof(struct in6_addr));

    Handle = CreateFileW(WIN_IPV6_DEVICE_NAME,
                         0,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,   // security attributes
                         OPEN_EXISTING,
                         0,      // flags & attributes
                         NULL);  // template file
    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;

    ret = DeviceIoControl(Handle, IOCTL_IPV6_QUERY_NEIGHBOR_CACHE,
                          &Query, sizeof Query,
                          buffer, sizeof buffer, &BytesReturned,
                          NULL);
    CloseHandle(Handle);
    if (! ret)
        return FALSE;

    if ((BytesReturned < sizeof *NCE) ||
        (BytesReturned != sizeof *NCE + NCE->LinkLayerAddressLength) ||
        (NCE->LinkLayerAddressLength != 6))
        return FALSE;

    if (NCE->NDState == ND_STATE_INCOMPLETE)
        return FALSE;

    memcpy(phys, NCE + 1, 6);
    return TRUE;
}

int
TranslateIPv6(struct sockaddr_in6 *addr, UCHAR *phys)
{
    if (TranslateIPv6NC(addr, phys))
        return TRUE;

    if ((addr->sin6_addr.s6_bytes[8] ^ 0x2) == 0)
        return FALSE;

    phys[0] = addr->sin6_addr.s6_bytes[8] ^ 0x2;
    phys[1] = addr->sin6_addr.s6_bytes[9];
    phys[2] = addr->sin6_addr.s6_bytes[10];
    phys[3] = addr->sin6_addr.s6_bytes[13];
    phys[4] = addr->sin6_addr.s6_bytes[14];
    phys[5] = addr->sin6_addr.s6_bytes[15];
    return TRUE;
}
