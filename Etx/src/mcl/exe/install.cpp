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
#include <strsafe.h>
#include <setupapi.h>
#include <netcfgx.h>
#include <devguid.h>
#include "types.h"

EXTERN_C void ausage(void);
EXTERN_C void RenameAdapter(const GUID *AdapterGuid, const WCHAR *NewName);

HRESULT
HrCreateINetCfg(
    IN boolint fAcquireWriteLock,
    OUT INetCfg** ppINetCfg)
{
    HRESULT hr;
    INetCfg* pINetCfg;

    //
    // Get the INetCfg interface.
    //
    hr = CoCreateInstance(
        CLSID_CNetCfg,
        NULL,
        CLSCTX_INPROC_SERVER | CLSCTX_NO_CODE_DOWNLOAD,
        IID_INetCfg,
        reinterpret_cast<void**>(&pINetCfg));
    if (! SUCCEEDED(hr))
        return hr;

    INetCfgLock * pnclock = NULL;

    if (fAcquireWriteLock) {
        //
        // Get the locking interface.
        //
        hr = pINetCfg->QueryInterface(IID_INetCfgLock,
                                 reinterpret_cast<LPVOID *>(&pnclock));
        if (SUCCEEDED(hr)) {
            LPWSTR pwszLockHolder;
            //
            // Attempt to lock the INetCfg for read/write.
            //
            hr = pnclock->AcquireWriteLock(100, L"InstallMCL",
                &pwszLockHolder);
            if (hr == S_FALSE) {
                //
                // Could not acquire the lock.
                //
                hr = NETCFG_E_NO_WRITE_LOCK;
                printf("The write lock could not be acquired.\n");
                printf("You must close %ls first.\n", pwszLockHolder);

            }
            if (pwszLockHolder != NULL)
                CoTaskMemFree(pwszLockHolder);
        }
    }

    if (SUCCEEDED(hr)) {
        hr = pINetCfg->Initialize(NULL);
        if (SUCCEEDED(hr)) {
            //
            // Add a reference for our caller.
            //
            pINetCfg->AddRef();
            *ppINetCfg = pINetCfg;
        }
        else {
            if (pnclock != NULL)
                pnclock->ReleaseWriteLock();
        }
    }

    if (pnclock != NULL)
        pnclock->Release();

    pINetCfg->Release();
    return S_OK;
}

void
AddOrRemoveComponent(
    INetCfg *pINetCfg,
    const GUID *Guid,
    const WCHAR *Name,
    const WCHAR *ConnectionName,
    boolint AddOrRemove)
{
    INetCfgClassSetup *pSetup;
    HRESULT hr;

    //
    // Get the setup interface used for installing
    // and uninstalling components.
    //
    hr = pINetCfg->QueryNetCfgClass(
        Guid,
        IID_INetCfgClassSetup,
        (void**)&pSetup);
    if (! SUCCEEDED(hr)) {
        fprintf(stderr, "mcl: Could not get setup interface.\n");
        exit(1);
    }

    OBO_TOKEN OboToken;
    INetCfgComponent *pIComp;

    ZeroMemory(&OboToken, sizeof OboToken);
    OboToken.Type = OBO_USER;

    if (AddOrRemove) {
        printf("Installing %ls...\n", Name);

        hr = pSetup->Install(
                Name,
                &OboToken,
                0, 0, NULL, NULL,
                &pIComp);

        if (pIComp != NULL) {
            GUID AdapterGuid;

            if ((ConnectionName != NULL) &&
                SUCCEEDED(pIComp->GetInstanceGuid(&AdapterGuid)))
                RenameAdapter(&AdapterGuid, ConnectionName);
            pIComp->Release();
        }
    }
    else {
        //
        // Need to remove the component.
        // Find it first.
        //
        hr = pINetCfg->FindComponent(Name, &pIComp);

        if (SUCCEEDED(hr)) {
            printf("Uninstalling %ls...\n", Name);

            hr = pSetup->DeInstall(pIComp, &OboToken, NULL);

            pIComp->Release();
        }
        else {
            printf("%ls is not installed.\n", Name);
        }
    }

    if (SUCCEEDED(hr)) {
        if (hr == NETCFG_S_REBOOT) {
            hr = S_OK;
            printf("A reboot is required to complete this action.\n");
        }
        else {
            printf("Succeeded.\n");
        }
    }
    else {
        printf("Failed to complete the action.\n");

        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
            hr = S_OK;
            printf("The INF file for MCL could not be found.\n");
        }
        else if (hr == NETCFG_E_NEED_REBOOT) {
            printf("A reboot is required before any further changes can be made.\n");
        }
        else {
            printf("Error %x\n", hr);
        }
    }

    pSetup->Release();
}

static void
InternalAddOrRemove(boolint AddOrRemove)
{
    INetCfg *pINetCfg;
    HRESULT hr;

    hr = HrCreateINetCfg(TRUE, &pINetCfg);
    if (! SUCCEEDED(hr)) {
        if (hr == NETCFG_E_NO_WRITE_LOCK) {
            fprintf(stderr, "mcl: Could not acquire config write lock\n");
            exit(1);
        }
        else if (hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
            ausage();
        }
        else {
            fprintf(stderr, "mcl: HrCreateINetCfg -> %x\n", hr);
            exit(1);
        }
    }

    AddOrRemoveComponent(pINetCfg, &GUID_DEVCLASS_NET,
                         L"MS_MCLMP", L"Mesh Virtual Adapter", AddOrRemove);
    AddOrRemoveComponent(pINetCfg, &GUID_DEVCLASS_NETTRANS,
                         L"MS_MCLTP", NULL, AddOrRemove);
    pINetCfg->Release();
}

static void
InstallInf(const char *SetupKitDir, const char *InfName)
{
    char InfFile[MAX_PATH];
    BOOL ok;

    if (FAILED(StringCchCopyA(InfFile, MAX_PATH, SetupKitDir)) ||
        FAILED(StringCchCatA(InfFile, MAX_PATH, "\\")) ||
        FAILED(StringCchCatA(InfFile, MAX_PATH, InfName))) {

        fprintf(stderr, "mcl: directory name too long\n");
        exit(1);
    }

    ok = SetupCopyOEMInfA(InfFile,
                          SetupKitDir,
                          SPOST_PATH,
                          0, // CopyStyle
                          NULL, 0, NULL, NULL);
    if (! ok) {
        fprintf(stderr, "mcl: SetupCopyOEMInfA(%s) -> %d\n",
                InfFile, GetLastError());
        exit(1);
    }

}

EXTERN_C void
AddOrRemoveMCL(boolint AddOrRemove, const char *SetupKitDir)
{
    HRESULT hr = S_OK;
    boolint fInitCom = TRUE;

    if (SetupKitDir != NULL) {
        InstallInf(SetupKitDir, "mclmp.inf");
        InstallInf(SetupKitDir, "mcltp.inf");
    }

    //
    // Initialize COM.
    //
    hr = CoInitializeEx(NULL,
                        COINIT_DISABLE_OLE1DDE | COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        //
        // If we changed mode, then we won't uninitialize COM when we are done.
        //
        hr = S_OK;
        fInitCom = FALSE;
    }

    if (! SUCCEEDED(hr)) {
        fprintf(stderr, "mcl: AddOrRemoveMCL: could not initialize COM\n");
        exit(1);
    }

    InternalAddOrRemove(AddOrRemove);

    if (fInitCom)
        CoUninitialize();
}
