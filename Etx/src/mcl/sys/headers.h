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

#pragma warning(disable:4115) // named type definition in parentheses
#pragma warning(disable:4200) // zero-sized array in struct/union
#pragma warning(disable:4201) // nameless struct/union
#pragma warning(disable:4213) // cast on l-value
#pragma warning(disable:4214) // bit field types other than int

#define NDIS_MINIPORT_DRIVER
#define NDIS51_MINIPORT 1
#define NDIS_WDM 1
#define NDIS51
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WINXP
#include <ndis.h>
#include <ntddk.h>
#include <align.h>
#include <malloc.h>

typedef struct ProtocolAdapter ProtocolAdapter;
typedef struct MiniportAdapter MiniportAdapter;
typedef struct NeighborCache NeighborCache;
typedef struct NeighborCacheEntry NeighborCacheEntry;
typedef struct RttParams RttParams; 
typedef struct PktPairParams PktPairParams; 
typedef struct EtxParams EtxParams; 
typedef struct WcettParams WcettParams; 

#define CHANGELOGS      1       // Must be before internal headers.
#define NUM_LINKCHANGE_RECORDS  256
#define NUM_ROUTECHANGE_RECORDS 256

#include "types.h"
#include "ether.h"
#include "lqsr.h"
#include "ntddmcl.h"
#include "sr.h"
#include "linkcache.h"
#include "reqtable.h"
#include "sendbuf.h"
#include "maintbuf.h"

//
// Multipliers to convert 100ns time unit. 
//

#define MICROSECOND (10)
#define MILLISECOND (1000 * MICROSECOND)
#define SECOND      (1000 * MILLISECOND)
#define MINUTE      (60 * SECOND)

// Minimum 5 milliseconds gap between broadcast packets.
#define MIN_BROADCAST_GAP (5 * MILLISECOND)

// Maximum number of broadcast packets we will queue.
#define MAX_FORWARD_QUEUE       16

// 100 ms backoff after first Route Request, 2 second max backoff
#define FIRST_BACKOFF (100 * MILLISECOND)        
#define MAX_BACKOFF (2 * SECOND)

// We think a link is broken if we do not receive an ack after this interval.
#define MAINTBUF_LINK_TIMEOUT   (500 * MILLISECOND)

// We retransmit an ack request after this interval.
// Must be <= MAINTBUF_LINK_TIMEOUT. Equal means no rexmits.
#define MAINTBUF_REXMIT_TIMEOUT (500 * MILLISECOND)

// We assume the link is working within this interval after an ack.
#define MAINTBUF_HOLDOFF_TIME   (250 * MILLISECOND)

// We delete a Maintenance Buffer Node after one day of inactivity.
// TODO - This is very large while we collect measurements.
#define MAINTBUF_IDLE_TIMEOUT   ((Time)24 * 60 * MINUTE)

// We limit the number of packets held in a Maintenance Buffer Node.
// This number should be large enough to hold "initial" packets
// sent when connections are starting up.
#define MAINTBUF_MAX_QUEUE      4

// We delay an Acknowledgement no longer than 80ms.
#define MAX_ACK_DELAY           (80 * MILLISECOND)

#define SENDBUF_TIMEOUT (FIRST_BACKOFF + MAX_BACKOFF)

// We send link information at least every 10 seconds.
#define LINKINFO_PERIOD         ((Time)10 * SECOND)

// if PACKET_POOL_SZ <= SEND_BUF_SZ, then bad things happen really fast
#define PACKET_POOL_SZ       100
#define SEND_BUF_SZ          20

// Default Route Flap Damping Factor. Used in LinkCacheFlapDamp.
#define DEFAULT_ROUTE_FLAP_DAMPING_FACTOR 32

//
// Functionality in sidebug.c.
//

#define MCL_POOL_TAG    'LCM'

#ifdef POOL_TAGGING

#ifdef ExAllocatePool
#undef ExAllocatePool
#endif

#define ExAllocatePool(type, size) ExAllocatePoolWithTag((type), (size), MCL_POOL_TAG)

#endif // POOL_TAGGING

#ifndef COUNTING_MALLOC
#define COUNTING_MALLOC DBG
#endif

#if COUNTING_MALLOC

#ifdef ExFreePool
#undef ExFreePool
#endif

#define ExAllocatePoolWithTag(type, size, tag)  CountingExAllocatePoolWithTag((type), (size), (tag), __FILE__, __LINE__)

#define ExFreePool(p) CountingExFreePool(p)

extern VOID *
CountingExAllocatePoolWithTag(
    IN POOL_TYPE        PoolType,
    IN ULONG            NumberOfBytes,
    IN ULONG            Tag,
    IN PCHAR            File,
    IN ULONG            Line);

extern VOID
CountingExFreePool(
    PVOID               p);

extern VOID
InitCountingMalloc(void);

extern VOID
DumpCountingMallocStats(void);

extern VOID
UnloadCountingMalloc(void);

#endif  // COUNTING_MALLOC

//
// Functionality in protocol.c.
//

//
// We can only use physical adapters that have a sufficiently large
// frame size that we can add our LQSR header.
//
#define PROTOCOL_MIN_FRAME_SIZE 1500

//
// We start dropping packets when a transmit queue gets bigger than this.
//
#define PROTOCOL_MAX_QUEUE      50

struct ProtocolAdapter {
    MiniportAdapter *VirtualAdapter;

    uint Index;
    GUID Guid;

    NDIS_HANDLE Handle;

    //
    // List of physical adapters bound to the virtual adapter,
    // locked by VirtualAdapter->Lock.
    //
    struct ProtocolAdapter **Prev, *Next;

    //
    // For allocating packets in ProtocolReceive.
    //
    NDIS_HANDLE PacketPool;
    NDIS_HANDLE BufferPool;

    //
    // For calling NdisOpenAdapter and NdisCloseAdapter.
    //
    KEVENT Event;
    NDIS_STATUS Status;

    //
    // Synchronizes Channel & Bandwidth attribute retrieval from NDIS.
    //
    FAST_MUTEX Mutex;

    //
    // Various attributes of the adapter.
    //
    uint MaxFrameSize;
    PhysicalAddress Address;
    boolint Promiscuous;
    boolint ReceiveOnly;
    uint Channel;
    boolint ChannelConfigured;          // Was Channel manually configured?
    uint Bandwidth;
    boolint BandwidthConfigured;        // Was Bandwidth manually configured?

    //
    // Counters for monitoring/debugging purposes.
    //
    uint PacketsSent;           // Total calls to NdisSend.
    uint PacketsReceived;       // Total ProtocolReceive/ProtocolReceivePacket.
    uint PacketsReceivedTD;     // ProtocolReceive with Transfer Data.
    uint PacketsReceivedFlat;   // ProtocolReceive without Transfer Data.

    uint CountPacketPoolFailure;

    uint CountRecvOutstanding;  // Packets received but not completed.
    uint MaxRecvOutstanding;    // CountRecvOutstanding high-water mark.
    uint CountSentOutstanding;  // Packets sent but not completed.
    uint MaxSentOutstanding;    // CountSentOutstanding high-water mark.
};

extern ProtocolAdapter *
FindPhysicalAdapterFromIndex(MiniportAdapter *VA, uint Index);

extern ProtocolAdapter *
FindPhysicalAdapterFromGuid(MiniportAdapter *VA, const GUID *Guid);

extern boolint
ProtocolQueueFull(MiniportAdapter *VA, LQSRIf OutIf);

extern NDIS_STATUS
ProtocolInit(const WCHAR *Name, MiniportAdapter *VA);

extern void
ProtocolCleanup(MiniportAdapter *VA);

extern void
ProtocolTransmit(ProtocolAdapter *PA, NDIS_PACKET *Packet);

extern void
ProtocolForwardRequest(MiniportAdapter *VA, SRPacket *srp);

extern void
ProtocolResetStatistics(ProtocolAdapter *PA);

extern void
ProtocolQuery(ProtocolAdapter *PA, MCL_INFO_PHYSICAL_ADAPTER *Info);

extern NTSTATUS
ProtocolControl(ProtocolAdapter *PA, MCL_CONTROL_PHYSICAL_ADAPTER *Control);

//
// Functionality in neighbor.c.
//

//
// The NeighborCacheEntry maps a virtual address and
// an interface index (representing another node's interface)
// to a physical address (of that other interface).
//
struct NeighborCacheEntry {
    NeighborCacheEntry *Next;
    NeighborCacheEntry *Prev;

    VirtualAddress VAddress;
    LQSRIf InIf;
    PhysicalAddress PAddress;
};

struct NeighborCache {
    KSPIN_LOCK Lock;
    NeighborCacheEntry *FirstNCE;
    NeighborCacheEntry *LastNCE;
};

__inline NeighborCacheEntry *
SentinelNCE(NeighborCache *NC)
{
    return (NeighborCacheEntry *) &NC->FirstNCE;
}

extern void
NeighborCacheInit(NeighborCache *NC);

extern void
NeighborCacheCleanup(NeighborCache *NC);

extern void
NeighborCacheFlushAddress(NeighborCache *NC, const VirtualAddress VAddr,
                          LQSRIf InIf);

extern void
NeighborReceivePassive(NeighborCache *NC, const VirtualAddress VAddr,
                       LQSRIf InIf, const PhysicalAddress PAddr);

extern boolint
NeighborFindPhysical(NeighborCache *NC, const VirtualAddress VAddr,
                     LQSRIf InIf, PhysicalAddress PAddr);

//
// Functionality in pback.c
//
// Currently the piggy-back cache uses a simple sorted list
// of options waiting to be sent. We expect the cache size
// to be small - in a lightly-loaded system not many options
// are sent, and in a highly-loaded system many packets are
// sent and so options leave the cache quickly.
//

#define PBACK_ROUTE_REPLY_TIMEOUT       5000            // 1/2 millisecond.
#define PBACK_ROUTE_ERROR_TIMEOUT       50000           // 5 milliseconds.
#define PBACK_INFO_REPLY_TIMEOUT        5000000         // 1/2 second.
#define PBACK_ROUTE_REPLY_SR_TIMEOUT    10000000        // 1 second.

typedef struct PbackOption PbackOption;

struct PbackOption {
    PbackOption *Next;
    VirtualAddress Dest;
    InternalOption *Opt;
    Time Timeout;
};

typedef struct PbackCache {
    KSPIN_LOCK Lock;
    PbackOption *List;

    uint Number;
    uint HighWater;
    Time AckMaxDupTime;

    uint CountPbackTooBig;
    uint CountPbackTotal;
    uint CountAloneTotal;
    uint CountPbackAck;
    uint CountAloneAck;
    uint CountPbackReply;
    uint CountAloneReply;
    uint CountPbackError;
    uint CountAloneError;
    uint CountPbackInfo;
    uint CountAloneInfo;
} PbackCache;

extern void
PbackInit(MiniportAdapter *VA);

extern void
PbackCleanup(MiniportAdapter *VA);

extern void
PbackResetStatistics(MiniportAdapter *VA);

extern void
PbackSendOption(MiniportAdapter *VA, const VirtualAddress Dest,
                InternalOption *Opt, Time Timeout);

extern void
PbackSendPacket(MiniportAdapter *VA, SRPacket *SRP);

extern Time
PbackTimeout(MiniportAdapter *VA, Time Now);

extern uint
PbackPacketSize(SRPacket *SRP);

//
// Functionality in etx.c.
//

struct EtxParams {
    uint ProbePeriod; 
    uint LossInterval;
    uint Alpha;
    uint PenaltyFactor;
    Time ProbeTimeout;
}; 

extern void EtxInit(
    MiniportAdapter *VA);

extern void
EtxPenalize(
    MiniportAdapter *VA,
    Link *Adj);

extern Time
EtxSendProbes(
    MiniportAdapter *VA,
    Time Now);

extern void
EtxReceiveProbe(
    MiniportAdapter *VA,
    InternalProbe *Probe,
    LQSRIf InIf);


//
// Functionality in wcett.c
//

struct WcettParams {
    uint ProbePeriod; 
    uint LossInterval;
    uint Alpha;
    uint PenaltyFactor;
    Time ProbeTimeout;
    uint Beta;
    uint PktPairProbePeriod;
    uint PktPairMinOverProbes;
};

extern void
WcettInit(MiniportAdapter *VA);

extern void
WcettPenalize(MiniportAdapter *VA, Link *Adj);

extern Time
WcettSendProbes(MiniportAdapter *VA, Time Now);

extern void
WcettReceiveProbe(MiniportAdapter *VA, InternalProbe *Probe, LQSRIf InIf);

extern void
WcettReceivePktPairProbeReply(MiniportAdapter *VA,
                              InternalProbeReply *ProbeReply);

extern uint
WcettEncodeBandwidth(uint Bandwidth);

extern uint
WcettDecodeBandwidth(uint Bandwidth);

//
// Functionality in pktpair.c.
//

struct PktPairParams {
    uint Alpha; 
    uint ProbePeriod; 
    uint PenaltyFactor;
}; 

extern void
PktPairPenalize(
    MiniportAdapter *VA,
    Link *Link);

extern void 
PktPairInit(MiniportAdapter *VA);

extern Time
PktPairSendProbes(
    MiniportAdapter *VA,
    Time Now); 

void
PktPairReceiveProbe(
    MiniportAdapter *VA, 
    InternalProbe *Probe);

void
PktPairSendProbeReply(
    MiniportAdapter *VA, 
    InternalProbe *Probe, 
    Time Now, 
    Time OutDelta);

void
PktPairReceiveProbeReply(
    MiniportAdapter *VA,
    InternalProbeReply *ProbeReply);

NDIS_STATUS
PktPairCreateProbePacket(
    MiniportAdapter *VA,
    Link *Adjacent,
    Time Timestamp,
    NDIS_PACKET **ReturnPacket,
    uint Seq,
    boolint LargeProbe);

void
PktPairSendProbeComplete(
    MiniportAdapter *VA,
    NDIS_PACKET *Packet,
    NDIS_STATUS Status);

//
// Functionality in rtt.c
//

struct RttParams {
    uint Alpha; 
    uint ProbePeriod; 
    uint HysteresisPeriod; 
    uint PenaltyFactor;
    uint SweepPeriod;
    Time SweepTimeout;
    boolint Random;
    boolint OutIfOverride;
}; 

extern void
RttInit(MiniportAdapter *VA); 

extern void
RttPenalize(
    MiniportAdapter *VA,
    Link *Link);

extern Time
RttSendProbes(MiniportAdapter *VA, Time Now);

extern void
RttReceiveProbe(MiniportAdapter *VA, InternalProbe *Probe); 

extern void
RttSendProbeReply(MiniportAdapter *VA, InternalProbe *Probe);

extern void
RttReceiveProbeReply(MiniportAdapter *VA, InternalProbeReply *ProbeReply);

extern Time
RttSweepForLateProbes(MiniportAdapter *VA, Time Now);

extern Time
RttUpdateMetric(MiniportAdapter *VA, Time Now);

extern void 
RttSelectOutIf(
    MiniportAdapter *VA,
    const uchar *Dest,
    LQSRIf *OutIf,
    LQSRIf *InIf);

//
// Functionality in crypto.c.
//

#define AES_KEYSIZE_128 (16)

#define CRYPTO_KEY_MAC_LENGTH   16

extern void
CryptoKeyMACModify(MiniportAdapter *VA, uchar Key[LQSR_KEY_SIZE]);

extern void
CryptoMAC(uchar CryptoKeyMAC[CRYPTO_KEY_MAC_LENGTH],
          uchar *MAC, uint Length,
          NDIS_PACKET *Packet, uint Offset);

extern NDIS_STATUS
CryptoEncryptPacket(MiniportAdapter *VA, NDIS_PACKET *OrigPacket,
                    uint Offset,
                    uchar IV[LQSR_IV_LENGTH], NDIS_PACKET **pEncryptedPacket);

extern NDIS_STATUS
CryptoDecryptPacket(MiniportAdapter *VA, NDIS_PACKET *OrigPacket,
                    uint OrigOffset, uint DecryptOffset,
                    uchar IV[LQSR_IV_LENGTH], NDIS_PACKET **pDecryptedPacket);

//
// NDIS-related helper functions, in ndishack.c.
//

typedef struct PacketContext {
    union {
        void (*TransmitComplete)(MiniportAdapter *VA, NDIS_PACKET *Packet, NDIS_STATUS Status);
        void (*ReceiveComplete)(MiniportAdapter *VA, SRPacket *srp, NDIS_STATUS Status);
    };
    NDIS_PACKET *OrigPacket;
    NDIS_BUFFER *OrigBuffer;
    void *CloneData;
    union {
        SRPacket *srp;
        MaintBufPacket *MBP;
    };
    ProtocolAdapter *PA;
} PacketContext;

__inline PacketContext *
PC(NDIS_PACKET *Packet)
{
    return (PacketContext *)Packet->ProtocolReserved;
}

extern NDIS_STATUS
NdisClonePacket(NDIS_PACKET *OrigPacket,
                NDIS_HANDLE PacketPool, NDIS_HANDLE BufferPool,
                uint OrigHeaderLength, uint CloneHeaderLength,
                uint LookAhead,
                void **pOrigHeader,
                NDIS_PACKET **pClonePacket, void **pCloneHeader);

extern void
NdisFreePacketClone(NDIS_PACKET *ClonePacket);

extern NDIS_STATUS
NdisMakeEmptyPacket(NDIS_HANDLE PacketPool, NDIS_HANDLE BufferPool,
                    uint Size,
                    NDIS_PACKET **pPacket, void **pData);

//
// Hack around problems and limitations in the NDIS interface,
// by relying on knowledge of internal NDIS data structures.
//

__inline PNDIS_BUFFER
NdisFirstBuffer(PNDIS_PACKET Packet)
{
    return Packet->Private.Head;
}

extern void
NdisAdjustBuffer(NDIS_BUFFER *Buffer, void *Address, uint Length);

extern NDIS_HANDLE
NdisProtocolFromBindContext(NDIS_HANDLE BindContext);

extern void **
NdisReservedFieldInProtocol(NDIS_HANDLE Protocol);

//
// Functionality in miniport.c.
//

typedef union MetricParams {
    RttParams Rtt;
    PktPairParams PktPair;
    EtxParams Etx;
    WcettParams Wcett;
} MetricParams; 

extern struct MiniportAdapters {
    KSPIN_LOCK Lock;
    MiniportAdapter *VirtualAdapters;
} MiniportAdapters;

struct MiniportAdapter {
    MiniportAdapter **Prev, *Next;

    uint Index;
    GUID Guid;

    NDIS_HANDLE MiniportHandle;
    NDIS_HANDLE ProtocolHandle;

    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT DeviceObject;

    KSPIN_LOCK Lock;
    ProtocolAdapter *PhysicalAdapters;
    uint NextPhysicalAdapterIndex;

    Time Timeout;
    KTIMER Timer;
    KDPC TimeoutDpc;
    uint TimeoutMinLoops;
    uint TimeoutMaxLoops;

    NeighborCache NC;
    PbackCache PCache;
    LinkCache *LC;
    ForwardedRequestTable *ReqTable;
    SendBuf *SB;
    MaintBuf *MaintBuf;


    SRPacket *ForwardList;
    SRPacket **ForwardLast;
    Time ForwardTime;

    Time LastLinkInfo;

    VirtualAddress Address;

    boolint Snooping;           // Learn route info from overhead packets?
    
    boolint Crypto;             // Encrypt the packets? 
    uchar CryptoKeyMAC[CRYPTO_KEY_MAC_LENGTH];
    uchar CryptoKeyAES[AES_KEYSIZE_128];

    boolint ArtificialDrop;     // Simulate lossy links by dropping packets?

    Time LinkTimeout;           // How long do stale links live?

    NDIS_MEDIUM Medium;
    uint PacketFilter;
    uint LookAhead;
    uint MediumLinkSpeed;
    uint MediumMinPacketLen;
    uint MediumMaxPacketLen;
    uint MediumMacHeaderLen;
    uint MediumMaxFrameLen;

    NDIS_HANDLE PacketPool;
    NDIS_HANDLE BufferPool;
    uint CountPacketPoolFailure;

    //
    // Parameters for Metric being used. 
    // 

    MetricType MetricType;
    MetricParams MetricParams;
    uint RouteFlapDampingFactor;   // Route flap damping factor. 0 means no damping.

    boolint (*IsInfinite)(uint LinkMetric);
    uint (*ConvMetric)(uint LinkMetric);
    void (*InitLinkMetric)(MiniportAdapter *VA,
                           int SNode, Link *Link, Time Now);
    uint (*PathMetric)(MiniportAdapter *VA, Link **Hops, uint NumHops);

    //
    // Counters for monitoring/debugging purposes.
    //
    uint CountXmit;
    uint CountXmitLocally;
    uint CountXmitQueueFull;
    uint CountRexmitQueueFull;
    uint CountXmitNoRoute;
    uint CountXmitMulticast;
    uint CountXmitRouteRequest;
    uint CountXmitRouteReply;
    uint CountXmitRouteError;
    uint CountXmitRouteErrorNoLink;
    uint CountXmitSendBuf;
    uint CountXmitMaintBuf;
    uint CountXmitForwardUnicast;
    uint CountXmitForwardQueueFull;
    uint CountXmitForwardBroadcast;
    uint CountXmitInfoRequest;
    uint CountXmitInfoReply;
    uint CountXmitProbe;
    uint CountXmitProbeReply;
    uint CountXmitLinkInfo;

    uint CountLinkInfoTooManyLinks;
    uint CountLinkInfoTooLarge;

    uint CountSalvageAttempt;
    uint CountSalvageStatic;
    uint CountSalvageOverflow;
    uint CountSalvageNoRoute;
    uint CountSalvageSameNextHop;
    uint CountSalvageTransmit;
    uint CountSalvageQueueFull;

    uint CountRecv;
    uint CountRecvLocally;
    uint CountRecvLocallySalvaged;
    uint CountRecvBadMAC;
    uint CountRecvRouteRequest;
    uint CountRecvRouteReply;
    uint CountRecvRouteError;
    uint CountRecvAckRequest;
    uint CountRecvAck;
    uint CountRecvSourceRoute;
    uint CountRecvInfoRequest;
    uint CountRecvInfoReply;
    uint CountRecvDupAckReq;
    uint CountRecvProbe;
    uint CountRecvProbeReply;
    uint CountRecvLinkInfo;
    uint CountRecvRecursive;
    uint CountRecvEmpty;
    uint CountRecvSmall;
    uint CountRecvDecryptFailure;

    uint MinVersionSeen;
    uint MaxVersionSeen;

    uint TotalForwardingStall;          // Microseconds.

    uint ForwardNum;
    uint ForwardMax;
    uint CountForwardFast;
    uint CountForwardQueue;
    uint CountForwardDrop;
};

extern MiniportAdapter *
FindVirtualAdapterFromIndex(uint Index);

extern MiniportAdapter *
FindVirtualAdapterFromGuid(const GUID *Guid);

extern boolint
MiniportInit(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);

extern void
MiniportReceivePacket(MiniportAdapter *VA, ProtocolAdapter *PA,
                      NDIS_PACKET *Original,
                      void (*FreePacket)(ProtocolAdapter *PA,
                                         NDIS_PACKET *Packet));

extern void
MiniportIndicateStatusConnected(MiniportAdapter *A);

extern void
MiniportIndicateStatusDisconnected(MiniportAdapter *A);

extern void
MiniportSendComplete(MiniportAdapter *VA, SRPacket *srp, NDIS_STATUS Status);

extern void 
MiniportSendRouteRequest(MiniportAdapter *VA,
                         const VirtualAddress Target, LQSRReqId Identifier);

extern void
MiniportSendViaRouteRequest(MiniportAdapter *VA,
                            SRPacket *SRP,
                            void (*Complete)(MiniportAdapter *VA,
                                             SRPacket *SRP,
                                             NDIS_STATUS Status));

extern void 
MiniportSendRouteError(MiniportAdapter *VA, const SRPacket *SRP);

extern void
MiniportSendInfoRequest(MiniportAdapter *VA);

extern void
MiniportResetStatistics(MiniportAdapter *VA);

extern void
MiniportRescheduleTimeout(MiniportAdapter *VA, Time Now, Time Timeout);

NDIS_STATUS
MiniportWriteMetricType(NDIS_HANDLE Config, MetricType MetricType);

NDIS_STATUS
MiniportReadMetricType(NDIS_HANDLE Config, MetricType *MetricType);

__inline void
MiniportAllocatePacket(
    MiniportAdapter *VA,
    NDIS_PACKET **pPacket,
    NDIS_STATUS *pStatus)
{
    NdisAllocatePacket(pStatus, pPacket, VA->PacketPool);
    if (*pStatus != NDIS_STATUS_SUCCESS)
        InterlockedIncrement((PLONG)&VA->CountPacketPoolFailure);
}

__inline NDIS_STATUS
MiniportClonePacket(
    MiniportAdapter *VA,
    NDIS_PACKET *OrigPacket,
    uint OrigHeaderLength, uint CloneHeaderLength,
    uint LookAhead,
    void **pOrigHeader,
    NDIS_PACKET **pClonePacket, void **pCloneHeader)
{
    NDIS_STATUS Status;

    Status = NdisClonePacket(OrigPacket, VA->PacketPool, VA->BufferPool,
                             OrigHeaderLength, CloneHeaderLength,
                             LookAhead, pOrigHeader,
                             pClonePacket, pCloneHeader);
    if (Status != NDIS_STATUS_SUCCESS)
        InterlockedIncrement((PLONG)&VA->CountPacketPoolFailure);
    return Status;
}

__inline NDIS_STATUS
MiniportMakeEmptyPacket(
    MiniportAdapter *VA, uint Size,
    NDIS_PACKET **pPacket, void **pData)
{
    NDIS_STATUS Status;

    Status = NdisMakeEmptyPacket(VA->PacketPool, VA->BufferPool,
                                 Size, pPacket, pData);
    if (Status != NDIS_STATUS_SUCCESS)
        InterlockedIncrement((PLONG)&VA->CountPacketPoolFailure);
    return Status;
}

extern boolint
MiniportIsInfinite(uint Metric);

extern uint
MiniportConvMetric(uint LinkMetric);

extern uint
MiniportPathMetric(MiniportAdapter *VA, Link **Hops, uint NumHops);

extern void 
MiniportInitLinkMetric(MiniportAdapter *VA, int SNode, Link *Link, Time Now);

extern NTSTATUS
MiniportPersistControl(MiniportAdapter *VA,
                       MCL_CONTROL_VIRTUAL_ADAPTER *Control);

extern void
MiniportReadControl(MiniportAdapter *VA,
                    MCL_CONTROL_VIRTUAL_ADAPTER *Control);

extern NTSTATUS
MiniportControl(MiniportAdapter *VA, MCL_CONTROL_VIRTUAL_ADAPTER *Control);

//
// Functionality in rtt.c
//
extern Time
RttSendProbes(MiniportAdapter *VA, Time Now);

extern void
RttSendProbeReply(MiniportAdapter *VA, InternalProbe *Probe);

extern void
RttReceiveProbeReply(MiniportAdapter *VA, InternalProbeReply *ProbeReply);


//
// Functionality in driver.c and io.c.
//

extern uint Version;

extern DEVICE_OBJECT *OurDeviceObject;

extern PDRIVER_DISPATCH IoMajorFunctions[IRP_MJ_MAXIMUM_FUNCTION + 1];

extern NTSTATUS
IoCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);

extern NTSTATUS
IoCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp);
extern NTSTATUS
IoClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);

extern NTSTATUS
IoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);

//
// Functionality in random.c.
//

extern int
RandomInit(void);

extern void
GetRandom(uchar *Buffer, uint Length);

extern uint
GetRandomNumber(uint Max);

extern NDIS_STATUS
GetSystemRandomBits(uchar *Buffer, uint Length);

//
// Functionality in reg.c.
//

typedef enum {
    OpenRegKeyRead,
    OpenRegKeyCreate,
    OpenRegKeyDeleting
} OpenRegKeyAction;

extern NTSTATUS
OpenRegKey(PHANDLE HandlePtr, HANDLE Parent, const WCHAR *KeyName,
           OpenRegKeyAction Action);

extern NTSTATUS
GetRegDWORDValue(HANDLE KeyHandle, const WCHAR *ValueName, PULONG ValueData);

extern NTSTATUS
SetRegDWORDValue(HANDLE KeyHandle, const WCHAR *ValueName,
                 IN ULONG ValueData);

extern NTSTATUS
GetRegNetworkAddress(HANDLE KeyHandle, const WCHAR *ValueName,
                     OUT VirtualAddress Address);

extern NTSTATUS
SetRegNetworkAddress(HANDLE KeyHandle, const WCHAR *ValueName,
                     IN VirtualAddress Address);

extern NTSTATUS
GetRegGuid(HANDLE KeyHandle, const WCHAR *ValueName,
           OUT GUID *Guid);

extern NTSTATUS
GetRegBinaryValue(HANDLE KeyHandle, const WCHAR *ValueName,
                  OUT uchar *Buffer, uint Length);

extern NTSTATUS
SetRegBinaryValue(HANDLE KeyHandle, const WCHAR *ValueName,
                  IN uchar *Buffer, uint Length);

//
// Define missing interlocked functions.
//

__inline LONGLONG
InterlockedRead64(LONGLONG volatile *Target)
{
#if defined(_M_IA64) || defined(_M_AMD64)
    return *Target;
#else
    LONGLONG Value = 0;
    return InterlockedCompareExchange64(Target, Value, Value);
#endif
}

__inline void
InterlockedWrite64(LONGLONG volatile *Target, LONGLONG Value)
{
#if defined(_M_IA64) || defined(_M_AMD64)
    *Target = Value;
#else
    LONGLONG OldValue = InterlockedRead64(Target);
    (void) InterlockedCompareExchange64(Target, Value, OldValue);
#endif
}

__inline void
InterlockedIncrementHighWater(LONG volatile *Count, LONG volatile *HighWater)
{
    LONG NewValue = InterlockedIncrement(Count);
    LONG OldHighWater;
    while ((OldHighWater = *HighWater) < NewValue) {
        if (InterlockedCompareExchange(HighWater, NewValue, OldHighWater) == NewValue)
            break;
    }
}
