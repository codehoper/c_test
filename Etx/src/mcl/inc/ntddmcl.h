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

//
// We need a definition of CTL_CODE for use below.
// When compiling kernel components in the DDK environment,
// ntddk.h supplies this definition. Otherwise get it
// from devioctl.h in the SDK environment.
//
#ifndef CTL_CODE
#include <devioctl.h>
#endif

//
// This string is the name of the MCL device.
// It is passed to NtCreateFile to access the device.
//
#define DD_MCL_DEVICE_NAME              L"\\Device\\MCL"

//
// The Windows-accessible device name.  It is the name that
// (prepended with "\\\\.\\") should be passed to CreateFile.
//
#define WIN_MCL_BASE_DEVICE_NAME        L"MCL"
#define WIN_MCL_DEVICE_NAME             L"\\\\.\\" WIN_MCL_BASE_DEVICE_NAME

//
// MCL IOCTL code definitions.
//
// The codes that use FILE_ANY_ACCESS are open to all users.
// The codes that use FILE_WRITE_ACCESS require local Administrator privs.
//

#define FSCTL_MCL_BASE FILE_DEVICE_NETWORK

#define _MCL_CTL_CODE(function, method, access) \
            CTL_CODE(FSCTL_MCL_BASE, function, method, access)


//
// This IOCTL retrieves information about a virtual adapter,
// given an adapter index or guid.
// It takes as input an MCL_QUERY_VIRTUAL_ADAPTER structure
// and returns as output an MCL_INFO_VIRTUAL_ADAPTER structure.
// To perform an iteration, start with Index set to -1, in which case
// only an MCL_QUERY_VIRTUAL_ADAPTER is returned, for the first adapter.
// If there are no more adapters, then the Index in the returned
// MCL_QUERY_VIRTUAL_ADAPTER will be -1.
//
#define IOCTL_MCL_QUERY_VIRTUAL_ADAPTER \
            _MCL_CTL_CODE(0, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_query_virtual_adapter {
    unsigned int Index;         // -1 means start/finish iteration,
                                // 0 means use the Guid.
    GUID Guid;
} MCL_QUERY_VIRTUAL_ADAPTER;

typedef struct mcl_control_mup {
    uint Alpha; 
    uint ProbePeriod;
    uint HysteresisPeriod;
    uint PenaltyFactor;
    uint SweepPeriod;
    boolint Random;
    boolint OutIfOverride;
} MCL_INFO_RTT; 

typedef struct mcl_control_pktpair {
    uint Alpha; 
    uint ProbePeriod;
    uint PenaltyFactor;
} MCL_INFO_PKTPAIR; 

typedef struct mcl_control_etx {
    uint ProbePeriod;
    uint LossInterval;
    uint Alpha; 
    uint PenaltyFactor;
} MCL_INFO_ETX; 

typedef struct mcl_control_wcett {
    uint ProbePeriod;
    uint LossInterval;
    uint Alpha; 
    uint PenaltyFactor;
    uint Beta;
    uint PktPairProbePeriod;
    uint PktPairMinOverProbes;
} MCL_INFO_WCETT;

typedef union mcl_info_MetricParams {
    MCL_INFO_RTT Rtt;
    MCL_INFO_PKTPAIR PktPair; 
    MCL_INFO_ETX Etx; 
    MCL_INFO_WCETT Wcett;
} MCL_INFO_METRIC_PARAMS;

#define LQSR_KEY_SIZE   16

typedef struct mcl_info_virtual_adapter {
    MCL_QUERY_VIRTUAL_ADAPTER Next;
    MCL_QUERY_VIRTUAL_ADAPTER This;

    uint Version;
    uint MinVersionSeen;
    uint MaxVersionSeen;

    VirtualAddress Address;
    uint LookAhead;
    boolint Snooping;
    boolint ArtificialDrop;

    boolint Crypto;
    uchar CryptoKeyMAC[LQSR_KEY_SIZE];
    uchar CryptoKeyAES[LQSR_KEY_SIZE];

    uint LinkTimeout;

    uint SendBufSize;
    uint SendBufHighWater;
    uint SendBufMaxSize;

    Time ReqTableMinElementReuse;
    Time ReqTableMinSuppressReuse;

    uint TotalForwardingStall;

    uint ForwardNum;
    uint ForwardMax;
    uint CountForwardFast;
    uint CountForwardQueue;
    uint CountForwardDrop;

    uint MetricType; 
    MCL_INFO_METRIC_PARAMS MetricParams; 
    uint RouteFlapDampingFactor;

    uint CountPacketPoolFailure;

    uint TimeoutMinLoops;
    uint TimeoutMaxLoops;

    uint PbackNumber;
    uint PbackHighWater;
    Time PbackAckMaxDupTime;

    uint SmallestMetric;
    uint LargestMetric;
    uint CountAddLinkInvalidate;
    uint CountAddLinkInsignificant;
    uint CountRouteFlap;
    uint CountRouteFlapDamp;

    uint MaintBufNumPackets;
    uint MaintBufHighWater;

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
} MCL_INFO_VIRTUAL_ADAPTER;


//
// This IOCTL retrieves information about a physical adapter,
// given an adapter index or guid.
// It takes as input an MCL_QUERY_PHYSICAL_ADAPTER structure
// and returns as output an MCL_INFO_PHYSICAL_ADAPTER structure.
// To perform an iteration, start with Index set to -1, in which case
// only an MCL_QUERY_PHYSICAL_ADAPTER is returned, for the first adapter.
// If there are no more adapters, then the Index in the returned
// MCL_QUERY_PHYSICAL_ADAPTER will be -1.
//
#define IOCTL_MCL_QUERY_PHYSICAL_ADAPTER \
            _MCL_CTL_CODE(1, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_query_physical_adapter {
    MCL_QUERY_VIRTUAL_ADAPTER VA;      // Bound to this virtual adapter.
    unsigned int Index;         // -1 means start/finish iteration,
                                // 0 means use the Guid.
    GUID Guid;
} MCL_QUERY_PHYSICAL_ADAPTER;

typedef enum {
    MCL_PHYSICAL_ADAPTER_ETHERNET,
    MCL_PHYSICAL_ADAPTER_802_11,
} MCL_PHYSICAL_ADAPTER_TYPE;

typedef struct mcl_info_physical_adapter {
    MCL_QUERY_PHYSICAL_ADAPTER Next;
    MCL_QUERY_PHYSICAL_ADAPTER This;

    PhysicalAddress Address;
    uint MTU;
    boolint Promiscuous;
    boolint ReceiveOnly;
    uint Channel;
    uint Bandwidth;

    uint PacketsSent;
    uint PacketsReceived;
    uint PacketsReceivedTD;
    uint PacketsReceivedFlat;

    uint CountPacketPoolFailure;

    uint CountRecvOutstanding;
    uint MaxRecvOutstanding;
    uint CountSentOutstanding;
    uint MaxSentOutstanding;

    MCL_PHYSICAL_ADAPTER_TYPE Type;

    union {
        struct {
            // Mandatory.
            NDIS_802_11_MAC_ADDRESS BSSID;
            NDIS_802_11_SSID SSID;
            NDIS_802_11_RSSI RSSI;
            NDIS_802_11_NETWORK_INFRASTRUCTURE Mode;
            NDIS_802_11_CONFIGURATION Radio;
            // Optional.
            NDIS_802_11_POWER_MODE PowerMode;
            NDIS_802_11_TX_POWER_LEVEL TxPowerLevel;
            NDIS_802_11_RTS_THRESHOLD RTSThreshold;
            NDIS_802_11_STATISTICS Statistics;
        } W;
    };
} MCL_INFO_PHYSICAL_ADAPTER;


//
// This IOCTL retrieves information from the neighbor cache.
// It takes as input an MCL_QUERY_NEIGHBOR_CACHE structure
// and returns as output an MCL_INFO_NEIGHBOR_CACHE structure.
// To perform an iteration, start with Address set to zero, in which case
// only an MCL_QUERY_NEIGHBOR_CACHE is returned, for the first address.
// If there are no more addresses, then the Address in the returned
// MCL_QUERY_NEIGHBOR_CACHE will be zero.
//
#define IOCTL_MCL_QUERY_NEIGHBOR_CACHE \
            _MCL_CTL_CODE(2, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_query_neighbor_cache {
    MCL_QUERY_VIRTUAL_ADAPTER VA;  // Fields that identify an adapter.
    VirtualAddress Address;
    uint InIF;
} MCL_QUERY_NEIGHBOR_CACHE;

typedef struct mcl_info_neighbor_cache {
    MCL_QUERY_NEIGHBOR_CACHE Query;

    PhysicalAddress Address;
} MCL_INFO_NEIGHBOR_CACHE;


//
// This IOCTL flushes entries from the neighbor cache.
// It uses the MCL_QUERY_NEIGHBOR_CACHE structure.
// If the Address is zero, then it flushes all entries
// for the virtual adapter.
//
#define IOCTL_MCL_FLUSH_NEIGHBOR_CACHE \
            _MCL_CTL_CODE(3, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// This IOCTL retrieves information from the link cache.
// The IOCTL retrieves a variable-length array of links
// from the specified node.
//
#define IOCTL_MCL_QUERY_CACHE_NODE \
            _MCL_CTL_CODE(4, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_query_cache_node {
    MCL_QUERY_VIRTUAL_ADAPTER VA;  // Fields that identify an adapter.
    VirtualAddress Address;
} MCL_QUERY_CACHE_NODE;

typedef struct mcl_info_metric_info_mup {
    uint SentProbes;
    uint LostProbes;
    uint LateProbes;
    uint RawMetric;
    uint LastRTT;
} MCL_INFO_METRIC_INFO_RTT; 

// 
// If you change this, remember to change 
// MAX_PKTPAIR_HISTORY in linkcache.h
//
#define MCL_INFO_MAX_PKTPAIR_HISTORY 30 

typedef struct mcl_info_MinHistory {
    uint Min; 
    uint Seq;
} mcl_info_MinHistory; 

typedef struct mcl_info_metric_info_pktpair {
    uint PairsSent;
    uint RepliesSent;
    uint RepliesRcvd;
    uint LostPairs;
    uint RTT;
    uint LastRTT;
    uint LastPktPair;
    uint CurrMin;           // Current min.
    uint NumSamples;        // Total number of samples so far.
    mcl_info_MinHistory MinHistory[MCL_INFO_MAX_PKTPAIR_HISTORY];  // History.
} MCL_INFO_METRIC_INFO_PKTPAIR; 

typedef struct mcl_info_metric_info_etx {
    uint TotSentProbes;
    uint TotRcvdProbes;
    uint FwdDeliv;        
    uint ProbeHistorySZ;
} MCL_INFO_METRIC_INFO_ETX; 

typedef struct mcl_info_metric_info_wcett {
    uint TotSentProbes;
    uint TotRcvdProbes;
    uint FwdDeliv;        
    uint ProbeHistorySZ;
    uint LastProb;
    uint PairsSent;
    uint RepliesSent;
    uint RepliesRcvd;
    uint LastPktPair;
    uint CurrMin;           // Current min.
    uint NumPktPairValid;
    uint NumPktPairInvalid;
} MCL_INFO_METRIC_INFO_WCETT;

typedef union mcl_info_metric {
    MCL_INFO_METRIC_INFO_RTT Rtt;
    MCL_INFO_METRIC_INFO_PKTPAIR PktPair;
    MCL_INFO_METRIC_INFO_ETX Etx;
    MCL_INFO_METRIC_INFO_WCETT Wcett;
} MCL_INFO_METRIC_INFO;

typedef struct mcl_info_link {
    VirtualAddress To;
    uint FromIF;
    uint ToIF;
    uint Metric;
    Time TimeStamp;
    uint Usage;
    uint Failures;
    uint DropRatio;
    uint ArtificialDrops;
    uint QueueDrops;
    MCL_INFO_METRIC_INFO MetricInfo; 
} MCL_INFO_LINK;

typedef struct mcl_info_cache_node {
    MCL_QUERY_CACHE_NODE Query;

    uint DijkstraMetric;
    uint CachedMetric;
    uint Hops;
    uint Previous;
    uint RouteChanges;

    uint LinkCount;
    MCL_INFO_LINK Links[];
} MCL_INFO_CACHE_NODE;


//
// This IOCTL adds a link to the link cache.
//
#define IOCTL_MCL_ADD_LINK \
            _MCL_CTL_CODE(5, METHOD_BUFFERED, FILE_WRITE_ACCESS)

typedef struct mcl_add_link {
    MCL_QUERY_CACHE_NODE Node;
    VirtualAddress To;
    uint FromIF;
    uint ToIF;
} MCL_ADD_LINK;


//
// This IOCTL flushes the link cache.
// It uses the MCL_QUERY_VIRTUAL_ADAPTER structure.
//
#define IOCTL_MCL_FLUSH_LINK_CACHE \
            _MCL_CTL_CODE(6, METHOD_BUFFERED, FILE_WRITE_ACCESS)


//
// This IOCTL retrieves information from the link cache.
// The IOCTL takes as input an MCL_QUERY_CACHE_NODE.
// The IOCTL retrieves a variable-length array of hops
// to reach the specified node.
//
#define IOCTL_MCL_QUERY_SOURCE_ROUTE \
            _MCL_CTL_CODE(7, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_info_source_route_hop {
    VirtualAddress Address;
    uint InIF;
    uint OutIF;
    uint Metric;
} MCL_INFO_SOURCE_ROUTE_HOP;

typedef struct mcl_info_source_route {
    MCL_QUERY_CACHE_NODE Query;

    boolint Static;
    uint NumHops;
    MCL_INFO_SOURCE_ROUTE_HOP Hops[];
} MCL_INFO_SOURCE_ROUTE;


//
// This IOCTL retrieves information from the link cache.
// The IOCTL takes as input an MCL_QUERY_VIRTUAL_ADAPTER.
// It returns the MCL_INFO_LINK_CACHE structure.
//
#define IOCTL_MCL_QUERY_LINK_CACHE \
            _MCL_CTL_CODE(8, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_info_link_cache {
    uint NumNodes;
    uint MaxNodes;
    Time Timeout;
} MCL_INFO_LINK_CACHE;


//
// This IOCTL retrieves information from the maintenance buffer.
// The IOCTL takes as input an MCL_QUERY_MAINTENANCE_BUFFER_NODE.
// It returns the MCL_INFO_MAINTENANCE_BUFFER_NODE structure.
// To perform an iteration, start with Node.Address set to zero, in which case
// only an MCL_QUERY_MAINTENANCE_BUFFER_NODE is returned, for the first node.
// If there are no more nodes, then the Node.Address in the returned
// MCL_QUERY_MAINTENANCE_BUFFER_NODE will be zero.
//
#define IOCTL_MCL_QUERY_MAINTENANCE_BUFFER \
            _MCL_CTL_CODE(9, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_query_maintenance_buffer_node {
    MCL_QUERY_VIRTUAL_ADAPTER VA;
    MCL_INFO_SOURCE_ROUTE_HOP Node;
} MCL_QUERY_MAINTENANCE_BUFFER_NODE;

typedef struct mcl_info_maintenance_buffer_node {
    MCL_QUERY_MAINTENANCE_BUFFER_NODE Query;

    ushort NextAckNum;
    ushort LastAckNum;
    Time LastAckRcv;
    Time FirstAckReq;
    Time LastAckReq;

    uint NumPackets;
    uint HighWater;

    uint NumAckReqs;
    uint NumFastReqs;
    uint NumValidAcks;
    uint NumInvalidAcks;
} MCL_INFO_MAINTENANCE_BUFFER_NODE;


//
// This IOCTL causes MCL to send an Information Query.
// The IOCTL takes as input an MCL_QUERY_VIRTUAL_ADAPTER.
//
#define IOCTL_MCL_INFORMATION_REQUEST \
            _MCL_CTL_CODE(10, METHOD_BUFFERED, FILE_WRITE_ACCESS)


//
// This IOCTL causes MCL to reset its statistics gathering.
// The IOCTL takes as input an MCL_QUERY_VIRTUAL_ADAPTER.
//
#define IOCTL_MCL_RESET_STATISTICS \
            _MCL_CTL_CODE(11, METHOD_BUFFERED, FILE_WRITE_ACCESS)


//
// This IOCTL retrieves the link cache change log.
// The IOCTL takes as input an MCL_QUERY_LINK_CACHE_CHANGE_LOG
// and returns an MCL_INFO_LINK_CACHE_CHANGE_LOG.
//
// A Query Index of -1 returns just the current Index.
//
#define IOCTL_MCL_QUERY_LINK_CACHE_CHANGE_LOG \
            _MCL_CTL_CODE(12, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_query_link_cache_change_log {
    MCL_QUERY_VIRTUAL_ADAPTER VA;
    uint Index;
} MCL_QUERY_LINK_CACHE_CHANGE_LOG;

#define LINK_STATE_CHANGE_DELETE_TIMEOUT        0
#define LINK_STATE_CHANGE_DELETE_MANUAL         1
#define LINK_STATE_CHANGE_DELETE_INTERFACE      4

#define LINK_STATE_CHANGE_ERROR                 2
#define LINK_STATE_CHANGE_SNOOP_ERROR           3

#define LINK_STATE_CHANGE_PENALIZED             5

#define LINK_STATE_CHANGE_ADD_MANUAL            64
#define LINK_STATE_CHANGE_ADD_REPLY             65
#define LINK_STATE_CHANGE_ADD_SNOOP_REPLY       66
#define LINK_STATE_CHANGE_ADD_SNOOP_SR          67
#define LINK_STATE_CHANGE_ADD_SNOOP_REQUEST     68
#define LINK_STATE_CHANGE_ADD_PROBE             69
#define LINK_STATE_CHANGE_ADD_LINKINFO          70

typedef struct mcl_info_link_cache_change {
    Time TimeStamp;
    VirtualAddress From;
    VirtualAddress To;
    uint FromIF;
    uint ToIF;
    uint Metric;
    uint Reason;
} MCL_INFO_LINK_CACHE_CHANGE;

typedef struct mcl_info_link_cache_change_log {
    uint NextIndex;
    MCL_INFO_LINK_CACHE_CHANGE Changes[];
} MCL_INFO_LINK_CACHE_CHANGE_LOG;

//
// This IOCTL controls the attributes of a virtual adapter.
// The IOCTL takes as input an MCL_CONTROL_VIRTUAL_ADAPTER.
//
#define IOCTL_MCL_CONTROL_VIRTUAL_ADAPTER \
            _MCL_CTL_CODE(13, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// 
// Maximum value of Alpha. Used as denominator to calculate real Alpha.
// RTT control parameter values are set using 
// IOCTL_MCL_CONTROL_VIRTUAL_ADAPTER.
//
#define MAXALPHA 10

typedef struct mcl_control_virtual_adapter {
    MCL_QUERY_VIRTUAL_ADAPTER This;
    boolint Snooping;        // -1 means no change.
    boolint ArtificialDrop;  // -1 means no change.
    uint RouteFlapDampingFactor;// -1 means no change.
    boolint Crypto;          // -1 means no change.
    uchar CryptoKeyMAC[LQSR_KEY_SIZE];  // 0 means no change.
    uchar CryptoKeyAES[LQSR_KEY_SIZE];  // 0 means no change.
    uint LinkTimeout;        // Seconds. -1 means no change, 0 means none.
    uint MetricType;         // -1 means no change.
    MCL_INFO_METRIC_PARAMS MetricParams; 
} MCL_CONTROL_VIRTUAL_ADAPTER;

//
// Initialize the fields of the MCL_CONTROL_VIRTUAL_ADAPTER structure
// to values that indicate no change.
//
__inline void
MCL_INIT_CONTROL_RTT(MCL_CONTROL_VIRTUAL_ADAPTER *Control)
{
    Control->MetricParams.Rtt.Alpha = (uint)-1;
    Control->MetricParams.Rtt.ProbePeriod = (uint)-1;
    Control->MetricParams.Rtt.HysteresisPeriod = (uint)-1;
    Control->MetricParams.Rtt.PenaltyFactor = (uint)-1;
    Control->MetricParams.Rtt.SweepPeriod = (uint)-1;
    Control->MetricParams.Rtt.Random = (uint)-1;
    Control->MetricParams.Rtt.OutIfOverride = (uint)-1;
};

__inline void
MCL_INIT_CONTROL_PKTPAIR(MCL_CONTROL_VIRTUAL_ADAPTER *Control)
{
    Control->MetricParams.PktPair.Alpha = (uint)-1;
    Control->MetricParams.PktPair.ProbePeriod = (uint)-1;
    Control->MetricParams.PktPair.PenaltyFactor = (uint)-1;
};

__inline void
MCL_INIT_CONTROL_ETX(MCL_CONTROL_VIRTUAL_ADAPTER *Control)
{
    Control->MetricParams.Etx.ProbePeriod = (uint)-1;
    Control->MetricParams.Etx.LossInterval = (uint)-1;
    Control->MetricParams.Etx.Alpha = (uint)-1; 
    Control->MetricParams.Etx.PenaltyFactor = (uint)-1;
};

__inline void
MCL_INIT_CONTROL_WCETT(MCL_CONTROL_VIRTUAL_ADAPTER *Control)
{
    Control->MetricParams.Wcett.ProbePeriod = (uint)-1;
    Control->MetricParams.Wcett.LossInterval = (uint)-1;
    Control->MetricParams.Wcett.Alpha = (uint)-1; 
    Control->MetricParams.Wcett.PenaltyFactor = (uint)-1;
    Control->MetricParams.Wcett.Beta = (uint)-1;
    Control->MetricParams.Wcett.PktPairProbePeriod = (uint)-1;
    Control->MetricParams.Wcett.PktPairMinOverProbes = (uint)-1;
};

__inline void
MCL_INIT_CONTROL_VIRTUAL_ADAPTER(MCL_CONTROL_VIRTUAL_ADAPTER *Control)
{
    Control->Snooping = (boolint)-1;
    Control->ArtificialDrop = (boolint)-1;
    Control->RouteFlapDampingFactor = (uint)-1;
    Control->Crypto = (uint)-1;
    memset(Control->CryptoKeyMAC, 0, sizeof Control->CryptoKeyMAC);
    memset(Control->CryptoKeyAES, 0, sizeof Control->CryptoKeyAES);
    Control->LinkTimeout = (uint)-1;
    Control->MetricType = (uint)-1;
};

__inline boolint
LQSR_KEY_PRESENT(uchar Key[LQSR_KEY_SIZE])
{
    return *(uint UNALIGNED *)Key != 0;
}

//
// This IOCTL controls the attributes of a physical adapter.
// The IOCTL takes as input an MCL_CONTROL_PHYSICAL_ADAPTER.
//
#define IOCTL_MCL_CONTROL_PHYSICAL_ADAPTER \
            _MCL_CTL_CODE(14, METHOD_BUFFERED, FILE_WRITE_ACCESS)

typedef struct mcl_control_physical_adapter {
    MCL_QUERY_PHYSICAL_ADAPTER This;
    boolint ReceiveOnly;        // -1 means no change.
    uint Channel;               // 0 means no change.
    uint Bandwidth;             // 0 means no change.
} MCL_CONTROL_PHYSICAL_ADAPTER;

//
// Initialize the fields of the MCL_CONTROL_PHYSICAL_ADAPTER structure
// to values that indicate no change.
//
__inline void
MCL_INIT_CONTROL_PHYSICAL_ADAPTER(MCL_CONTROL_PHYSICAL_ADAPTER *Control)
{
    Control->ReceiveOnly = (boolint)-1;
    Control->Channel = 0;
    Control->Bandwidth = 0;
}

//
// This IOCTL stores a given static route.
// The IOCTL takes as input an MCL_INFO_SOURCE_ROUTE structure.
//
#define IOCTL_MCL_ADD_SOURCE_ROUTE \
            _MCL_CTL_CODE(15, METHOD_BUFFERED, FILE_WRITE_ACCESS)


//
// This IOCTL retrieves the route cache change log.
// The IOCTL takes as input an MCL_QUERY_ROUTE_CACHE_CHANGE_LOG
// and returns an MCL_INFO_ROUTE_CACHE_CHANGE_LOG.
//
// A Query Index of -1 returns just the current Index.
//
#define IOCTL_MCL_QUERY_ROUTE_CACHE_CHANGE_LOG \
            _MCL_CTL_CODE(16, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_query_route_cache_change_log {
    MCL_QUERY_VIRTUAL_ADAPTER VA;
    uint Index;
} MCL_QUERY_ROUTE_CACHE_CHANGE_LOG;

typedef struct mcl_info_route_cache_change {
    Time TimeStamp;
    VirtualAddress Dest;
    uint Metric;
    uint PrevMetric;
    uchar Buffer[128]; // Must be large enough for a SourceRoute option.
} MCL_INFO_ROUTE_CACHE_CHANGE;

typedef struct mcl_info_route_cache_change_log {
    uint NextIndex;
    MCL_INFO_ROUTE_CACHE_CHANGE Changes[];
} MCL_INFO_ROUTE_CACHE_CHANGE_LOG;


//
// This IOCTL retrieves route usage information from the link cache.
// The IOCTL retrieves a variable-length array of variable-sized
// route usage records, given an MCL_QUERY_CACHE_NODE.
//
#define IOCTL_MCL_QUERY_ROUTE_USAGE \
            _MCL_CTL_CODE(17, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct mcl_info_route_usage {
    uint Usage;
    uint NumHops;
    MCL_INFO_SOURCE_ROUTE_HOP Hops[];
} MCL_INFO_ROUTE_USAGE;


//
// This IOCTL controls the attributes of a link.
// The IOCTL takes as input an MCL_CONTROL_LINK.
//
#define IOCTL_MCL_CONTROL_LINK \
            _MCL_CTL_CODE(18, METHOD_BUFFERED, FILE_WRITE_ACCESS)

typedef struct mcl_control_link {
    MCL_QUERY_CACHE_NODE Node;
    VirtualAddress To;
    uint FromIF;
    uint ToIF;
    uint DropRatio;
} MCL_CONTROL_LINK;

//
// This IOCTL controls the attributes of a virtual adapter
// by updating the registry.
// The IOCTL takes as input an MCL_CONTROL_VIRTUAL_ADAPTER,
// but some attributes can only be controlled persistently
// and take effect after restarting the driver.
//
#define IOCTL_MCL_PERSISTENT_CONTROL_VIRTUAL_ADAPTER \
            _MCL_CTL_CODE(19, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// This IOCTL returns strong random bits, suitable for MCL crypto keys.
//
#define IOCTL_MCL_GENERATE_RANDOM \
            _MCL_CTL_CODE(20, METHOD_BUFFERED, FILE_ANY_ACCESS)
