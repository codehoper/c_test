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

#define NUM_DUPLICATE_SUPPRESS  64

typedef struct duplicateSuppress_t {
    VirtualAddress Target;
    LQSRReqId Id;
    Time LastUsed;              // For statistics gathering.
} DuplicateSuppress;

typedef struct requestTableElement_t {
    VirtualAddress Addr;
    Time LastUsed;              // For statistics gathering.

    //
    // These fields are used when sending Route Requests for a Target.
    //
    Time LastReq;
    uint Backoff;               // How many Requests since last Reply.
    LQSRReqId NextID;

    //
    // These fields are used when suppressing Route Requests from a Source.
    //
    uint Victim;
    DuplicateSuppress Suppress[NUM_DUPLICATE_SUPPRESS];
} RequestTableElement;

typedef struct forwardedRequestTable_t {
    KSPIN_LOCK Lock;
    uint Victim;
    uint MaxSize;
    uint CurSize;
    RequestTableElement *RTE;

    Time MinElementReuse;
    Time MinSuppressReuse;
} ForwardedRequestTable;

extern NDIS_STATUS
ReqTableNew(MiniportAdapter *VA, uint Size);

extern void 
ReqTableFree(MiniportAdapter *VA);

extern boolint
ReqTableSuppress(MiniportAdapter *VA,
                 const VirtualAddress Source,
                 const VirtualAddress Target,
                 LQSRReqId Identifier);

extern LQSRReqId
ReqTableIdentifier(MiniportAdapter *VA,
                   const VirtualAddress Target);

extern boolint
ReqTableSendP(MiniportAdapter *VA, const VirtualAddress Target,
              LQSRReqId *Identifier);

extern void
ReqTableReceivedReply(MiniportAdapter *VA, const VirtualAddress Target);

extern void
ReqTableResetStatistics(MiniportAdapter *VA);
