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
#include <stdio.h>
#include <tchar.h>
#include <shellapi.h>
#include <objbase.h>
#include <shlobj.h>
#include <wtypes.h>

//
// This is the GUID for the network connections folder. It is constant.
// {7007ACC7-3202-11D1-AAD2-00805FC1270E}
//
const GUID CLSID_NetworkConnections = {
    0x7007ACC7, 0x3202, 0x11D1,
    { 0xAA, 0xD2, 0x00, 0x80, 0x5F, 0xC1, 0x27, 0x0E }
};

//
// NB: This function leaks memory/references.
//
EXTERN_C void
RenameAdapter(const GUID *AdapterGuid, const WCHAR *NewName)
{
    HRESULT hr;

    IShellFolder *pShellFolder;
    hr = CoCreateInstance(CLSID_NetworkConnections, NULL,
                          CLSCTX_INPROC_SERVER,
                          IID_IShellFolder,
                          reinterpret_cast<LPVOID *>(&pShellFolder));
    if (FAILED(hr)) {
        fprintf(stderr, "mcl: RenameAdapter: CoCreateInstance: %x\n", hr);
        return;
    }

    LPOLESTR szClsId;
    hr = StringFromCLSID(*AdapterGuid, &szClsId);
    if (FAILED(hr)) {
        fprintf(stderr, "mcl: RenameAdapter: StringFromCLSID: %x\n", hr);
        return;
    }

    WCHAR szAdapterGuid[MAX_PATH];
    swprintf(szAdapterGuid, L"::%s", szClsId);

    LPITEMIDLIST pidl;
    hr = pShellFolder->ParseDisplayName(NULL, NULL,
                                        szAdapterGuid, NULL,
                                        &pidl, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "mcl: RenameAdapter: ParseDisplayName: %x\n", hr);
        return;
    }

    hr = pShellFolder->SetNameOf(NULL, pidl, NewName, SHGDN_NORMAL, &pidl);
    if (FAILED(hr)) {
        fprintf(stderr, "mcl: RenameAdapter: SetNameOf: %x\n", hr);
        return;
    }
}
