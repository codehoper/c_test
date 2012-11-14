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

#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>

extern int
TranslateIPv6(struct sockaddr_in6 *addr, UCHAR *phys);

int
TranslateIPv4(DWORD addr, UCHAR *phys)
{
    MIB_IPNETTABLE *Table;
    ULONG Size = 16;
    DWORD Error;
    DWORD i;
    int ret = FALSE;

    for (;;) {
        Table = (MIB_IPNETTABLE *) malloc(Size);
        if (Table == NULL) {
            printf("malloc failed\n");
            exit(1);
        }

        Error = GetIpNetTable(Table, &Size, FALSE);
        if (Error == NO_ERROR)
            break;

        if (Error != ERROR_INSUFFICIENT_BUFFER) {
            printf("GetIpNetTable: error %u\n", Error);
            exit(1);
        }

        free(Table);
    }

    for (i = 0; i < Table->dwNumEntries; i++) {
        MIB_IPNETROW *Row = &Table->table[i];

        if (Row->dwAddr == addr) {
            if (((Row->Type == MIB_IPNET_TYPE_DYNAMIC) ||
                 (Row->Type == MIB_IPNET_TYPE_STATIC)) &&
                (Row->dwPhysAddrLen == 6)) {

                memcpy(phys, Row->bPhysAddr, 6);
                ret = TRUE;
            }
            break;
        }
    }

    free(Table);
    return ret;
}

int
Translate(struct sockaddr *sa, UCHAR *phys)
{
    if (sa->sa_family == AF_INET)
        return TranslateIPv4(((struct sockaddr_in *)sa)->sin_addr.s_addr, phys);
    else if (sa->sa_family == AF_INET6)
        return TranslateIPv6((struct sockaddr_in6 *)sa, phys);
    else
        return FALSE;
}
