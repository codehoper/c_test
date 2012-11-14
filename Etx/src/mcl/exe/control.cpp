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
// Abstract:
//
// Device control (enable/disable) code.  Loosely based on the
// "devcon" sample in the DDK.
//

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <setupapi.h>
#include <devguid.h>

int
ControlDevice(
    HDEVINFO Devs,             // Device set.
    PSP_DEVINFO_DATA DevInfo,  // Device in set.
    DWORD Action)              // Action to perform.
{
    SP_PROPCHANGE_PARAMS Params;

    //
    // Operate on config-specific profile.
    //
    Params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    Params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    Params.StateChange = Action;
    Params.Scope = DICS_FLAG_CONFIGSPECIFIC;
    Params.HwProfile = 0;

    if (SetupDiSetClassInstallParams(Devs, DevInfo, &Params.ClassInstallHeader,
                                     sizeof(Params)) &&
        SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, Devs, DevInfo)) {
        //
        // Success!
        //
        return 1;
    } else {
        return 0;
    }
}


LPTSTR
GetDeviceDescription(
    HDEVINFO Devs,             // Device set.
    PSP_DEVINFO_DATA DevInfo)  // Device in set.
{
    LPTSTR Buffer;
    DWORD Attempt;
    DWORD Size;
    DWORD ActualSize;
    DWORD DataType;

    Size = 64;  // Educated guess.

    for (Attempt = 0; Attempt < 2; Attempt++) {

        Buffer = new TCHAR[(Size / sizeof(TCHAR)) + sizeof(TCHAR)];
        if (Buffer == NULL) {
            return NULL;
        }

        if (!SetupDiGetDeviceRegistryProperty(Devs, DevInfo, SPDRP_DEVICEDESC,
                                              &DataType, (LPBYTE)Buffer, Size,
                                              &ActualSize)) {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                delete [] Buffer;
                return NULL;
            }

            //
            // Our guess at a buffer size was too small.  Try again.
            //
            Size = ActualSize;
            delete [] Buffer;
            continue;
        }

        //
        // Make sure the registry entry we found is of the expected type.
        //
        if (DataType != REG_SZ) {
            delete [] Buffer;
            return NULL;
        }

        Size = ActualSize / sizeof(TCHAR);
        Buffer[Size] = TEXT('\0');

        return Buffer;
    }

    return NULL;
}


EXTERN_C int
ControlDeviceClass(
    LPTSTR Query,  // Prefix of device description string to match.
    DWORD Action)  // Action to take on found device(s).
{
    HDEVINFO Devs = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA DevInfo;
    DWORD DevIndex;
    int Count = 0;

    //
    // Create a device information set, consisting of our desired devices.
    //
    Devs = SetupDiGetClassDevs(&GUID_DEVCLASS_NET,  // Only "Net" devices.
                               NULL,
                               NULL,
                               DIGCF_PRESENT);      // And currently present.
    if (Devs == INVALID_HANDLE_VALUE) {
        return 0;
    }

    //
    // Run through the set, looking for devices for which the prefix of their
    // description string matches our query.
    //
    DevInfo.cbSize = sizeof(DevInfo);
    for (DevIndex = 0; SetupDiEnumDeviceInfo(Devs, DevIndex, &DevInfo);
         DevIndex++) {

        LPTSTR Buffer;

        Buffer = GetDeviceDescription(Devs, &DevInfo);

        if (Buffer == NULL)
            continue;

        if (_tcsnicmp(Buffer, Query, _tcslen(Query)) == 0) {
            //
            // Found one.  Attempt to take requested action.
            //
            Count += ControlDevice(Devs, &DevInfo, Action);
        }

        delete [] Buffer;
    }

    SetupDiDestroyDeviceInfoList(Devs);

    return Count;
}
