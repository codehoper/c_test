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
#include <math.h>
#include <string.h>
#include <setupapi.h>

#include "types.h"
#include "ether.h"
#include "lqsr.h"
#include "ntddndis.h"
#include "ntddmcl.h"

//
// Multipliers to convert 100ns time unit. 
//

#define MICROSECOND (10)
#define MILLISECOND (1000 * MICROSECOND)
#define SECOND      (1000 * MILLISECOND)
#define MINUTE      (60 * SECOND)

//
// The minimum congestion window, in time units.
//
#define WCETT_CWMIN     (320 * MICROSECOND)

extern void
AddOrRemoveMCL(boolint AddOrRemove, const char *SetupKitDir);

extern int
ControlDeviceClass(LPTSTR Query, DWORD Action);

HANDLE Handle;

boolint Persistent = FALSE;
boolint Verbose = FALSE;
boolint AdminAccess = TRUE;
boolint ConvertHostName = TRUE; // Whether to query names or just print addrs.

__inline int
TimeToSeconds(Time T)
{
    return (int) ((__int64)T / (__int64)(10*1000*1000));
}

__inline int
TimeToMillis(Time T)
{
    return (int) ((__int64)T / (__int64)(10*1000));
}

__inline int
TimeToMicros(Time T)
{
    return (int) ((__int64)T / (__int64)(10));
}


//* WcettDecodeBandwidth
//
//  Converts bandwidth in our 12-bit encoded format to bps.
//  Bandwidth is encoded with a 10-bit mantissa and a 2-bit exponent:
//      bps = Bmant * (1000 ^ (Bexp + 1))
//  In other words, Bexp = 0 means Kbps, Bexp = 1 means Mbps, etc.
//
uint
WcettDecodeBandwidth(uint Bandwidth)
{
    uint Bexp = Bandwidth & 3;
    uint Bmant = Bandwidth >> 2;

    Bandwidth = Bmant * 1000;
    while (Bexp != 0) {
        Bandwidth *= 1000;
        Bexp--;
    }

    return Bandwidth;
}

//* WcettConvETT
//
//  Converts a link metric (loss probability & bandwidth) to ETT.
//  The ETT value uses 100-ns time units.
//
uint
WcettConvETT(uint LinkMetric)
{
    WCETTMetric *wcett = (WCETTMetric *)&LinkMetric;
    uint Temp;
    uint Backoff;
    uint Bandwidth;
    uint Transmit;

    //
    // First calculate a temp value (scaled by 4096)
    // from the loss probability (which is scaled by 4096):
    //   Temp = 1 + p + 2p^2 + 4p^3 + 8p^4 + 16p^5 + 32p^6 + 64p^7
    //
    Temp = 4096 + 2 * wcett->LossProb;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + 2 * wcett->LossProb * Temp) / 4096;
    Temp = (4096*4096 + wcett->LossProb * Temp) / 4096;
    //
    // Now finish the backoff calculation, converting to time units:
    //  Backoff = (CWmin / 2) * (Temp / (1 - p))
    //
    Backoff = (WCETT_CWMIN * Temp) / (2 * (4096 - wcett->LossProb));

    //
    // Calculate the transmission time for a 1024-byte packet,
    // converting to time units:
    //  Transmit = (S / B) * (1 / (1 - p))
    // We use S = 1024 bytes.
    // So we want to calculate
    //  Transmit = (8 * 1024 * SECOND * 4096) / (B * (4096 - wcett->LossProb))
    // We divide both numerator & denominator by 1024 * 1024.
    //
    Bandwidth = WcettDecodeBandwidth(wcett->Bandwidth);
    if (Bandwidth >= 1024 * 1024)
        Temp = ((Bandwidth / 1024) * (4096 - wcett->LossProb)) / 1024;
    else
        Temp = (Bandwidth * (4096 - wcett->LossProb)) / (1024 * 1024);
    if (Temp == 0)
        return (uint)-1;
    else
        Transmit = (4 * 8 * SECOND) / Temp;

    if (Backoff + Transmit < Transmit)
        return (uint)-1;

    return Backoff + Transmit;
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
    uint Bexp = Bandwidth & 3;
    uint Bmant = Bandwidth >> 2;

    switch (Bexp) {
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

    sprintf(Formatted, "%u%s", Bmant, Units);
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
            uint ETT = WcettConvETT(Metric);

            if (ETT == (uint)-1)
                sprintf(Formatted, "%.2f-%s-%u (inf)",
                        wcett->LossProb / (double)4096,
                        FormatBandwidth(wcett->Bandwidth),
                        wcett->Channel);
            else
                sprintf(Formatted, "%.2f-%s-%u (%.2fms)",
                        wcett->LossProb / (double)4096,
                        FormatBandwidth(wcett->Bandwidth),
                        wcett->Channel, ETT / (double)10000);
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

//* FormatPathMetric
//  
//  Returns a formatted string containing the metric value.
//
char *
FormatPathMetric(
    uint Metric, 
    uint MetricType)
{
    static char Formatted[100];
    uint Val;
    if (Metric == (uint)-1)
        strcpy(Formatted, "inf");
    else if (Metric == 0)
        strcpy(Formatted, "0");
    else {
        switch (MetricType) {
        case METRIC_TYPE_HOP:
            sprintf(Formatted, "%u", Metric);
            break;
        case METRIC_TYPE_ETX:
            sprintf(Formatted, "%.2f", Metric / (double)4096);
            break;
        case METRIC_TYPE_WCETT:
            //
            // Convert to ms for now.
            //
            sprintf(Formatted, "%.2fms", Metric / (double)10000.0);
            break;
        case METRIC_TYPE_RTT:
        case METRIC_TYPE_PKTPAIR:
            sprintf(Formatted, "%.2fms", Metric / (double)10000);
            break;
        default:
            fprintf(stderr, "Invalid metric type %u\n", MetricType);
            exit(1);
        }
    }
    return Formatted;
}

boolint
GetOurAddress(struct sockaddr_in6 *A)
{
    struct sockaddr_in6 sin6;
    SOCKET s;
    uint BytesReturned;
    int err;

    s = socket(AF_INET6, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        return FALSE;

    memset(&sin6, 0, sizeof sin6);
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr.s6_bytes[0] = 0x3f;
    sin6.sin6_addr.s6_bytes[1] = 0xfe;
    sin6.sin6_addr.s6_bytes[2] = 0x83;
    sin6.sin6_addr.s6_bytes[3] = 0x11;
    sin6.sin6_addr.s6_bytes[4] = 0xff;
    sin6.sin6_addr.s6_bytes[5] = 0xff;
    sin6.sin6_addr.s6_bytes[6] = 0xf7;
    sin6.sin6_addr.s6_bytes[7] = 0x1f;

    err = WSAIoctl(s, SIO_ROUTING_INTERFACE_QUERY,
                   &sin6, sizeof sin6,
                   A, sizeof *A,
                   &BytesReturned, NULL, NULL);
    closesocket(s);

    if ((err != 0) || (A->sin6_family != AF_INET6))
        return FALSE;

    return TRUE;
}

boolint
GetNumber(const char *astr, uint *number)
{
    uint num;

    num = 0;
    while (*astr != '\0') {
        if (('0' <= *astr) && (*astr <= '9'))
            num = 10 * num + (*astr - '0');
        else
            return FALSE;
        astr++;
    }

    *number = num;
    return TRUE;
}

boolint
GetGuid(const char *astr, GUID *Guid)
{
    WCHAR GuidStr[40+1];
    UNICODE_STRING UGuidStr;

    MultiByteToWideChar(CP_ACP, 0, astr, -1, GuidStr, 40);

    RtlInitUnicodeString(&UGuidStr, GuidStr);
    return RtlGUIDFromString(&UGuidStr, Guid) == STATUS_SUCCESS;
}

boolint
GetVirtualAdapter(const char *astr, MCL_QUERY_VIRTUAL_ADAPTER *Query)
{
    if (*astr == '{') {
        //
        // Read a guid.
        //
        Query->Index = 0;
        return GetGuid(astr, &Query->Guid);

    } else if (*astr == 'v') {
        //
        // Read a non-zero adapter index.
        //
        return GetNumber(astr + 1, &Query->Index) && (Query->Index != 0);
    }
    else
        return FALSE;
}

boolint
GetPhysicalAdapter(const char *astr, MCL_QUERY_PHYSICAL_ADAPTER *Query)
{
    if (*astr == '{') {
        //
        // Read a guid.
        //
        Query->Index = 0;
        return GetGuid(astr, &Query->Guid);

    } else {
        //
        // Read a non-zero adapter index.
        //
        return GetNumber(astr, &Query->Index) && (Query->Index != 0);
    }
}

boolint
GetVirtualAddress(const char *astr, VirtualAddress Address)
{
    uint Numbers[6];
    uint i;

    if (sscanf(astr, "%02x-%02x-%02x-%02x-%02x-%02x",
               &Numbers[0], &Numbers[1], &Numbers[2],
               &Numbers[3], &Numbers[4], &Numbers[5]) != 6)
        return FALSE;

    for (i = 0; i < 6; i++) {
        if (Numbers[i] > MAXUCHAR)
            return FALSE;
        Address[i] = (uchar) Numbers[i];
    }

    return TRUE;
}

boolint
GetHostName(const char *Host, VirtualAddress Address)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct in6_addr *addr;
    int err;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = PF_INET6;

    err = getaddrinfo(Host, NULL, &hints, &result);
    if (err != 0)
        return FALSE;

    addr = &((struct sockaddr_in6 *)result->ai_addr)->sin6_addr;
    freeaddrinfo(result);

    Address[0] = addr->s6_bytes[8] ^ 0x2;
    Address[1] = addr->s6_bytes[9];
    Address[2] = addr->s6_bytes[10];
    Address[3] = addr->s6_bytes[13];
    Address[4] = addr->s6_bytes[14];
    Address[5] = addr->s6_bytes[15];

    return TRUE;
}

char *
FormatHostName(VirtualAddress Address)
{
    static char Host[NI_MAXHOST];
    struct sockaddr_in6 sin6;

    // If ConvertHostName is false, return NULL.
    if (ConvertHostName == FALSE) 
        return NULL; 

    // Otherwise, try to get the real host name. 
#if 1
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr.s6_bytes[0] = 0x3f;
    sin6.sin6_addr.s6_bytes[1] = 0xfe;
    sin6.sin6_addr.s6_bytes[2] = 0x83;
    sin6.sin6_addr.s6_bytes[3] = 0x11;
    sin6.sin6_addr.s6_bytes[4] = 0xff;
    sin6.sin6_addr.s6_bytes[5] = 0xff;
    sin6.sin6_addr.s6_bytes[6] = 0xf7;
    sin6.sin6_addr.s6_bytes[7] = 0x1f;
#else
    //
    // Need a way to force this to return the best address on the MCL
    // interface for this to be useful.
    //
    if (! GetOurAddress(&sin6)) 
        return NULL;
#endif
    
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

char *
FormatVirtualAddress(VirtualAddress Address)
{
    static char buffer[128];

    sprintf(buffer, "%02x-%02x-%02x-%02x-%02x-%02x",
            Address[0], Address[1], Address[2],
            Address[3], Address[4], Address[5]);

    return buffer;
}

//
// At least for now, virtual and physical addresses
// have the same format. But we need the two functions
// to use different buffers.
//
char *
FormatPhysicalAddress(PhysicalAddress Address)
{
    static char buffer[128];

    sprintf(buffer, "%02x-%02x-%02x-%02x-%02x-%02x",
            Address[0], Address[1], Address[2],
            Address[3], Address[4], Address[5]);

    return buffer;
}

boolint
GetLinkAddressAndIndex(const char *astr, VirtualAddress Address, uint *Index)
{
    char *slash;

    slash = strchr(astr, '/');
    if (slash == NULL) {
        //
        // We just have an address, so the Index defaults to 1.
        //
        *Index = 1;
    }
    else {
        //
        // Read the index.
        //
        *slash = '\0';
        if (! GetNumber(astr, Index))
            return FALSE;
        astr = slash + 1;
    }

    return GetVirtualAddress(astr, Address);
}

char *
FormatGuid(const GUID *Guid)
{
    static char buffer[40];

    sprintf(buffer,
            "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
            Guid->Data1,
            Guid->Data2,
            Guid->Data3,
            Guid->Data4[0],
            Guid->Data4[1],
            Guid->Data4[2],
            Guid->Data4[3],
            Guid->Data4[4],
            Guid->Data4[5],
            Guid->Data4[6],
            Guid->Data4[7]);

    return buffer;
}

WCHAR *
MapAdapterGuidToFriendlyName(const GUID *Guid)
{
    static HMODULE hModule;
    static HRESULT (WINAPI *pHrLanConnectionNameFromGuidOrPath)
        (const GUID *pGuid, LPCWSTR pszwPath,
         LPWSTR pszwName, LPDWORD pcchMax);

    //
    // Get the conversion function, if we don't already have it.
    //
    if (hModule == NULL) {
        char Path[MAX_PATH + 64];

        //
        // Use an absolute path with LoadLibrary
        // to prevent Prefast warning.
        //
        if (GetSystemDirectory(Path, MAX_PATH + 1) == 0)
            return NULL;
        strcat(Path, "\\netman.dll");

        hModule = LoadLibrary(Path);
        if (hModule != NULL)
            pHrLanConnectionNameFromGuidOrPath =
                (HRESULT (WINAPI *)(const GUID *, LPCWSTR, LPWSTR, LPDWORD))
                GetProcAddress(hModule, "HrLanConnectionNameFromGuidOrPath");
    }

    if (pHrLanConnectionNameFromGuidOrPath != NULL) {
        static WCHAR FriendlyName[2048];
        DWORD FriendlyNameLength = 2048;

        if ((*pHrLanConnectionNameFromGuidOrPath)(Guid, NULL,
                                                  FriendlyName,
                                                  &FriendlyNameLength) == 0)
            return FriendlyName;
    }

    return NULL;
}

MCL_INFO_VIRTUAL_ADAPTER *
GetVirtualAdapterInfo(MCL_QUERY_VIRTUAL_ADAPTER *Query)
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
ForEachVirtualAdapter(void (*func)(MCL_INFO_VIRTUAL_ADAPTER *))
{
    MCL_QUERY_VIRTUAL_ADAPTER Query;
    MCL_INFO_VIRTUAL_ADAPTER VA;
    uint BytesReturned;

    Query.Index = (uint) -1;
    
    for (;;) {
        if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_VIRTUAL_ADAPTER,
                             &Query, sizeof Query,
                             &VA, sizeof VA, &BytesReturned,
                             NULL)) {
            fprintf(stderr, "ForEachVirtualAdapter:bad index %u\n",
                    Query.Index);
            exit(1);
        }

        if (Query.Index != (uint) -1) {

            if (BytesReturned != sizeof VA) {
                fprintf(stderr, "inconsistent info length\n");
                exit(1);
            }
            (*func)(&VA);
        }
        else {
            if (BytesReturned != sizeof VA.Next) {
                fprintf(stderr, "inconsistent info length\n");
                exit(1);
            }
        }

        if (VA.Next.Index == (uint) -1)
            break;
        Query = VA.Next;
    }
}

MCL_INFO_PHYSICAL_ADAPTER *
GetPhysicalAdapterInfo(MCL_QUERY_PHYSICAL_ADAPTER *Query)
{
    MCL_INFO_PHYSICAL_ADAPTER *PA;
    uint BytesReturned;

    PA = malloc(sizeof *PA);
    if (PA == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    if (!DeviceIoControl(Handle,
                         IOCTL_MCL_QUERY_PHYSICAL_ADAPTER,
                         Query, sizeof *Query,
                         PA, sizeof *PA, &BytesReturned,
                         NULL)) {
        fprintf(stderr, "bad index %u\n", Query->Index);
        exit(1);
    }

    if (BytesReturned != sizeof *PA) {
        fprintf(stderr, "inconsistent adapter info length\n");
        exit(1);
    }

    return PA;
}

void
ForEachPhysicalAdapter(MCL_INFO_VIRTUAL_ADAPTER *VA,
                       void (*func)(MCL_INFO_PHYSICAL_ADAPTER *))
{
    MCL_QUERY_PHYSICAL_ADAPTER Query;
    MCL_INFO_PHYSICAL_ADAPTER PA;
    uint BytesReturned;

    Query.VA = VA->This;
    Query.Index = (uint) -1;

    for (;;) {
        if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_PHYSICAL_ADAPTER,
                             &Query, sizeof Query,
                             &PA, sizeof PA, &BytesReturned,
                             NULL)) {
            fprintf(stderr, "bad index %u\n", Query.Index);
            exit(1);
        }

        if (Query.Index != (uint) -1) {

            if (BytesReturned != sizeof PA) {
                fprintf(stderr, "inconsistent info length\n");
                exit(1);
            }

            (*func)(&PA);
        }
        else {
            if (BytesReturned != sizeof PA.Next) {
                fprintf(stderr, "inconsistent info length\n");
                exit(1);
            }
        }

        if (PA.Next.Index == (uint) -1)
            break;
        Query = PA.Next;
    }
}

MCL_INFO_NEIGHBOR_CACHE *
GetNeighborCacheEntry(MCL_QUERY_NEIGHBOR_CACHE *Query)
{
    MCL_INFO_NEIGHBOR_CACHE *NCE;
    uint BytesReturned;

    NCE = (MCL_INFO_NEIGHBOR_CACHE *) malloc(sizeof *NCE);
    if (NCE == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_NEIGHBOR_CACHE,
                         Query, sizeof *Query,
                         NCE, sizeof *NCE, &BytesReturned,
                         NULL)) {
        fprintf(stderr, "bad address %s\n",
                FormatVirtualAddress(Query->Address));
        exit(1);
    }

    if (BytesReturned != sizeof *NCE) {
        fprintf(stderr, "inconsistent neighbor cache info length\n");
        exit(1);
    }

    NCE->Query = *Query;
    return NCE;
}

void
ForEachNeighborCacheEntry(MCL_QUERY_VIRTUAL_ADAPTER *VA,
                          void (*func)(MCL_INFO_NEIGHBOR_CACHE *))
{
    MCL_QUERY_NEIGHBOR_CACHE Query, NextQuery;
    MCL_INFO_NEIGHBOR_CACHE NCE;
    uint BytesReturned;

    NextQuery.VA = *VA;
    RtlZeroMemory(NextQuery.Address, sizeof(VirtualAddress));
    NextQuery.InIF = (uint) -1;

    for (;;) {
        Query = NextQuery;

        if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_NEIGHBOR_CACHE,
                             &Query, sizeof Query,
                             &NCE, sizeof NCE, &BytesReturned,
                             NULL)) {
            fprintf(stderr, "bad address %s\n",
                    FormatVirtualAddress(Query.Address));
            exit(1);
        }

        if (BytesReturned < sizeof NCE.Query) {
            fprintf(stderr, "inconsistent neighbor cache info length\n");
            exit(1);
        }

        NextQuery = NCE.Query;

        if (! IsUnspecified(Query.Address)) {

            if (BytesReturned < sizeof NCE) {
                fprintf(stderr, "inconsistent neighbor cache info length\n");
                exit(1);
            }

            NCE.Query = Query;
            (*func)(&NCE);
        }

        if (IsUnspecified(NextQuery.Address))
            break;
    }
}

void
PrintNeighborCacheEntry(MCL_INFO_NEIGHBOR_CACHE *NCE)
{
    char *Host;

    Host = FormatHostName(NCE->Query.Address);

    printf("  VA %u/%s -> PA %s\n",
           NCE->Query.InIF,
           (Host != NULL) ? Host : FormatVirtualAddress(NCE->Query.Address),
           FormatPhysicalAddress(NCE->Address));
}

const char *
FormatPowerMode(NDIS_802_11_POWER_MODE PowerMode)
{
    switch (PowerMode) {
    case Ndis802_11PowerModeCAM:
        return "CAM";
    case Ndis802_11PowerModeMAX_PSP:
        return "Max PSP";
    case Ndis802_11PowerModeFast_PSP:
        return "Fast PSP";
    case (NDIS_802_11_POWER_MODE) -1:
        return "not available";
    default:
        return "unknown";
    }
}

const char *
FormatInfrastructureMode(NDIS_802_11_NETWORK_INFRASTRUCTURE Mode)
{
    switch (Mode) {
    case Ndis802_11IBSS:
        return "IBSS";
    case Ndis802_11Infrastructure:
        return "AP";
    case Ndis802_11AutoUnknown:
        return "Auto";
    case (NDIS_802_11_NETWORK_INFRASTRUCTURE) -1:
        return "not available";
    default:
        return "unknown";
    }
}

boolint
GetHexDigit(char achar, uint *Digit)
{
    if (('0' <= achar) && (achar <= '9'))
        *Digit = achar - '0';
    else if (('a' <= achar) && (achar <= 'f'))
        *Digit = achar - 'a' + 10;
    else if (('A' <= achar) && (achar <= 'F'))
        *Digit = achar - 'A' + 10;
    else
        return FALSE;
    return TRUE;
}

boolint
GetBinary(char *astr, uchar *Buffer, uint Length)
{
    uint High, Low;
    uint i;

    for (i = 0; i < Length; i++) {
        if (! GetHexDigit(*astr++, &High))
            return FALSE;
        if (! GetHexDigit(*astr++, &Low))
            return FALSE;
        Buffer[i] = (High << 4) | Low;
    }

    if (*astr != '\0')
        return FALSE;

    return TRUE;
}

void
PrintBinary(uchar *Buffer, uint Length)
{
    uint i;

    for (i = 0; i < Length; i++)
        printf("%02x", Buffer[i]);
}

void
PrintPhysicalAdapter(MCL_INFO_PHYSICAL_ADAPTER *PA)
{
    WCHAR *FriendlyName;

    FriendlyName = MapAdapterGuidToFriendlyName(&PA->This.Guid);

    printf("  Physical Adapter %u:", PA->This.Index);
    switch (PA->Type) {
    case MCL_PHYSICAL_ADAPTER_ETHERNET:
        printf(" ethernet:");
        break;
    case MCL_PHYSICAL_ADAPTER_802_11:
        printf(" wireless:");
        break;
    }
    if (FriendlyName != NULL)
        printf(" %ws", FriendlyName);
    printf("\n");
    printf("    guid %s\n", FormatGuid(&PA->This.Guid));
    printf("    address %s\n", FormatPhysicalAddress(PA->Address));
    printf("    MTU %u\n", PA->MTU);
    if (PA->Promiscuous)
        printf("    promiscuous\n");
    if (PA->ReceiveOnly)
        printf("    receive-only\n");
    printf("    virtual channel %u\n", PA->Channel);
    printf("    bandwidth %s\n", FormatBandwidth(PA->Bandwidth));

    if (PA->Type == MCL_PHYSICAL_ADAPTER_802_11) {
        printf("    %s SSID %.*s BSSID %s Frequency %5.3fGHz\n",
               FormatInfrastructureMode(PA->W.Mode),
               PA->W.SSID.SsidLength,
               PA->W.SSID.Ssid,
               FormatPhysicalAddress(PA->W.BSSID),
               PA->W.Radio.DSConfig / (double)1000000);
        if (Verbose)
            printf("    PowerMode %s TxPowerLevel %u RTSThreshold %d\n",
                   FormatPowerMode(PA->W.PowerMode),
                   PA->W.TxPowerLevel,
                   PA->W.RTSThreshold);
    }

    if (Verbose) {
        printf("    packet pool failures %u\n", PA->CountPacketPoolFailure);
        printf("    Packets Sent %u\n", PA->PacketsSent);
        printf("    Packets Received %u\n", PA->PacketsReceived);
        printf("    Packets Received w/TD %u\n", PA->PacketsReceivedTD);
        printf("    Packets Received Flat %u\n", PA->PacketsReceivedFlat);
        printf("    Send Outstanding %u hw %u\n",
               PA->CountSentOutstanding, PA->MaxSentOutstanding);
        printf("    Receive Outstanding %u hw %u\n",
               PA->CountRecvOutstanding, PA->MaxRecvOutstanding);
    }
}

void
PrintVirtualAdapter(MCL_INFO_VIRTUAL_ADAPTER *VA)
{
    WCHAR *FriendlyName;

    FriendlyName = MapAdapterGuidToFriendlyName(&VA->This.Guid);

    printf("Virtual Adapter %u (Version %u):",
           VA->This.Index, VA->Version);
    if (FriendlyName != NULL)
        printf(" %ws", FriendlyName);
    printf("\n");
    printf("  guid %s\n", FormatGuid(&VA->This.Guid));
    printf("  address %s\n", FormatVirtualAddress(VA->Address));

    if (Verbose)
        printf("  lookahead %u\n", VA->LookAhead);

    if (! VA->Snooping)
        printf("  not snooping\n");
    if (VA->ArtificialDrop)
        printf("  artificial drop enabled\n");

    if (! VA->Crypto)
        printf("  crypto disabled\n");
    if (LQSR_KEY_PRESENT(VA->CryptoKeyMAC)) {
        printf("  CryptoKeyMAC ");
        PrintBinary(VA->CryptoKeyMAC, sizeof VA->CryptoKeyMAC);
        printf("\n");
    }
    else
        printf("  no CryptoKeyMAC\n");
    if (LQSR_KEY_PRESENT(VA->CryptoKeyAES)) {
        printf("  CryptoKeyAES ");
        PrintBinary(VA->CryptoKeyAES, sizeof VA->CryptoKeyAES);
        printf("\n");
    }
    else
        printf("  no CryptoKeyAES\n");

    if (VA->LinkTimeout == 0)
        printf("  LinkTimeout none\n");
    else
        printf("  LinkTimeout %us\n", VA->LinkTimeout);

    if (Verbose) {
        printf("  send buffer %u of %u (hw %u)\n",
               VA->SendBufSize, VA->SendBufMaxSize, VA->SendBufHighWater);

        printf("  packet pool failures %u\n",
               VA->CountPacketPoolFailure);

        if (VA->ReqTableMinElementReuse == MAXTIME)
            printf("  req table no element reuse\n");
        else
            printf("  req table element reuse %us\n",
                   TimeToSeconds(VA->ReqTableMinElementReuse));

        if (VA->ReqTableMinSuppressReuse == MAXTIME)
            printf("  req table no suppress reuse\n");
        else
            printf("  req table suppress reuse %us\n",
                   TimeToSeconds(VA->ReqTableMinSuppressReuse));

        printf("  avg forwarding stall %.1fus\n",
               (VA->CountXmitForwardBroadcast == 0) ?
               0.0 :
               VA->TotalForwardingStall /
               (double)VA->CountXmitForwardBroadcast);

        printf("  broadcast queue current %u max %u\n",
               VA->ForwardNum, VA->ForwardMax);
        printf("  broadcast queue %u drop %u fast %u\n",
               VA->CountForwardQueue, VA->CountForwardDrop,
               VA->CountForwardFast);

        printf("  timeout loops min %u max %u\n",
               VA->TimeoutMinLoops, VA->TimeoutMaxLoops);

        printf("  ack max dup time %ums\n",
               TimeToMillis(VA->PbackAckMaxDupTime));

        printf("  pback num %u hw %u\n", VA->PbackNumber, VA->PbackHighWater);

        printf("  pback alone %u of %u (%u too big)\n",
               VA->CountAloneTotal, VA->CountPbackTotal, VA->CountPbackTooBig);
        printf("  pback ack alone %u of %u\n",
               VA->CountAloneAck, VA->CountPbackAck);
        printf("  pback reply alone %u of %u\n",
               VA->CountAloneReply, VA->CountPbackReply);
        printf("  pback error alone %u of %u\n",
               VA->CountAloneError, VA->CountPbackError);
        printf("  pback info alone %u of %u\n",
               VA->CountAloneInfo, VA->CountPbackInfo);
    }

    if (VA->RouteFlapDampingFactor == 0)
        printf("  route-flap damping disabled\n");
    else 
        printf("  route-flap damping factor: %u \n",
               VA->RouteFlapDampingFactor);

    printf("  route-flap damp %u of %u\n",
           VA->CountRouteFlapDamp,
           VA->CountRouteFlap + VA->CountRouteFlapDamp);

    if (Verbose) {
        printf("  link cache smallest metric %s\n",
               FormatPathMetric(VA->SmallestMetric, VA->MetricType));

        printf("  link cache largest metric %s\n",
               FormatPathMetric(VA->LargestMetric, VA->MetricType));

        printf("  link cache add %u of %u\n",
               VA->CountAddLinkInvalidate,
               VA->CountAddLinkInvalidate + VA->CountAddLinkInsignificant);

        printf("  maintbuf num %u hw %u\n",
               VA->MaintBufNumPackets, VA->MaintBufHighWater);
    }

    printf("  transmit %u of %u\n",
           VA->CountXmitLocally, VA->CountXmit);

    if (Verbose) {
        printf("  CountXmitQueueFull %u\n", VA->CountXmitQueueFull);
        printf("  CountRexmitQueueFull %u\n", VA->CountRexmitQueueFull);
        printf("  CountXmitNoRoute %u\n", VA->CountXmitNoRoute);
        printf("  CountXmitMulticast %u\n", VA->CountXmitMulticast);
        printf("  CountXmitRouteRequest %u\n", VA->CountXmitRouteRequest);
        printf("  CountXmitRouteReply %u\n", VA->CountXmitRouteReply);
        printf("  CountXmitRouteError %u\n", VA->CountXmitRouteError);
        printf("  CountXmitRouteErrorNoLink %u\n",
               VA->CountXmitRouteErrorNoLink);
        printf("  CountXmitSendBuf %u\n", VA->CountXmitSendBuf);
        printf("  CountXmitMaintBuf %u\n", VA->CountXmitMaintBuf);
        printf("  CountXmitForwardUnicast %u\n", VA->CountXmitForwardUnicast);
        printf("  CountXmitForwardQueueFull %u\n",
               VA->CountXmitForwardQueueFull);
        printf("  CountXmitForwardBroadcast %u\n",
               VA->CountXmitForwardBroadcast);
        printf("  CountXmitInfoRequest %u\n", VA->CountXmitInfoRequest);
        printf("  CountXmitInfoReply %u\n", VA->CountXmitInfoReply);
        printf("  CountXmitProbe %u\n", VA->CountXmitProbe);
        printf("  CountXmitProbeReply %u\n", VA->CountXmitProbeReply);
        printf("  CountXmitLinkInfo %u\n", VA->CountXmitLinkInfo);
        printf("  CountLinkInfoTooManyLinks %u\n",
               VA->CountLinkInfoTooManyLinks);
        printf("  CountLinkInfoTooLarge %u\n", VA->CountLinkInfoTooLarge);
        printf("  CountSalvageAttempt %u\n", VA->CountSalvageAttempt);
        printf("  CountSalvageStatic %u\n", VA->CountSalvageStatic);
        printf("  CountSalvageOverflow %u\n", VA->CountSalvageOverflow);
        printf("  CountSalvageNoRoute %u\n", VA->CountSalvageNoRoute);
        printf("  CountSalvageSameNextHop %u\n", VA->CountSalvageSameNextHop);
        printf("  CountSalvageTransmit %u\n", VA->CountSalvageTransmit);
        printf("  CountSalvageQueueFull %u\n", VA->CountSalvageQueueFull);
    }

    printf("  receive %u of %u\n", VA->CountRecvLocally, VA->CountRecv);

    if (Verbose) {
        printf("  CountRecvLocallySalvaged %u\n",
               VA->CountRecvLocallySalvaged);
        printf("  CountRecvBadMAC %u\n", VA->CountRecvBadMAC);
        printf("  CountRecvRouteRequest %u\n", VA->CountRecvRouteRequest);
        printf("  CountRecvRouteReply %u\n", VA->CountRecvRouteReply);
        printf("  CountRecvRouteError %u\n", VA->CountRecvRouteError);
        printf("  CountRecvAckRequest %u\n", VA->CountRecvAckRequest);
        printf("  CountRecvAck %u\n", VA->CountRecvAck);
        printf("  CountRecvSourceRoute %u\n", VA->CountRecvSourceRoute);
        printf("  CountRecvInfoRequest %u\n", VA->CountRecvInfoRequest);
        printf("  CountRecvInfoReply %u\n", VA->CountRecvInfoReply);
        printf("  CountRecvProbe %u\n", VA->CountRecvProbe);
        printf("  CountRecvProbeReply %u\n", VA->CountRecvProbeReply);
        printf("  CountRecvLinkInfo %u\n", VA->CountRecvLinkInfo);
        printf("  CountRecvRecursive %u\n", VA->CountRecvRecursive);
        printf("  CountRecvEmpty %u\n", VA->CountRecvEmpty);
        printf("  CountRecvSmall %u\n", VA->CountRecvSmall);
        printf("  CountRecvDecryptFailure %u\n", VA->CountRecvDecryptFailure);
    }

    switch (VA->MetricType) {
    case METRIC_TYPE_HOP:
        printf("  HOP metric\n");
        break;
    case METRIC_TYPE_RTT:
        printf("  RTT settings:\n");
        printf("    Alpha = %.3f\n", 
                (double) VA->MetricParams.Rtt.Alpha / (double) MAXALPHA);
        printf("    ProbePeriod = %ums\n",
               TimeToMillis((Time)VA->MetricParams.Rtt.ProbePeriod));
        printf("    HysteresisPeriod = %ums\n",
               TimeToMillis((Time)VA->MetricParams.Rtt.HysteresisPeriod));
        printf("    PenaltyFactor = %u\n", 
                VA->MetricParams.Rtt.PenaltyFactor);
        printf("    SweepPeriod = %ums\n",
               TimeToMillis((Time)VA->MetricParams.Rtt.SweepPeriod));
        printf("    Random = %s\n", 
                VA->MetricParams.Rtt.Random? "True" : "False");
        printf("    Override = %s\n", 
                VA->MetricParams.Rtt.OutIfOverride? "True" : "False");
        break;
    case METRIC_TYPE_PKTPAIR:
        printf("  PktPair settings:\n");
        printf("    Alpha = %.3f\n", 
                (double) VA->MetricParams.PktPair.Alpha / (double) MAXALPHA);
        printf("    ProbePeriod = %ums\n",
               TimeToMillis((Time)VA->MetricParams.PktPair.ProbePeriod));
        printf("    PenaltyFactor = %u\n", 
                VA->MetricParams.PktPair.PenaltyFactor);
        break;
    case METRIC_TYPE_ETX:
        printf("  ETX settings:\n");
        printf("    ProbePeriod = %ums\n",
               TimeToMillis((Time)VA->MetricParams.Etx.ProbePeriod));
        printf("    LossInterval = %us\n",
               TimeToSeconds((Time)VA->MetricParams.Etx.LossInterval));
        printf("    Alpha = %.3f\n", 
                (double)VA->MetricParams.Etx.Alpha / (double)MAXALPHA);
        printf("    PenaltyFactor = %u\n", 
                VA->MetricParams.Etx.PenaltyFactor);
        break;
    case METRIC_TYPE_WCETT:
        printf("  WCETT settings:\n");
        printf("    ProbePeriod = %ums\n",
               TimeToMillis((Time)VA->MetricParams.Wcett.ProbePeriod));
        printf("    LossInterval = %us\n",
               TimeToSeconds((Time)VA->MetricParams.Wcett.LossInterval));
        printf("    Alpha = %.3f\n", 
                (double)VA->MetricParams.Wcett.Alpha / (double)MAXALPHA);
        printf("    PenaltyFactor = %u\n", 
                VA->MetricParams.Wcett.PenaltyFactor);
        printf("    Beta = %.3f\n",
               (double)VA->MetricParams.Wcett.Beta / (double)MAXALPHA);
        printf("    PktPair ProbePeriod = %ums\n",
               TimeToMillis((Time)VA->MetricParams.Wcett.PktPairProbePeriod));
        printf("    PktPair Minimum of %u Probes\n",
               VA->MetricParams.Wcett.PktPairMinOverProbes);
        break;
    default:
        printf("  unknown metric type %u\n", VA->MetricType);
    }

    ForEachPhysicalAdapter(VA, PrintPhysicalAdapter);
}

//
// This can have three values:
// NULL - We have not found the default VA.
// &DefaultVA - There is no default VA.
// Otherwise it contains the default VA.
//
MCL_INFO_VIRTUAL_ADAPTER *DefaultVA;

void
GetDefaultVirtualAdapterHelper(MCL_INFO_VIRTUAL_ADAPTER *VA)
{
    if (DefaultVA == NULL) {
        //
        // Save this VA as the default.
        //
        DefaultVA = (MCL_INFO_VIRTUAL_ADAPTER *) malloc(sizeof *VA);
        if (DefaultVA == NULL) {
            fprintf(stderr, "malloc failed\n");
            exit(1);
        }
        *DefaultVA = *VA;
    }
    else if (DefaultVA == (MCL_INFO_VIRTUAL_ADAPTER *)&DefaultVA) {
        //
        // We have already determined there is no default.
        //
    }
    else {
        //
        // With more than one virtual adapter, there is no default.
        //
        free(DefaultVA);
        DefaultVA = (MCL_INFO_VIRTUAL_ADAPTER *)&DefaultVA;
    }
}

MCL_INFO_VIRTUAL_ADAPTER *
GetDefaultVirtualAdapter(void)
{
    //
    // If we haven't already looked for a default, check now.
    //
    if (DefaultVA == NULL) {
        ForEachVirtualAdapter(GetDefaultVirtualAdapterHelper);
        //
        // If there are no virtual adapters, there is no default.
        //
        if (DefaultVA == NULL)
            DefaultVA = (MCL_INFO_VIRTUAL_ADAPTER *)&DefaultVA;
    }

    if (DefaultVA == (MCL_INFO_VIRTUAL_ADAPTER *)&DefaultVA) {
        fprintf(stderr, "no default virtual adapter\n");
        exit(1);
    }

    return DefaultVA;
}

void
usage(void)
{
    fprintf(stderr, "usage: mcl install [dir]\n");
    fprintf(stderr, "       mcl uninstall\n");
    fprintf(stderr, "       mcl enable\n");
    fprintf(stderr, "       mcl disable\n");
    fprintf(stderr, "       mcl restart\n");
    fprintf(stderr, "       mcl [-v] va [vaindex]\n");
    fprintf(stderr, "       mcl vac [vaindex] [context] parameter value\n");
    fprintf(stderr, "       mcl pac [vaindex] paindex [ro] [-ro]\n");
    fprintf(stderr, "       mcl nc [vaindex] [address]\n");
    fprintf(stderr, "       mcl ncf [vaindex] [address]\n");
    fprintf(stderr, "       mcl [-v] lc [vaindex] [address]\n");
    fprintf(stderr, "       mcl lca [vaindex] [findex/]faddress [tindex/]taddress\n");
    fprintf(stderr, "       mcl rca <filename>\n");
    fprintf(stderr, "       mcl lcf [vaindex]\n");
    fprintf(stderr, "       mcl [-v] lcc [vaindex]\n");
    fprintf(stderr, "       mcl [-v] rcc [vaindex] [dest]\n");
    fprintf(stderr, "       mcl sr [vaindex] dest\n");
    fprintf(stderr, "       mcl mb [vaindex]\n");
    fprintf(stderr, "       mcl ad [vaindex] [findex/]faddress [tindex/]taddress percentage\n");
    fprintf(stderr, "       mcl inforeq [vaindex]\n");
    fprintf(stderr, "       mcl reset [vaindex]\n");
    fprintf(stderr, "       mcl rand [numbytes]\n");
    exit(1);
}

void
ausage(void)
{
    fprintf(stderr, "You do not have local Administrator privileges.\n");
    exit(1);
}

void
QueryVirtualAdapter(int argc, char *argv[])
{
    if (argc == 0) {
        ForEachVirtualAdapter(PrintVirtualAdapter);
    }
    else if (argc == 1) {
        MCL_QUERY_VIRTUAL_ADAPTER Query;
        MCL_INFO_VIRTUAL_ADAPTER *VA;

        if (! GetVirtualAdapter(argv[0], &Query))
            usage();

        VA = GetVirtualAdapterInfo(&Query);
        PrintVirtualAdapter(VA);
        free(VA);
    }
    else {
        usage();
    }
}

boolint
GetChannel(const char *astr, uint *Channel)
{
    return GetNumber(astr, Channel) && (*Channel < 256);
}

boolint
GetBandwidth(const char *astr, uint *Bandwidth)
{
    uint mantissa;

    mantissa = 0;
    while (*astr != '\0') {
        if (('0' <= *astr) && (*astr <= '9'))
            mantissa = 10 * mantissa + (*astr - '0');
        else if (!strcmp(astr, "Kbps")) {
            if (mantissa == 0) {
                *Bandwidth = 0;
                return TRUE;
            }
            if (mantissa >= 1024)
                return FALSE;
            *Bandwidth = (mantissa << 2);
            return TRUE;
        }
        else if (!strcmp(astr, "Mbps")) {
            if (mantissa == 0) {
                *Bandwidth = 0;
                return TRUE;
            }
            if (mantissa >= 1024)
                return FALSE;
            *Bandwidth = (mantissa << 2) | 1;
            return TRUE;
        }
        else if (!strcmp(astr, "Gbps")) {
            if (mantissa == 0) {
                *Bandwidth = 0;
                return TRUE;
            }
            if (mantissa >= 1024)
                return FALSE;
            *Bandwidth = (mantissa << 2) | 2;
            return TRUE;
        }
        else if (!strcmp(astr, "Tbps")) {
            if (mantissa == 0) {
                *Bandwidth = 0;
                return TRUE;
            }
            if (mantissa >= 1024)
                return FALSE;
            *Bandwidth = (mantissa << 2) | 3;
            return TRUE;
        }
        else
            return FALSE;
        astr++;
    }

    return FALSE;
}

void
ControlPhysicalAdapter(int argc, char *argv[])
{
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    MCL_QUERY_PHYSICAL_ADAPTER Query;
    MCL_INFO_PHYSICAL_ADAPTER *PA;
    MCL_CONTROL_PHYSICAL_ADAPTER Control;
    int i;
    uint BytesReturned;

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //
    if ((argc > 0) && GetVirtualAdapter(argv[0], &Query.VA)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
        VA = GetVirtualAdapterInfo(&Query.VA);
    }
    else {
        VA = GetDefaultVirtualAdapter();
    }

    Query.VA = VA->This;

    if ((argc < 1) || ! GetPhysicalAdapter(argv[0], &Query)) {
        usage();
    }

    PA = GetPhysicalAdapterInfo(&Query);
    Control.This = PA->This;
    MCL_INIT_CONTROL_PHYSICAL_ADAPTER(&Control);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "ro")) {
            Control.ReceiveOnly = TRUE;
        }
        else if (!strcmp(argv[i], "-ro")) {
            Control.ReceiveOnly = FALSE;
        }
        else if (!strcmp(argv[i], "channel") && (i+1 < argc)) {
            if (! GetChannel(argv[++i], &Control.Channel)) {
                usage();
            }
        }
        else if (!strcmp(argv[i], "bw") && (i+1 < argc)) {
            if (! GetBandwidth(argv[++i], &Control.Bandwidth)) {
                usage();
            }
        }
        else {
            usage();
        }
    }

    if (!DeviceIoControl(Handle, IOCTL_MCL_CONTROL_PHYSICAL_ADAPTER,
                         &Control, sizeof Control,
                         NULL, 0, &BytesReturned, NULL)) {
        fprintf(stderr, "control error: %x\n", GetLastError());
        exit(1);
    }
}

void
PrintNeighborCache(MCL_INFO_VIRTUAL_ADAPTER *VA)
{
    printf("Virtual Adapter %u Neighbor Cache:\n",
           VA->This.Index);
    ForEachNeighborCacheEntry(&VA->This, PrintNeighborCacheEntry);
}

void
QueryNeighborCache(int argc, char *argv[])
{
    if (argc == 0) {
        ForEachVirtualAdapter(PrintNeighborCache);
    }
    else {
        MCL_QUERY_VIRTUAL_ADAPTER Query;
        MCL_INFO_VIRTUAL_ADAPTER *VA;

        //
        // The first argument is optional. If present,
        // it specifies the virtual adapter.
        // It must be present if there is more than one virtual adapter.
        //
        if (GetVirtualAdapter(argv[0], &Query)) {
            //
            // The first argument specifies a virtual adapter.
            //
            argc -= 1;
            argv += 1;
            VA = GetVirtualAdapterInfo(&Query);
        }
        else {
            VA = GetDefaultVirtualAdapter();
        }

        if (argc == 1) {
            MCL_QUERY_NEIGHBOR_CACHE Node;
            MCL_INFO_NEIGHBOR_CACHE *NCE;

            Node.VA = VA->This;

            if (! GetLinkAddressAndIndex(argv[0], Node.Address, &Node.InIF))
                usage();

            NCE = GetNeighborCacheEntry(&Node);
            PrintNeighborCacheEntry(NCE);
        }
        else if (argc == 0) {
            PrintNeighborCache(VA);
        }
        else
            usage();
    }
}

void
FlushNeighborCache(int argc, char *argv[])
{
    MCL_QUERY_NEIGHBOR_CACHE Request;
    uint BytesReturned;

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //

    if ((argc > 0) && GetVirtualAdapter(argv[0], &Request.VA)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
    }
    else {
        MCL_INFO_VIRTUAL_ADAPTER *VA;

        VA = GetDefaultVirtualAdapter();
        Request.VA = VA->This;
    }

    if (argc == 1) {
        //
        // The user specified a virtual address to flush.
        //
        if (! GetLinkAddressAndIndex(argv[0], Request.Address, &Request.InIF))
            usage();
    }
    else if (argc == 0) {
        //
        // The unspecified address means flush everything.
        //
        RtlZeroMemory(Request.Address, sizeof(VirtualAddress));
        Request.InIF = (uint) -1;
    }
    else
        usage();

    if (!DeviceIoControl(Handle, IOCTL_MCL_FLUSH_NEIGHBOR_CACHE,
                         &Request, sizeof Request,
                         NULL, 0, &BytesReturned, NULL)) {
        fprintf(stderr, "flush neighbor cache error: %x\n",
                GetLastError());
        exit(1);
    }
}

__inline uint
SizeofNode(uint NumLinks)
{
    return sizeof(MCL_INFO_CACHE_NODE) + NumLinks * sizeof(MCL_INFO_LINK);
}

MCL_INFO_CACHE_NODE *
GetCacheNode(MCL_QUERY_CACHE_NODE *Query)
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
            fprintf(stderr, "bad address %s\n",
                    FormatVirtualAddress(Query->Address));
            exit(1);
        }

        if ((BytesReturned < sizeof *Node) ||
            (BytesReturned > SizeofNode(Node->LinkCount))) {
            fprintf(stderr, "inconsistent link cache info length\n");
            exit(1);
        }

        if (BytesReturned == SizeofNode(Node->LinkCount))
            break;

        if (++NumAttempts == 4) {
            fprintf(stderr, "inconsistent link cache info length\n");
            exit(1);
        }

        BufferSize = SizeofNode(Node->LinkCount);
        free(Node);
        Node = (MCL_INFO_CACHE_NODE *) malloc(BufferSize);
        if (Node == NULL) {
            fprintf(stderr, "malloc failed\n");
            exit(1);
        }
    }

    Node->Query = *Query;
    return Node;
}

void
ForEachCacheNode(MCL_INFO_VIRTUAL_ADAPTER *VAI,
                 void (*func)(MCL_INFO_CACHE_NODE *, MCL_INFO_VIRTUAL_ADAPTER *))
{
    MCL_QUERY_VIRTUAL_ADAPTER *VA = &VAI->This;
    MCL_QUERY_CACHE_NODE Query, NextQuery;
    MCL_INFO_CACHE_NODE *Node;
    uint BufferSize;
    uint BytesReturned;

    NextQuery.VA = *VA;
    RtlZeroMemory(NextQuery.Address, sizeof(VirtualAddress));

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
        uint NumAttempts = 0;
        Query = NextQuery;

        for (;;) {
            if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_CACHE_NODE,
                                 &Query, sizeof Query,
                                 Node, BufferSize, &BytesReturned,
                                 NULL)) {
                fprintf(stderr, "bad address %s\n",
                        FormatVirtualAddress(Query.Address));
                exit(1);
            }

            if (IsUnspecified(Query.Address) &&
                (BytesReturned == sizeof Node->Query))
                break;

            if ((BytesReturned < sizeof *Node) ||
                (BytesReturned > SizeofNode(Node->LinkCount))) {
                fprintf(stderr, "inconsistent link cache info length\n");
                exit(1);
            }

            if (BytesReturned == SizeofNode(Node->LinkCount))
                break;

            if (++NumAttempts == 4) {
                fprintf(stderr, "inconsistent link cache info length\n");
                exit(1);
            }

            BufferSize = SizeofNode(Node->LinkCount);
            free(Node);
            Node = (MCL_INFO_CACHE_NODE *) malloc(BufferSize);
            if (Node == NULL) {
                fprintf(stderr, "malloc failed\n");
                exit(1);
            }
        }

        NextQuery = Node->Query;

        if (! IsUnspecified(Query.Address)) {

            Node->Query = Query;
            (*func)(Node, VAI);
        }

        if (IsUnspecified(NextQuery.Address))
            break;
    }

    free(Node);
}

void
PrintRouteUsage(MCL_INFO_ROUTE_USAGE *RU)
{
    char *Host;
    uint Hop;

    printf("        %u:", RU->Usage);
    if (RU->NumHops == 0)
        printf(" no route");
    else for (Hop = 0; Hop < RU->NumHops; Hop++) {
        Host = FormatHostName(RU->Hops[Hop].Address);
        printf(" %s/%u/%u", (Host != NULL) ?
               Host : FormatVirtualAddress(RU->Hops[Hop].Address),
               RU->Hops[Hop].InIF, RU->Hops[Hop].OutIF);
    }
    printf("\n");
}

void
GetRouteUsage(MCL_QUERY_CACHE_NODE *Node,
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
            fprintf(stderr, "bad address %s\n",
                    FormatVirtualAddress(Node->Address));
            exit(1);
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
PrintCacheNode(MCL_INFO_CACHE_NODE *Node, MCL_INFO_VIRTUAL_ADAPTER *VA)
{
    Time Now;
    uint link;
    char *Host;

    GetSystemTimeAsFileTime((FILETIME *)&Now);

    Host = FormatHostName(Node->Query.Address);

    if (Host != NULL)
        printf("%s (%s): ",
               Host,
               FormatVirtualAddress(Node->Query.Address));
    else
        printf("%s: ", FormatVirtualAddress(Node->Query.Address));

    printf("Hops %d DMetric %s ",
           Node->Hops, 
           FormatPathMetric(Node->DijkstraMetric, VA->MetricType));
    printf("CMetric %s Prev %d\n",
           FormatPathMetric(Node->CachedMetric, VA->MetricType),
           Node->Previous);

    if (Verbose && !VirtualAddressEqual(Node->Query.Address, VA->Address)) {
        MCL_INFO_ROUTE_USAGE *History;
        uint NumRecords;
        uint Record;
        uint TotalPackets = 0;
        uint TotalHops = 0;
        uint NumRoutes = 0;

        GetRouteUsage(&Node->Query, &NumRecords, &History);

        printf("    Route Usage:\n");

        for (Record = 0; Record < NumRecords; Record++) {
            if (History->NumHops != 0) {
                NumRoutes++;
                TotalPackets += History->Usage;
                TotalHops += History->Usage * History->NumHops;
            }

            PrintRouteUsage(History);

            (uchar *)History += sizeof *History +
                History->NumHops * sizeof History->Hops[0];
        }

        printf("      num routes %u records %u packets %u changes %u\n",
               NumRoutes, NumRecords, TotalPackets, Node->RouteChanges);
        printf("      average path length %.2f hops\n",
               ((TotalPackets == 0) ?
                0.0 : ((double)TotalHops / TotalPackets) - 1.0));

        free(History);
    }

    for (link = 0; link < Node->LinkCount; link++) {
        MCL_INFO_LINK *Link = &Node->Links[link];

        Host = FormatHostName(Link->To);
        printf("    From %u To %u/%s\n",
                Link->FromIF,
                Link->ToIF,
                (Host != NULL) ? Host : FormatVirtualAddress(Link->To));
        printf("        TimeStamp %ds Usage %u Metric %s\n",
               TimeToSeconds(Link->TimeStamp - Now), 
               Link->Usage,
               FormatLinkMetric(Link->Metric, VA->MetricType));

        if (VA->ArtificialDrop) {
            uint DropPercentage = Link->DropRatio / ((uint)-1 / 100);
            printf("        Artificial Drop Percentage %u Drops %u\n",
                   DropPercentage, Link->ArtificialDrops);
        }

        if (VirtualAddressEqual(Node->Query.Address, VA->Address)) {
            printf("        Queue Drops %u Failures %u fraction %.2g\n",
                   Link->QueueDrops,
                   Link->Failures,
                   ((Link->Usage == 0) ?
                    0.0 : Link->Failures / (double)Link->Usage));

            //
            // Print details that are specific to a given metric
            // type.  We have these details only for our own links. 
            // (i.e. the ones that originate at us). For all other links, 
            // this info is not known, since link info packets contain 
            // only the metric. 
            //
            switch (VA->MetricType) {
            case METRIC_TYPE_RTT:
                printf("        RawMetric %.2fms RTT %.2fms\n",
                    (double)Link->MetricInfo.Rtt.RawMetric/(double)10000,
                    (double)Link->MetricInfo.Rtt.LastRTT/(double)10000);
                printf("        SentProbes %u LostProbes %u LateProbes %u\n",
                    Link->MetricInfo.Rtt.SentProbes,
                    Link->MetricInfo.Rtt.LostProbes,
                    Link->MetricInfo.Rtt.LateProbes);
                break;

            case METRIC_TYPE_PKTPAIR:
                printf("        PairsSent %u RepliesRcvd %u LostPairs %u RTT %.2fms\n",
                    Link->MetricInfo.PktPair.PairsSent,
                    Link->MetricInfo.PktPair.RepliesRcvd,
                    Link->MetricInfo.PktPair.LostPairs,
                    (double)Link->MetricInfo.PktPair.RTT/(double)10000);
                printf("        LastPktPair %.2fms LastRTT %.2fms\n",
                    (double)Link->MetricInfo.PktPair.LastPktPair/(double)10000,
                    (double)Link->MetricInfo.PktPair.LastRTT/(double)10000);
                if (Verbose) {
                    int i; 
                    double Bandwidth; 
                    for (i = 0; i < MCL_INFO_MAX_PKTPAIR_HISTORY ; i++) {
                        Bandwidth = 0; 
                        if (Link->MetricInfo.PktPair.MinHistory[i].Seq != (uint)-1) {
                            if (Link->MetricInfo.PktPair.MinHistory[i].Min != (uint)-1) {
                                Bandwidth = (1088.0 * 8.0) / ((double)Link->MetricInfo.PktPair.MinHistory[i].Min/(double)10000);
                            }
                            else {
                                Bandwidth = 0;
                            }
                            //
                            // NB: The bandwidth is approximate. It does not
                            // take into account 802.11 headres and RTS/CTS delays. 
                            //
                            printf ("        %u : %.2f Kbps %u\n", 
                                    Link->MetricInfo.PktPair.MinHistory[i].Seq, 
                                    Bandwidth,
                                    Link->MetricInfo.PktPair.MinHistory[i].Min);
                        }
                    }
                }
                break;

            case METRIC_TYPE_ETX:
                printf("        TotSentProbes %u\n",
                       Link->MetricInfo.Etx.TotSentProbes);
                break;

            case METRIC_TYPE_WCETT:
                printf("        ETX:\n");
                printf("        TotSentProbes %u LastProb %.3f\n",
                       Link->MetricInfo.Wcett.TotSentProbes,
                       (double)Link->MetricInfo.Wcett.LastProb/4096.0);
                printf("        PktPair:\n");
                printf("        PairsSent %u RepliesRcvd %u\n",
                       Link->MetricInfo.Wcett.PairsSent,
                       Link->MetricInfo.Wcett.RepliesRcvd);
                printf("        LastPktPair %.2fms",
                    (double)Link->MetricInfo.Wcett.LastPktPair/(double)10000);
                if (Link->MetricInfo.Wcett.CurrMin == (uint)-1)
                    printf(" CurrMin inf\n");
                else
                    printf(" CurrMin %.2fms\n",
                        (double)Link->MetricInfo.Wcett.CurrMin/(double)10000);
                printf("        NumValid %u NumInvalid %u\n",
                       Link->MetricInfo.Wcett.NumPktPairValid,
                       Link->MetricInfo.Wcett.NumPktPairInvalid);
                break;
            }
        }

        if (VirtualAddressEqual(Link->To, VA->Address)) {
            switch (VA->MetricType) {
            case METRIC_TYPE_PKTPAIR:
                printf("        RepliesSent %u\n",
                       Link->MetricInfo.PktPair.RepliesSent);
                break;

            case METRIC_TYPE_ETX:
                printf("        TotRcvdProbes %u FwdDeliv %u RevDeliv %u\n",
                       Link->MetricInfo.Etx.TotRcvdProbes,
                       Link->MetricInfo.Etx.FwdDeliv,
                       Link->MetricInfo.Etx.ProbeHistorySZ);
                break;

            case METRIC_TYPE_WCETT:
                printf("        RepliesSent %u\n",
                       Link->MetricInfo.Wcett.RepliesSent);
                printf("        TotRcvdProbes %u FwdDeliv %u RevDeliv %u\n",
                       Link->MetricInfo.Wcett.TotRcvdProbes,
                       Link->MetricInfo.Wcett.FwdDeliv,
                       Link->MetricInfo.Wcett.ProbeHistorySZ);
                break;
            }
        }
    }
}

MCL_INFO_LINK_CACHE *
GetLinkCacheInfo(MCL_QUERY_VIRTUAL_ADAPTER *Query)
{
    MCL_INFO_LINK_CACHE *LC;
    uint BytesReturned;

    LC = malloc(sizeof *LC);
    if (LC == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    if (!DeviceIoControl(Handle,
                         IOCTL_MCL_QUERY_LINK_CACHE,
                         Query, sizeof *Query,
                         LC, sizeof *LC, &BytesReturned,
                         NULL)) {
        fprintf(stderr, "bad index %u\n", Query->Index);
        exit(1);
    }

    if (BytesReturned != sizeof *LC) {
        fprintf(stderr, "inconsistent link cache info length\n");
        exit(1);
    }

    return LC;
}

void
PrintLinkCache(MCL_INFO_VIRTUAL_ADAPTER *VA)
{
    MCL_INFO_LINK_CACHE *LC = GetLinkCacheInfo(&VA->This);
    Time Now;

    GetSystemTimeAsFileTime((FILETIME *)&Now);

    printf("VA %u Link Cache, %u nodes of %u, Timeout %ds\n",
           VA->This.Index,
           LC->NumNodes,
           LC->MaxNodes,
           TimeToSeconds(LC->Timeout - Now));
    ForEachCacheNode(VA, PrintCacheNode);
}

void
QueryLinkCache(int argc, char *argv[])
{
    if (argc == 0) {
        ForEachVirtualAdapter(PrintLinkCache);
    }
    else {
        MCL_QUERY_VIRTUAL_ADAPTER QueryVA;
        MCL_INFO_VIRTUAL_ADAPTER *VA;

        //
        // The first argument is optional. If present,
        // it specifies the virtual adapter.
        // It must be present if there is more than one virtual adapter.
        //
        if (GetVirtualAdapter(argv[0], &QueryVA)) {
            //
            // The first argument specifies a virtual adapter.
            //
            argc -= 1;
            argv += 1;
            VA = GetVirtualAdapterInfo(&QueryVA);
        }
        else {
            VA = GetDefaultVirtualAdapter();
        }

        if (argc == 1) {
            MCL_QUERY_CACHE_NODE QueryNode;
            MCL_INFO_CACHE_NODE *Node;

            QueryNode.VA = VA->This;

            if (! GetVirtualAddress(argv[0], QueryNode.Address))
                usage();

            Node = GetCacheNode(&QueryNode);
            PrintCacheNode(Node, VA);
        }
        else if (argc == 0) {
            PrintLinkCache(VA);
        }
        else
            usage();
    }
}

void
AddLink(int argc, char *argv[])
{
    MCL_ADD_LINK Request;
    uint BytesReturned;

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //

    if ((argc > 0) && GetVirtualAdapter(argv[0], &Request.Node.VA)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
    }
    else {
        MCL_INFO_VIRTUAL_ADAPTER *VA;

        VA = GetDefaultVirtualAdapter();
        Request.Node.VA = VA->This;
    }

    //
    // The next two arguments specify the from and to addresses.
    // In addition, the arguments may optionally specify indices.
    //

    if (argc != 2)
        usage();

    if (! GetLinkAddressAndIndex(argv[0],
                                 Request.Node.Address, &Request.FromIF))
        usage();

    if (! GetLinkAddressAndIndex(argv[1],
                                 Request.To, &Request.ToIF))
        usage();

    if (!DeviceIoControl(Handle, IOCTL_MCL_ADD_LINK,
                         &Request, sizeof Request,
                         NULL, 0, &BytesReturned, NULL)) {
        fprintf(stderr, "add link error: %x\n", GetLastError());
        exit(1);
    }
}

void
FlushLinkCache(int argc, char *argv[])
{
    MCL_QUERY_VIRTUAL_ADAPTER Request;
    uint BytesReturned;

    if (argc == 0) {
        MCL_INFO_VIRTUAL_ADAPTER *VA;

        VA = GetDefaultVirtualAdapter();
        Request = VA->This;
    }
    else if (argc == 1) {
        if (! GetVirtualAdapter(argv[0], &Request))
            usage();
    }
    else
        usage();

    if (!DeviceIoControl(Handle, IOCTL_MCL_FLUSH_LINK_CACHE,
                         &Request, sizeof Request,
                         NULL, 0, &BytesReturned, NULL)) {
        fprintf(stderr, "flush link cache error: %x\n",
                GetLastError());
        exit(1);
    }
}

static char *
FormatLinkStateChange(uint Change)
{
    switch (Change) {
    case LINK_STATE_CHANGE_DELETE_TIMEOUT:
        return "DELETE (Timeout)";
    case LINK_STATE_CHANGE_DELETE_MANUAL:
        return "DELETE (Manual)";
    case LINK_STATE_CHANGE_DELETE_INTERFACE:
        return "DELETE (Interface)";

    case LINK_STATE_CHANGE_ERROR:
        return "CHANGE (Error)";
    case LINK_STATE_CHANGE_SNOOP_ERROR:
        return "CHANGE (Snoop Error)";

    case LINK_STATE_CHANGE_PENALIZED:
        return "CHANGE (MaintBuf)";

    case LINK_STATE_CHANGE_ADD_MANUAL:
        return "ADD (Manual)";
    case LINK_STATE_CHANGE_ADD_REPLY:
        return "ADD (Reply)";
    case LINK_STATE_CHANGE_ADD_SNOOP_REPLY:
        return "ADD (Snoop Reply)";
    case LINK_STATE_CHANGE_ADD_SNOOP_SR:
        return "ADD (Snoop Source Route)";
    case LINK_STATE_CHANGE_ADD_SNOOP_REQUEST:
        return "ADD (Snoop Request)";
    case LINK_STATE_CHANGE_ADD_PROBE:
        return "ADD (Probe)";
    case LINK_STATE_CHANGE_ADD_LINKINFO:
        return "ADD (LinkInfo)";
    default:
        return "UNKNOWN";
    }
}

uint
LinkCacheChangeLogIndex(MCL_QUERY_VIRTUAL_ADAPTER *VA)
{
    MCL_QUERY_LINK_CACHE_CHANGE_LOG Query;
    MCL_INFO_LINK_CACHE_CHANGE_LOG Info;
    uint BytesReturned;

    Query.VA = *VA;
    Query.Index = (uint)-1;

    if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_LINK_CACHE_CHANGE_LOG,
                         &Query, sizeof Query,
                         &Info, sizeof Info,
                         &BytesReturned, NULL) ||
        (BytesReturned != sizeof Info)) {
        fprintf(stderr, "query link cache change log error: %x\n",
                GetLastError());
        exit(1);
    }

    return Info.NextIndex;
}

void
LinkCacheChangeLog(int argc, char *argv[])
{
    MCL_QUERY_LINK_CACHE_CHANGE_LOG Query;
    MCL_INFO_LINK_CACHE_CHANGE_LOG *Info;
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    uint InfoSize = sizeof *Info + 1024 * sizeof Info->Changes[0];
    uint BytesReturned;
    Time BaseTime = 0;

    Info = malloc(InfoSize);
    if (Info == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //
    if ((argc > 0) && GetVirtualAdapter(argv[0], &Query.VA)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
        VA = GetVirtualAdapterInfo(&Query.VA);
    }
    else {
        VA = GetDefaultVirtualAdapter();
        Query.VA = VA->This;
    }

    if (argc != 0)
        usage();

    for (Query.Index = LinkCacheChangeLogIndex(&Query.VA) + Verbose; ;
         Query.Index = Info->NextIndex) {
        uint NumRecords;
        uint i;

        if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_LINK_CACHE_CHANGE_LOG,
                             &Query, sizeof Query,
                             Info, InfoSize,
                             &BytesReturned, NULL) ||
            (BytesReturned < sizeof *Info)) {
            fprintf(stderr, "query link cache change log error: %x\n",
                    GetLastError());
            exit(1);
        }

        NumRecords = ((BytesReturned - sizeof *Info) /
                      sizeof Info->Changes[0]);

        for (i = 0; i < NumRecords; i++) {
            MCL_INFO_LINK_CACHE_CHANGE *Record = &Info->Changes[i];
            char *Host;

            if (Record->TimeStamp == 0)
                continue;

            if (BaseTime == 0)
                BaseTime = Record->TimeStamp;

            printf("%8.2f ",
                   (Record->TimeStamp - BaseTime) /
                   (double)(10*1000*1000));

            Host = FormatHostName(Record->From);
            printf("%s/%u -> ",
                   (Host != NULL) ?
                   Host : FormatVirtualAddress(Record->From),
                   Record->FromIF);

            Host = FormatHostName(Record->To);
            printf("%s/%u ",
                   (Host != NULL) ?
                   Host : FormatVirtualAddress(Record->To),
                   Record->ToIF);

            printf("%s (metric %s)\n",
                   FormatLinkStateChange(Record->Reason),
                   FormatLinkMetric(Record->Metric, VA->MetricType));
        }
        fflush(stdout);
        CloseHandle(Handle);
        Sleep(1000); // One second.

        //
        // We close & reopen the handle to give the driver
        // a chance to unload.
        //

        Handle = CreateFileW(WIN_MCL_DEVICE_NAME,
                             0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,   // security attributes
                             OPEN_EXISTING,
                             0,      // flags & attributes
                             NULL);  // template file
        if (Handle == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Could not access MCL.\n");
            exit(1);
        }
    }
}

uint
RouteCacheChangeLogIndex(MCL_QUERY_VIRTUAL_ADAPTER *VA)
{
    MCL_QUERY_ROUTE_CACHE_CHANGE_LOG Query;
    MCL_INFO_ROUTE_CACHE_CHANGE_LOG Info;
    uint BytesReturned;

    Query.VA = *VA;
    Query.Index = (uint)-1;

    if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_ROUTE_CACHE_CHANGE_LOG,
                         &Query, sizeof Query,
                         &Info, sizeof Info,
                         &BytesReturned, NULL) ||
        (BytesReturned != sizeof Info)) {
        fprintf(stderr, "query route cache change log error: %x\n",
                GetLastError());
        exit(1);
    }

    return Info.NextIndex;
}

void
RouteCacheChangeLog(int argc, char *argv[])
{
    MCL_QUERY_ROUTE_CACHE_CHANGE_LOG Query;
    MCL_INFO_ROUTE_CACHE_CHANGE_LOG *Info;
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    VirtualAddress Dest;
    uint InfoSize = sizeof *Info + 1024 * sizeof Info->Changes[0];
    uint BytesReturned;
    Time BaseTime = 0;

    Info = malloc(InfoSize);
    if (Info == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //
    if ((argc > 0) && GetVirtualAdapter(argv[0], &Query.VA)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
        VA = GetVirtualAdapterInfo(&Query.VA);
    }
    else {
        VA = GetDefaultVirtualAdapter();
        Query.VA = VA->This;
    }

    //
    // The second argument is an optional destination address,
    // which we use to filter our output.
    //
    if (argc == 0) {
        RtlZeroMemory(Dest, sizeof(VirtualAddress));
    }
    else if (argc == 1) {
        if (! GetVirtualAddress(argv[0], Dest) &&
            ! GetHostName(argv[0], Dest)) {
            fprintf(stderr, "bad destination %s\n", argv[0]);
            usage();
        }
    }
    else
        usage();

    for (Query.Index = RouteCacheChangeLogIndex(&Query.VA) + Verbose; ;
         Query.Index = Info->NextIndex) {
        uint NumRecords;
        uint Index;

        if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_ROUTE_CACHE_CHANGE_LOG,
                             &Query, sizeof Query,
                             Info, InfoSize,
                             &BytesReturned, NULL) ||
            (BytesReturned < sizeof *Info)) {
            fprintf(stderr, "query route cache change log error: %x\n",
                    GetLastError());
            exit(1);
        }

        NumRecords = ((BytesReturned - sizeof *Info) /
                      sizeof Info->Changes[0]);
        if ((Info->NextIndex >= Query.Index) &&
            (Info->NextIndex - Query.Index != NumRecords)) {
            fprintf(stderr, "query route cache change log: inconsistency\n");
            exit(1);
        }

        for (Index = 0; Index < NumRecords; Index++) {
            MCL_INFO_ROUTE_CACHE_CHANGE *Record = &Info->Changes[Index];
            SourceRoute UNALIGNED *SR =
                (SourceRoute UNALIGNED *) Record->Buffer;
            char *Host;
            uint Hop;

            if (Record->TimeStamp == 0)
                continue;

            if (BaseTime == 0)
                BaseTime = Record->TimeStamp;

            if (!IsUnspecified(Dest) &&
                !VirtualAddressEqual(Dest, Record->Dest))
                continue;

            printf("%8.2f",
                   (Record->TimeStamp - BaseTime) /
                   (double)(10*1000*1000));

            if (SR->optionType == 0) {
                Host = FormatHostName(Record->Dest);
                printf(" no route to %s\n",
                       (Host != NULL) ?
                       Host : FormatVirtualAddress(Record->Dest));
            }
            else {
                for (Hop = 0; Hop < SOURCE_ROUTE_HOPS(SR->optDataLen); Hop++) {
                    if (Hop != 0)
                        printf(" %s",
                               FormatLinkMetric(SR->hopList[Hop].Metric,
                                                VA->MetricType));

                    Host = FormatHostName(SR->hopList[Hop].addr);
                    printf(" %s",
                           (Host != NULL) ?
                           Host : FormatVirtualAddress(SR->hopList[Hop].addr));
                }
                if (SR->staticRoute)
                    printf(" (static)\n");
                else {
                    printf(" (metric %s ->",
                           FormatPathMetric(Record->PrevMetric, VA->MetricType));
                    printf(" %s)\n",
                           FormatPathMetric(Record->Metric, VA->MetricType));
                }
            }
        }
        fflush(stdout);
        CloseHandle(Handle);
        Sleep(1000); // One second.

        //
        // We close & reopen the handle to give the driver
        // a chance to unload.
        //

        Handle = CreateFileW(WIN_MCL_DEVICE_NAME,
                             0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,   // security attributes
                             OPEN_EXISTING,
                             0,      // flags & attributes
                             NULL);  // template file
        if (Handle == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Could not access MCL.\n");
            exit(1);
        }
    }
}

__inline uint
SizeofSR(uint NumHops)
{
    return sizeof(MCL_INFO_SOURCE_ROUTE) + NumHops * sizeof(MCL_INFO_SOURCE_ROUTE_HOP);
}

MCL_INFO_SOURCE_ROUTE *
GetSourceRoute(MCL_QUERY_CACHE_NODE *Query)
{
    uint BufferSize;
    MCL_INFO_SOURCE_ROUTE *SR;
    uint BytesReturned;

    //
    // Start with space for zero hops.
    //
    BufferSize = SizeofSR(0);
    SR = (MCL_INFO_SOURCE_ROUTE *) malloc(BufferSize);
    if (SR == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    for (;;) {
        if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_SOURCE_ROUTE,
                             Query, sizeof *Query,
                             SR, BufferSize, &BytesReturned,
                             NULL)) {
            if (GetLastError() == 41) // What error is this???
                fprintf(stderr, "no route to %s\n",
                        FormatVirtualAddress(Query->Address));
            else
                fprintf(stderr, "ioctl internal error\n");
            exit(1);
        }

        if ((BytesReturned < sizeof *SR) ||
            (BytesReturned > SizeofSR(SR->NumHops))) {
            fprintf(stderr, "inconsistent source route length\n");
            exit(1);
        }

        if (BytesReturned == SizeofSR(SR->NumHops))
            break;

        BufferSize = SizeofSR(SR->NumHops);
        free(SR);
        SR = (MCL_INFO_SOURCE_ROUTE *) malloc(BufferSize);
        if (SR == NULL) {
            fprintf(stderr, "malloc failed\n");
            exit(1);
        }
    }

    SR->Query = *Query;
    return SR;
}

void
PrintSourceRoute(MCL_INFO_VIRTUAL_ADAPTER *VA,
                 MCL_INFO_SOURCE_ROUTE *SR)
{
    uint i;

    if (SR->Static)
        printf("static route:\n");
    else
        printf("current route:\n");

    for (i = 0; i < SR->NumHops; i++) {
        char *Host = FormatHostName(SR->Hops[i].Address);

        if (Host != NULL)
            printf("%s (%s ",
                   Host,
                   FormatVirtualAddress(SR->Hops[i].Address));
        else
            printf("%s (", FormatVirtualAddress(SR->Hops[i].Address));
        printf("In %u -> Out %u)",
               SR->Hops[i].InIF,
               SR->Hops[i].OutIF);
        if ((i == 0) || SR->Static)
            printf("\n");
        else
            printf(" metric %s\n",
                   FormatLinkMetric(SR->Hops[i].Metric, VA->MetricType));
    }
}

void
QuerySourceRoute(int argc, char *argv[])
{
    MCL_QUERY_VIRTUAL_ADAPTER Query;
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    MCL_QUERY_CACHE_NODE Node;
    MCL_INFO_SOURCE_ROUTE *SR;

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //
    if ((argc > 0) && GetVirtualAdapter(argv[0], &Query)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
        VA = GetVirtualAdapterInfo(&Query);
    }
    else {
        VA = GetDefaultVirtualAdapter();
    }
    Node.VA = VA->This;

    if (argc != 1)
        usage();

    if (! GetVirtualAddress(argv[0], Node.Address) &&
        ! GetHostName(argv[0], Node.Address)) {
        fprintf(stderr, "bad destination %s\n", argv[0]);
        usage();
    }

    SR = GetSourceRoute(&Node);
    PrintSourceRoute(VA, SR);
}

void
ForEachMaintenanceBufferNode(MCL_QUERY_VIRTUAL_ADAPTER *VA,
                             void (*func)(MCL_INFO_MAINTENANCE_BUFFER_NODE *))
{
    MCL_QUERY_MAINTENANCE_BUFFER_NODE Query, NextQuery;
    MCL_INFO_MAINTENANCE_BUFFER_NODE MBN;
    uint BytesReturned;

    NextQuery.VA = *VA;
    RtlZeroMemory(&NextQuery.Node, sizeof NextQuery.Node);

    for (;;) {
        Query = NextQuery;

        if (!DeviceIoControl(Handle, IOCTL_MCL_QUERY_MAINTENANCE_BUFFER,
                             &Query, sizeof Query,
                             &MBN, sizeof MBN, &BytesReturned,
                             NULL)) {
            fprintf(stderr, "bad address %s\n",
                    FormatVirtualAddress(Query.Node.Address));
            exit(1);
        }

        NextQuery = MBN.Query;

        if (! IsUnspecified(Query.Node.Address)) {

            if (BytesReturned != sizeof MBN) {
                fprintf(stderr, "inconsistent info length\n");
                exit(1);
            }

            MBN.Query = Query;
            (*func)(&MBN);
        }
        else {
            if (BytesReturned != sizeof MBN.Query) {
                fprintf(stderr, "inconsistent info length\n");
                exit(1);
            }
        }

        if (IsUnspecified(NextQuery.Node.Address))
            break;
    }
}

void
PrintMaintenanceBufferNode(MCL_INFO_MAINTENANCE_BUFFER_NODE *Node)
{
    char *Host = FormatHostName(Node->Query.Node.Address);
    Time Now;

    if (Host != NULL)
        printf("%s (%s o%ui%u):\n",
               Host, FormatVirtualAddress(Node->Query.Node.Address),
               Node->Query.Node.OutIF, Node->Query.Node.InIF);
    else
        printf("%s o%ui%u:\n", FormatVirtualAddress(Node->Query.Node.Address),
               Node->Query.Node.OutIF, Node->Query.Node.InIF);
    
    GetSystemTimeAsFileTime((FILETIME *)&Now);
    printf("  Next %u Last %u LastRcv %ds FirstReq %ds LastReq %ds\n",
           Node->NextAckNum, Node->LastAckNum,
           TimeToSeconds(Now - Node->LastAckRcv),
           TimeToSeconds(Now - Node->FirstAckReq),
           TimeToSeconds(Now - Node->LastAckReq));
    printf("  Queued %u HW %u AckReq %u FastReq %u Valid %u Invalid %u\n",
           Node->NumPackets, Node->HighWater,
           Node->NumAckReqs, Node->NumFastReqs,
           Node->NumValidAcks, Node->NumInvalidAcks);
}

void
QueryMaintenanceBuffer(int argc, char *argv[])
{
    MCL_QUERY_VIRTUAL_ADAPTER Query;
    MCL_INFO_VIRTUAL_ADAPTER *VA;

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //
    if ((argc > 0) && GetVirtualAdapter(argv[0], &Query)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
        VA = GetVirtualAdapterInfo(&Query);
    }
    else {
        VA = GetDefaultVirtualAdapter();
    }

    if (argc != 0)
        usage();

    printf("VA %u Maintenance Buffer\n", VA->This.Index);
    ForEachMaintenanceBufferNode(&VA->This, PrintMaintenanceBufferNode);
}

void
ControlLink(int argc, char *argv[])
{
    MCL_CONTROL_LINK Request;
    uint BytesReturned;
    uint DropPercentage;

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //

    if ((argc > 0) && GetVirtualAdapter(argv[0], &Request.Node.VA)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
    }
    else {
        MCL_INFO_VIRTUAL_ADAPTER *VA;

        VA = GetDefaultVirtualAdapter();
        Request.Node.VA = VA->This;
    }

    //
    // The next two arguments specify the from and to addresses.
    // In addition, the arguments may optionally specify indices.
    //

    if (argc != 3)
        usage();

    if (! GetLinkAddressAndIndex(argv[0],
                                 Request.Node.Address, &Request.FromIF))
        usage();

    if (! GetLinkAddressAndIndex(argv[1],
                                 Request.To, &Request.ToIF))
        usage();

    //
    // The final argument specifies the percentage of packets to drop.
    //
    if ((!GetNumber(argv[2], &DropPercentage)) || DropPercentage > 100)
        usage();

    Request.DropRatio = DropPercentage * ((uint)-1 / 100);

    if (!DeviceIoControl(Handle, IOCTL_MCL_CONTROL_LINK,
                         &Request, sizeof Request,
                         NULL, 0, &BytesReturned, NULL)) {
        fprintf(stderr, "control link error: %x\n", GetLastError());
        exit(1);
    }
}

void
SendInformationRequest(int argc, char *argv[])
{
    MCL_QUERY_VIRTUAL_ADAPTER Query;
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    uint BytesReturned;

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //
    if ((argc > 0) && GetVirtualAdapter(argv[0], &Query)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
        VA = GetVirtualAdapterInfo(&Query);
    }
    else {
        VA = GetDefaultVirtualAdapter();
    }

    if (argc != 0)
        usage();

    printf("VA %u - Sending Information Request\n", VA->This.Index);

    if (!DeviceIoControl(Handle, IOCTL_MCL_INFORMATION_REQUEST,
                         &VA->This, sizeof VA->This,
                         NULL, 0, &BytesReturned, NULL)) {
        fprintf(stderr, "information request error: %x\n",
                GetLastError());
        exit(1);
    }
}

void
ResetStatistics(int argc, char *argv[])
{
    MCL_QUERY_VIRTUAL_ADAPTER Query;
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    uint BytesReturned;

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //
    if ((argc > 0) && GetVirtualAdapter(argv[0], &Query)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
        VA = GetVirtualAdapterInfo(&Query);
    }
    else {
        VA = GetDefaultVirtualAdapter();
    }

    if (argc != 0)
        usage();

    printf("VA %u - Resetting Statistics and Counters\n", VA->This.Index);

    if (!DeviceIoControl(Handle, IOCTL_MCL_RESET_STATISTICS,
                         &VA->This, sizeof VA->This,
                         NULL, 0, &BytesReturned, NULL)) {
        fprintf(stderr, "reset statistics error: %x\n",
                GetLastError());
        exit(1);
    }
}

uint 
ConvertAlpha(char *Val) 
{
    double Alpha = atof(Val); 

    //
    // Alpha must be between 0 and 1.
    //
    if ((Alpha < 0) || (Alpha > 1)) {
        fprintf(stderr, "Alpha must be between 0 and 1.\n");
        exit(1);
    }

    //
    // Alpha in rtt.c is an integer, scaled by MAXALPHA.
    //

    Alpha = Alpha * MAXALPHA;

    if (Alpha - floor(Alpha) < 0.5) 
         Alpha = floor(Alpha); 
    else
        Alpha = ceil(Alpha); 

    fprintf(stderr, "Requested=%s Achieved=%.3f\n", Val, Alpha/MAXALPHA);

    return (uint)Alpha; 
}

uint 
ConvertProbePeriod(char *Val) 
{
    double ProbePeriod = atof(Val);

    // 
    // Probe period must be between 0.1 and 429 seconds
    // The upper bound comes from the need to limit the
    // value in 100nanoseconds to be less than 2^32 - 1.
    //
    if ((ProbePeriod < 0.1) || (ProbePeriod > 429)) {
        fprintf(stderr, "Probe Period must be between 0.1 and 429\n");
        exit(1);
    }

    //
    // Convert Probe period into 100ns units.
    //
    ProbePeriod *= 10000000; 
    
    return (uint)ProbePeriod; 
}

uint 
ConvertHysteresisPeriod(char *Val) 
{
    double HysteresisPeriod = atof(Val);

    // 
    // Probe period must be between 0.1 and 429 seconds
    // The upper bound comes from the need to limit the
    // value in 100nanoseconds to be less than 2^32 - 1.
    //
    if ((HysteresisPeriod < 0.1) || (HysteresisPeriod > 429)) {
        fprintf(stderr, "Hysteresis Period must be between 0.1 and 429\n");
        exit(1);
    }

    //
    // Convert Probe period into 100ns units.
    //
    HysteresisPeriod *= 10000000; 
    
    return (uint)HysteresisPeriod; 
}

uint 
ConvertPenaltyFactor(char *Val) 
{
    int PenaltyFactor = atoi(Val); 

    //
    // Penalty Factor must be between 1 and 32.
    //
    if ((PenaltyFactor < 1) || (PenaltyFactor > 32)) {
        fprintf(stderr, "PenaltyFactor must be between 1 and 32.\n");
        exit(1);
    }

    return (uint)PenaltyFactor; 
}

uint 
ConvertSweepPeriod(char *Val) 
{
    double SweepPeriod = atof(Val);

    // 
    // SweepPeriod must be between 0.001 and 429 seconds
    // The upper bound comes from the need to limit the
    // value in 100nanoseconds to be less than 2^32 - 1.
    //
    if ((SweepPeriod < 0.001) || (SweepPeriod > 429)) {
        fprintf(stderr, "SweepPeriod must be between 0.001 and 429\n");
        exit(1);
    }

    //
    // Convert Probe period into 100ns units.
    //
    SweepPeriod *= 10000000; 
    
    return (uint)SweepPeriod; 
}

uint 
ConvertLossInterval(char *Val) 
{
    double ProbePeriod = atof(Val);

    // 
    // Probe period must be between 0.1 and 60 seconds. 
    // The lower bound prevents system overlaod, and upper
    // bound limits memory usage.
    //
    if ((ProbePeriod < 0.1) || (ProbePeriod > 60)) {
        fprintf(stderr, "Probe Period must be between 0.1 and 60\n");
        exit(1);
    }

    //
    // Convert Probe period into 100ns units.
    //
    ProbePeriod *= 10000000; 
    
    return (uint)ProbePeriod; 
}

uint 
ConvertPktPairMinOverProbes(char *Val) 
{
    int MinOverProbes = atoi(Val);

    //
    // MinOverProbes must be positive.
    //
    if (MinOverProbes < 1) {
        fprintf(stderr, "PktPairMinOverProbes must be at least 1\n");
        exit(1);
    }

    return (uint)MinOverProbes;
}

void
ControlVirtualAdapter(int argc, char *argv[])
{
    MCL_QUERY_VIRTUAL_ADAPTER Query;
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    MCL_CONTROL_VIRTUAL_ADAPTER Control;
    int i;
    uint BytesReturned;
   
    //
    // The first thing we need to do is to see if the user
    // provided a virtual adapter index. If not, we need
    // to find the default one. 
    // 
    if ((argc > 1) && GetVirtualAdapter(argv[0], &Query)) {
        //
        // The third argument specifies a virtual adapter.
        //
        VA = GetVirtualAdapterInfo(&Query);
        argc -= 1;
        argv += 1;
    }
    else {
        VA = GetDefaultVirtualAdapter();
    }

    //
    // We should have at least one argument.
    //
    if (argc < 1)
        usage();

    //
    // If all is well, set this as the virtual adapter to 
    // control, init all parameters to "NO CHANGE"
    // and parse the rest of the command. 
    //
    Control.This = VA->This; 
    MCL_INIT_CONTROL_VIRTUAL_ADAPTER(&Control);

    //
    // Parse the rest of the command to get configuration settings.
    //
    for (i = 0; i < argc; i++) {
        switch (Control.MetricType) {
        case (uint)-1:
            if (!strcmp(argv[i], "snoop"))
                Control.Snooping = TRUE;
            else if (!strcmp(argv[i], "-snoop"))
                Control.Snooping = FALSE;
            else if (!strcmp(argv[i], "artificialdrop"))
                Control.ArtificialDrop = TRUE;
            else if (!strcmp(argv[i], "-artificialdrop"))
                Control.ArtificialDrop = FALSE;
            else if (!strcmp(argv[i], "flapdamp") && (i + 1 < argc)) {
                if (! GetNumber(argv[++i], &Control.RouteFlapDampingFactor))
                    usage();
            }
            else if (!strcmp(argv[i], "crypto"))
                Control.Crypto = TRUE;
            else if (!strcmp(argv[i], "-crypto"))
                Control.Crypto = FALSE;
            else if (!strcmp(argv[i], "CryptoKeyMAC") && (i + 1 < argc)) {
                if (! Persistent) {
                    fprintf(stderr, "Can not change keys at runtime.\n");
                    exit(1);
                }
                if (! GetBinary(argv[++i], Control.CryptoKeyMAC,
                                sizeof Control.CryptoKeyMAC))
                    usage();
            }
            else if (!strcmp(argv[i], "CryptoKeyAES") && (i + 1 < argc)) {
                if (! Persistent) {
                    fprintf(stderr, "Can not change keys at runtime.\n");
                    exit(1);
                }
                if (! GetBinary(argv[++i], Control.CryptoKeyAES,
                                sizeof Control.CryptoKeyAES))
                    usage();
            }
            else if (!strcmp(argv[i], "LinkTimeout") && (i + 1 < argc)) {
                if (! GetNumber(argv[++i], &Control.LinkTimeout))
                    usage();
            }
            else if (!strcmp(argv[i], "HOP")) {
                if (! Persistent && (VA->MetricType != METRIC_TYPE_HOP)) {
                    fprintf(stderr, "Can not change metric at runtime.\n");
                    exit(1);
                }
                Control.MetricType = METRIC_TYPE_HOP;
            }
            else if (!strcmp(argv[i], "RTT")) {
                if (! Persistent && (VA->MetricType != METRIC_TYPE_RTT)) {
                    fprintf(stderr, "Can not change metric at runtime.\n");
                    exit(1);
                }
                Control.MetricType = METRIC_TYPE_RTT;
                MCL_INIT_CONTROL_RTT(&Control);
            }
            else if (!strcmp(argv[i], "PKTPAIR")) {
                if (! Persistent && (VA->MetricType != METRIC_TYPE_PKTPAIR)) {
                    fprintf(stderr, "Can not change metric at runtime.\n");
                    exit(1);
                }
                Control.MetricType = METRIC_TYPE_PKTPAIR;
                MCL_INIT_CONTROL_PKTPAIR(&Control);
            }
            else if (!strcmp(argv[i], "ETX")) {
                if (! Persistent && (VA->MetricType != METRIC_TYPE_ETX)) {
                    fprintf(stderr, "Can not change metric at runtime.\n");
                    exit(1);
                }
                Control.MetricType = METRIC_TYPE_ETX;
                MCL_INIT_CONTROL_ETX(&Control);
            }
            else if (!strcmp(argv[i], "WCETT")) {
                if (! Persistent && (VA->MetricType != METRIC_TYPE_WCETT)) {
                    fprintf(stderr, "Can not change metric at runtime.\n");
                    exit(1);
                }
                Control.MetricType = METRIC_TYPE_WCETT;
                MCL_INIT_CONTROL_WCETT(&Control);
            }
            else
                usage();
            break;

        case METRIC_TYPE_RTT:
            if (!strcmp(argv[i], "Alpha") && (i + 1 < argc))
                Control.MetricParams.Rtt.Alpha = ConvertAlpha(argv[++i]); 
            else if (!strcmp(argv[i], "PP") && (i + 1 < argc))
                Control.MetricParams.Rtt.ProbePeriod = ConvertProbePeriod(argv[++i]);
            else if (!strcmp(argv[i], "HP") && (i + 1 < argc))
                Control.MetricParams.Rtt.HysteresisPeriod = ConvertHysteresisPeriod(argv[++i]);
            else if (!strcmp(argv[i], "PF") && (i + 1 < argc))
                Control.MetricParams.Rtt.PenaltyFactor = ConvertPenaltyFactor(argv[++i]);
            else if (!strcmp(argv[i], "SP") && (i + 1 < argc))
                    Control.MetricParams.Rtt.SweepPeriod = ConvertSweepPeriod(argv[++i]);
            else if (!strcmp(argv[i], "random"))
                Control.MetricParams.Rtt.Random = TRUE;
            else if (!strcmp(argv[0], "-random"))
                Control.MetricParams.Rtt.Random = FALSE;
            else if (!strcmp(argv[0], "override"))
                Control.MetricParams.Rtt.OutIfOverride = TRUE;
            else if (!strcmp(argv[0], "-override"))
                Control.MetricParams.Rtt.OutIfOverride = FALSE;
            else
                usage();
            break;

        case METRIC_TYPE_PKTPAIR:
            if (!strcmp(argv[i], "Alpha") && (i + 1 < argc))
                Control.MetricParams.PktPair.Alpha = ConvertAlpha(argv[++i]); 
            else if (!strcmp(argv[i], "PP") && (i + 1 < argc))
                Control.MetricParams.PktPair.ProbePeriod = ConvertProbePeriod(argv[++i]);
            else if (!strcmp(argv[i], "PF") && (i + 1 < argc))
                Control.MetricParams.PktPair.PenaltyFactor = ConvertPenaltyFactor(argv[++i]);
            else
                usage();
            break;

        case METRIC_TYPE_ETX:
            if (!strcmp(argv[i], "PP") && (i + 1 < argc))
                Control.MetricParams.Etx.ProbePeriod = ConvertProbePeriod(argv[++i]);
            else if (!strcmp(argv[i], "LI") && (i + 1 < argc))
                Control.MetricParams.Etx.LossInterval = ConvertLossInterval(argv[++i]);
            else if (!strcmp(argv[i], "Alpha") && (i + 1 < argc))
                Control.MetricParams.Etx.Alpha = ConvertAlpha(argv[++i]); 
            else if (!strcmp(argv[i], "PF") && (i + 1 < argc))
                Control.MetricParams.Etx.PenaltyFactor = ConvertPenaltyFactor(argv[++i]);
            else
                usage();
            break;

        case METRIC_TYPE_WCETT:
            if (!strcmp(argv[i], "PP") && (i + 1 < argc))
                Control.MetricParams.Wcett.ProbePeriod = ConvertProbePeriod(argv[++i]);
            else if (!strcmp(argv[i], "LI") && (i + 1 < argc))
                Control.MetricParams.Wcett.LossInterval = ConvertLossInterval(argv[++i]);
            else if (!strcmp(argv[i], "Alpha") && (i + 1 < argc))
                Control.MetricParams.Wcett.Alpha = ConvertAlpha(argv[++i]); 
            else if (!strcmp(argv[i], "PF") && (i + 1 < argc))
                Control.MetricParams.Wcett.PenaltyFactor = ConvertPenaltyFactor(argv[++i]);
            else if (!strcmp(argv[i], "Beta") && (i + 1 < argc))
                Control.MetricParams.Wcett.Beta = ConvertAlpha(argv[++i]);
            else if (!strcmp(argv[i], "PktPairPP") && (i + 1 < argc))
                Control.MetricParams.Wcett.PktPairProbePeriod = ConvertProbePeriod(argv[++i]);
            else if (!strcmp(argv[i], "PktPairMOP") && (i + 1 < argc))
                Control.MetricParams.Wcett.PktPairMinOverProbes = ConvertPktPairMinOverProbes(argv[++i]);
            else
                usage();
            break;

        default:
            usage();
            break;
        }
    }

    if (!DeviceIoControl(Handle,
                         (Persistent ?
                          IOCTL_MCL_PERSISTENT_CONTROL_VIRTUAL_ADAPTER :
                          IOCTL_MCL_CONTROL_VIRTUAL_ADAPTER),
                         &Control, sizeof Control,
                         NULL, 0, &BytesReturned, NULL)) {
        fprintf(stderr, "control error: %x\n", GetLastError());
        exit(1);
    }
}

MCL_INFO_SOURCE_ROUTE *
ReadSourceRoute(FILE *fp) 
{
    MCL_INFO_SOURCE_ROUTE *SR;
    uint NumHops;
    uint Hop; 

    if ((fscanf(fp, "Hops %u\n", &NumHops) != 1) ||
        (NumHops < 1)) {
        fprintf(stderr, "too few hops\n");
        exit(1);
    }

    SR = malloc(sizeof *SR + NumHops * sizeof SR->Hops[0]);
    if (SR == NULL) { 
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    for (Hop = 0; Hop < NumHops; Hop++) {
        char buffer[64];

        if (fscanf(fp, "%64s %u %u\n", buffer,
                   &SR->Hops[Hop].InIF, &SR->Hops[Hop].OutIF) != 3) {
            fprintf(stderr, "fscanf failure\n");
            exit(1);
        }

        if (! GetVirtualAddress(buffer, SR->Hops[Hop].Address)) {
            fprintf(stderr, "bad address: %s\n", buffer);
            exit(1);
        }

    }

    SR->NumHops = NumHops;
    return SR;
}

void
AddRoutesFromFile(int argc, char *argv[])
{
    MCL_QUERY_VIRTUAL_ADAPTER Query;
    MCL_INFO_VIRTUAL_ADAPTER *VA;
    MCL_INFO_SOURCE_ROUTE *SR;
    uint NumRoutes;
    uint BytesReturned;
    FILE *fp; 
    uint i;

    //
    // The first argument is optional. If present,
    // it specifies the virtual adapter.
    // It must be present if there is more than one virtual adapter.
    //

    if ((argc > 0) && GetVirtualAdapter(argv[0], &Query)) {
        //
        // The first argument specifies a virtual adapter.
        //
        argc -= 1;
        argv += 1;
        VA = GetVirtualAdapterInfo(&Query);
    }
    else {
        VA = GetDefaultVirtualAdapter();
    }

    //
    // The next argument specifies a filename that contains
    // a list of routes. 
    //
    if (argc != 1)
        usage();
   
    //
    // The file preamble lists the number of routes in the file, 
    // followed by a list of those routes.
    // Each route is preceded by a hopcount, followed by a list 
    // of hops. The route is always listed from source to 
    // destination.
    // Each hop lists the address of the virtual adapter in the 
    // usual xx-xx-xx-xx-xx-xx format, followed by index of 
    // IN interface, and then index of the OUT interface.
    // There are no blank lines in the file.
    //
    fp = fopen(argv[0], "r"); 
    if (fp == NULL) {
        fprintf(stderr, "Can't open file %s for reading\n", argv[0]);
        exit(1);
    }

    //
    // Read the number of routes
    //
    NumRoutes = 0;
    if ((fscanf(fp, "Routes %u\n", &NumRoutes) != 1) ||
        (NumRoutes <= 0)) {
        fprintf(stderr, "Number of routes in the file must be greater than zero\n");
        exit(1);
    }
    
    // 
    // Read each route, and if the source is this node, add it to the
    // routing table. 
    //
    for (i = 0; i < NumRoutes; i++) {
        SR = ReadSourceRoute(fp);

        // 
        // Are we the source of this route?
        //
        if (VirtualAddressEqual(SR->Hops[0].Address, VA->Address)) {
            //
            // Insert the static route.
            //
            SR->Query.VA = VA->This;
            RtlCopyMemory(SR->Query.Address, SR->Hops[SR->NumHops - 1].Address,
                          sizeof(VirtualAddress));
            SR->Static = TRUE;

            if (!DeviceIoControl(Handle, IOCTL_MCL_ADD_SOURCE_ROUTE,
                                 SR, sizeof *SR + SR->NumHops * sizeof SR->Hops[0],
                                 NULL, 0, &BytesReturned, NULL)) {
                fprintf(stderr, "add route error: %x\n", GetLastError());
                exit(1);
            }
        }

        free(SR);
    }
    fclose(fp);
}

void
GenerateRandom(int argc, char *argv[])
{
    uchar *Buffer;
    uint NumBytes;
    uint BytesReturned;

    if (argc == 0) {
        //
        // This is the size of our keys.
        //
        NumBytes = LQSR_KEY_SIZE;
    }
    else if (argc == 1) {
        if (! GetNumber(argv[0], &NumBytes))
            usage();
    }
    else
        usage();

    Buffer = malloc(NumBytes);
    if (Buffer == NULL) {
        fprintf(stderr, "mcl: malloc failed\n");
        exit(1);
    }

    if (!DeviceIoControl(Handle, IOCTL_MCL_GENERATE_RANDOM,
                         NULL, 0,
                         Buffer, NumBytes, &BytesReturned, NULL) ||
        (NumBytes != BytesReturned)) {
        fprintf(stderr, "mcl: generate random: %x\n", GetLastError());
        exit(1);
    }

    PrintBinary(Buffer, NumBytes);
    printf("\n");
}

int __cdecl
main(int argc, char **argv)
{
    WSADATA wsaData;
    LPTSTR DeviceDesc = TEXT("mcl");
    int Count;
   
    if (WSAStartup(MAKEWORD(2, 0), &wsaData)) {
        fprintf(stderr, "WSAStartup failed\n");
        exit(1);
    }

    //
    // Parse any global options.
    //
    for (Count = 1; Count < argc; Count++) {
        if (!strcmp(argv[Count], "-v"))
            Verbose = TRUE;
        else if (!strcmp(argv[Count], "-p"))
            Persistent = TRUE;
        else if (!strcmp(argv[Count], "-d"))
            ConvertHostName = FALSE;
        else
            break;
    }

    argc -= Count;
    argv += Count;

    if (argc < 1)
        usage();

    //
    // Start with the commands that don't access the device.
    //

    if (!strcmp(argv[0], "install")) {
        switch (argc) {
        case 2:
            AddOrRemoveMCL(TRUE, argv[1]);
            break;
        case 1:
            AddOrRemoveMCL(TRUE, NULL);
            break;
        default:
            usage();
        }
        exit(0);
    }

    if (!strcmp(argv[0], "uninstall")) {
        if (argc != 1)
            usage();
        AddOrRemoveMCL(FALSE, NULL);
        exit(0);
    }

    if (!strcmp(argv[0], "enable")) {
        if (argc > 1)
            DeviceDesc = argv[1];
        Count = ControlDeviceClass(DeviceDesc, DICS_ENABLE);
        printf("%u Adapter%s enabled.\n", Count, (Count == 1) ? "" : "s");
        exit(0);
    }

    if (!strcmp(argv[0], "disable")) {
        if (argc > 1)
            DeviceDesc = argv[1];
        Count = ControlDeviceClass(DeviceDesc, DICS_DISABLE);
        printf("%u Adapter%s disabled.\n", Count, (Count == 1) ? "" : "s");
        exit(0);
    }

    if (!strcmp(argv[0], "restart")) {
        if (argc > 1)
            DeviceDesc = argv[1];
        Count = ControlDeviceClass(DeviceDesc, DICS_PROPCHANGE);
        printf("%u Adapter%s restarted.\n", Count, (Count == 1) ? "" : "s");
        exit(0);
    }

    //
    // Request write access to the device.
    // This will fail if the process does not have appropriate privs.
    //
    Handle = CreateFileW(WIN_MCL_DEVICE_NAME,
                         GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,   // security attributes
                         OPEN_EXISTING,
                         0,      // flags & attributes
                         NULL);  // template file
    if (Handle == INVALID_HANDLE_VALUE) {
        //
        // We will not have Administrator access to the stack.
        //
        AdminAccess = FALSE;

        Handle = CreateFileW(WIN_MCL_DEVICE_NAME,
                             0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,   // security attributes
                             OPEN_EXISTING,
                             0,      // flags & attributes
                             NULL);  // template file
        if (Handle == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Could not access MCL.\n");
            exit(1);
        }
    }

    if (!strcmp(argv[0], "va")) {
        QueryVirtualAdapter(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "vac")) {
        if (! AdminAccess)
            ausage();
        ControlVirtualAdapter(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "pac")) {
        if (! AdminAccess)
            ausage();
        ControlPhysicalAdapter(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "nc")) {
        QueryNeighborCache(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "ncf")) {
        if (! AdminAccess)
            ausage();
        FlushNeighborCache(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "lc")) {
        QueryLinkCache(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "lca")) {
        if (! AdminAccess)
            ausage();
        AddLink(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "lcf")) {
        if (! AdminAccess)
            ausage();
        FlushLinkCache(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "lcc")) {
        LinkCacheChangeLog(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "rcc")) {
        RouteCacheChangeLog(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "sr")) {
        QuerySourceRoute(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "mb")) {
        QueryMaintenanceBuffer(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "ad")) {
        if (! AdminAccess)
            ausage();
        ControlLink(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "inforeq")) {
        SendInformationRequest(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "reset")) {
        ResetStatistics(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "rca")) {
        if (! AdminAccess)
            ausage();
        AddRoutesFromFile(argc - 1, argv + 1);
    }
    else if (!strcmp(argv[0], "rand")) {
        GenerateRandom(argc - 1, argv + 1);
    }
    else {
        usage();
    }
    exit(0);
}
