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

#include "headers.h"
#include <string.h>
#include <wchar.h>

#define WORK_BUFFER_SIZE  512

//* OpenRegKey
//
//  Opens a Registry key and returns a handle to it.
//
//  Returns (plus other failure codes):
//      STATUS_OBJECT_NAME_NOT_FOUND
//      STATUS_SUCCESS
//
NTSTATUS
OpenRegKey(
    PHANDLE HandlePtr,  // Where to write the opened handle.
    HANDLE Parent,
    const WCHAR *KeyName,     // Name of Registry key to open.
    OpenRegKeyAction Action)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING UKeyName;

    PAGED_CODE();

    RtlInitUnicodeString(&UKeyName, KeyName);

    memset(&ObjectAttributes, 0, sizeof(OBJECT_ATTRIBUTES));
    InitializeObjectAttributes(&ObjectAttributes, &UKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               Parent, NULL);

    switch (Action) {
    case OpenRegKeyRead:
        Status = ZwOpenKey(HandlePtr, KEY_READ, &ObjectAttributes);
        break;

    case OpenRegKeyCreate:
        Status = ZwCreateKey(HandlePtr, KEY_WRITE, &ObjectAttributes,
                             0,         // TitleIndex
                             NULL,      // Class
                             REG_OPTION_NON_VOLATILE,
                             NULL);     // Disposition
        break;

    case OpenRegKeyDeleting:
        Status = ZwOpenKey(HandlePtr, KEY_ALL_ACCESS, &ObjectAttributes);
        break;

    default:
        ASSERT(!"bad Action");
        Status = STATUS_INVALID_PARAMETER;
        break;
    }

    return Status;
}

//* GetRegDWORDValue
//
//  Reads a REG_DWORD value from the registry into the supplied variable.
//
NTSTATUS  // Returns: STATUS_SUCCESS or an appropriate failure code.
GetRegDWORDValue(
    HANDLE KeyHandle,  // Open handle to the parent key of the value to read.
    const WCHAR *ValueName,  // Name of the value to read.
    PULONG ValueData)  // Variable into which to read the data.
{
    NTSTATUS status;
    ULONG resultLength;
    PKEY_VALUE_FULL_INFORMATION keyValueFullInformation;
    UCHAR keybuf[WORK_BUFFER_SIZE];
    UNICODE_STRING UValueName;

    PAGED_CODE();

    RtlInitUnicodeString(&UValueName, ValueName);

    keyValueFullInformation = (PKEY_VALUE_FULL_INFORMATION)keybuf;
    RtlZeroMemory(keyValueFullInformation, sizeof(keyValueFullInformation));

    status = ZwQueryValueKey(KeyHandle, &UValueName, KeyValueFullInformation,
                             keyValueFullInformation, WORK_BUFFER_SIZE,
                             &resultLength);

    if (NT_SUCCESS(status)) {
        if (keyValueFullInformation->Type != REG_DWORD) {
            status = STATUS_INVALID_PARAMETER_MIX;
        } else {
            *ValueData = *((ULONG UNALIGNED *)
                           ((PCHAR)keyValueFullInformation +
                            keyValueFullInformation->DataOffset));
        }
    }

    return status;
}

//* SetRegDWORDValue
//
//  Writes the contents of a variable to a REG_DWORD value.
//
NTSTATUS  // Returns: STATUS_SUCCESS or an appropriate failure code.
SetRegDWORDValue(
    HANDLE KeyHandle,  // Open handle to the parent key of the value to write.
    const WCHAR *ValueName,  // Name of the value to write.
    ULONG ValueData)  // Variable from which to write the data.
{
    NTSTATUS status;
    UNICODE_STRING UValueName;

    PAGED_CODE();

    RtlInitUnicodeString(&UValueName, ValueName);

    status = ZwSetValueKey(KeyHandle, &UValueName, 0, REG_DWORD,
                           &ValueData, sizeof ValueData);

    return status;
}

//* GetRegStringValue
//
//  Reads a REG_*_SZ string value from the Registry into the supplied
//  key value buffer.  If the buffer string buffer is not large enough,
//  it is reallocated.
//
NTSTATUS  // Returns: STATUS_SUCCESS or an appropriate failure code.
GetRegStringValue(
    HANDLE KeyHandle,   // Open handle to the parent key of the value to read.
    const WCHAR *ValueName,   // Name of the value to read.
    PKEY_VALUE_PARTIAL_INFORMATION *ValueData,  // Destination of read data.
    PUSHORT ValueSize)  // Size of the ValueData buffer.  Updated on output.
{
    NTSTATUS status;
    ULONG resultLength;
    UNICODE_STRING UValueName;

    PAGED_CODE();

    RtlInitUnicodeString(&UValueName, ValueName);

    status = ZwQueryValueKey(KeyHandle, &UValueName,
                             KeyValuePartialInformation, *ValueData,
                             (ULONG) *ValueSize, &resultLength);

    if ((status == STATUS_BUFFER_OVERFLOW) ||
        (status == STATUS_BUFFER_TOO_SMALL)) {
        PVOID temp;

        //
        // Free the old buffer and allocate a new one of the
        // appropriate size.
        //

        ASSERT(resultLength > (ULONG) *ValueSize);

        if (resultLength <= 0xFFFF) {

            temp = ExAllocatePool(NonPagedPool, resultLength);

            if (temp != NULL) {

                if (*ValueData != NULL) {
                    ExFreePool(*ValueData);
                }

                *ValueData = temp;
                *ValueSize = (USHORT) resultLength;

                status = ZwQueryValueKey(KeyHandle, &UValueName,
                                         KeyValuePartialInformation,
                                         *ValueData, *ValueSize,
                                         &resultLength);

                ASSERT((status != STATUS_BUFFER_OVERFLOW) &&
                       (status != STATUS_BUFFER_TOO_SMALL));
            } else {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    return status;
}

//* GetRegSZValue
//
//  Reads a REG_SZ string value from the Registry as a Unicode string,
//  allocating the string buffer.
//
//  Note that the caller must free the string buffer even
//  if this function returns a failure code.
//
NTSTATUS  // Returns: STATUS_SUCCESS or an appropriate failure code.
GetRegSZValue(
    HANDLE KeyHandle,  // Open handle to the parent key of the value to read.
    const WCHAR *ValueName,  // Name of the value to read.
    PUNICODE_STRING ValueData)  // Destination string for the value data.
{
    PKEY_VALUE_PARTIAL_INFORMATION keyValuePartialInformation;
    NTSTATUS status;

    PAGED_CODE();

    ValueData->Buffer = NULL;
    ValueData->MaximumLength = 0;
    ValueData->Length = 0;

    status = GetRegStringValue(KeyHandle, ValueName,
                               (PKEY_VALUE_PARTIAL_INFORMATION *)
                               &(ValueData->Buffer),
                               &(ValueData->MaximumLength));

    if (NT_SUCCESS(status)) {

        keyValuePartialInformation =
            (PKEY_VALUE_PARTIAL_INFORMATION)ValueData->Buffer;

        if (keyValuePartialInformation->Type == REG_SZ) {
            WCHAR *src;
            WCHAR *dst;
            ULONG dataLength;

            dataLength = keyValuePartialInformation->DataLength;
            ASSERT(dataLength <= ValueData->MaximumLength);

            dst = ValueData->Buffer;
            src = (PWCHAR) &(keyValuePartialInformation->Data);

            while (ValueData->Length <= dataLength) {

                if ((*dst++ = *src++) == UNICODE_NULL)
                    break;

                ValueData->Length += sizeof(WCHAR);
            }

            if (ValueData->Length < (ValueData->MaximumLength - 1)) {
                ValueData->Buffer[ValueData->Length/sizeof(WCHAR)] =
                    UNICODE_NULL;
            }
        } else {
            status = STATUS_INVALID_PARAMETER_MIX;
        }
    }

    return status;
}

//* GetVirtualAddress
//
//  Reads a VirtualAddress from a string.
//
boolint
GetVirtualAddress(const WCHAR *astr, VirtualAddress Address)
{
    uint Number;
    uint i;

    for (i = 0; ; i++) {
        if ((L'0' <= astr[0]) && (astr[0] <= L'9'))
            Number = astr[0] - L'0';
        else if ((L'a' <= astr[0]) && (astr[0] <= L'f'))
            Number = 10 + (astr[0] - L'a');
        else if ((L'A' <= astr[0]) && (astr[0] <= L'F'))
            Number = 10 + (astr[0] - L'A');
        else
            return FALSE;

        Number *= 16;

        if ((L'0' <= astr[1]) && (astr[1] <= L'9'))
            Number += astr[1] - L'0';
        else if ((L'a' <= astr[1]) && (astr[1] <= L'f'))
            Number += 10 + (astr[1] - L'a');
        else if ((L'A' <= astr[1]) && (astr[1] <= L'F'))
            Number += 10 + (astr[1] - L'A');
        else
            return FALSE;

        Address[i] = (uchar) Number;

        if (i == 5) {
            if (astr[2] != L'\0')
                return FALSE;
            break;
        }

        if (astr[2] != L'-')
            return FALSE;

        astr += 3;
    }

    return TRUE;
}

//* GetRegNetworkAddress
//
//  Reads a virtual address from the registry.
//
NTSTATUS  // Returns: STATUS_SUCCESS or an appropriate failure code.
GetRegNetworkAddress(
    HANDLE KeyHandle,  // Open handle to the parent key of the value to read.
    const WCHAR *ValueName,
    OUT VirtualAddress Address)
{
    UNICODE_STRING String;
    NTSTATUS Status;

    Status = GetRegSZValue(KeyHandle, ValueName, &String);
    if (NT_SUCCESS(Status)) {

        if (GetVirtualAddress(String.Buffer, Address))
            Status = STATUS_SUCCESS;
        else
            Status = STATUS_INVALID_PARAMETER;
    }

    if (String.Buffer != NULL)
        ExFreePool(String.Buffer);

    return Status;
}

//* SetRegNetworkAddress
//
//  Writes a network address to the registry.
//
NTSTATUS
SetRegNetworkAddress(
    HANDLE KeyHandle,  // Open handle to the parent key.
    const WCHAR *ValueName,
    IN VirtualAddress Address)
{
    UNICODE_STRING UValueName;
    WCHAR Buffer[6*3];

    RtlInitUnicodeString(&UValueName, ValueName);

    swprintf(Buffer, L"%02x-%02x-%02x-%02x-%02x-%02x",
             Address[0], Address[1], Address[2],
             Address[3], Address[4], Address[5]);

    //
    // The trailing zero is included in the value.
    //
    return ZwSetValueKey(KeyHandle, &UValueName, 0, REG_SZ,
                         Buffer, sizeof Buffer);
}

//* GetRegGuid
//
//  Reads a GUID from the registry.
//
NTSTATUS  // Returns: STATUS_SUCCESS or an appropriate failure code.
GetRegGuid(
    HANDLE KeyHandle,  // Open handle to the parent key of the value to read.
    const WCHAR *ValueName,
    OUT GUID *Guid)
{
    UNICODE_STRING String;
    NTSTATUS Status;

    Status = GetRegSZValue(KeyHandle, ValueName, &String);
    if (NT_SUCCESS(Status)) {

        Status = RtlGUIDFromString(&String, Guid);
    }

    if (String.Buffer != NULL)
        ExFreePool(String.Buffer);

    return Status;
}

//* GetRegBinaryValue
//
//  Reads a binary value from the registry into the supplied buffer.
//
NTSTATUS  // Returns: STATUS_SUCCESS or an appropriate failure code.
GetRegBinaryValue(
    HANDLE KeyHandle,  // Open handle to the parent key of the value to read.
    const WCHAR *ValueName,  // Name of the value to read.
    OUT uchar *Buffer,  // Buffer into which to read the data.
    uint Length)        // Length of the buffer.
{
    KEY_VALUE_PARTIAL_INFORMATION *Value;
    UCHAR valbuf[WORK_BUFFER_SIZE];
    UNICODE_STRING UValueName;
    ULONG ValueSize;
    NTSTATUS Status;

    PAGED_CODE();

    RtlInitUnicodeString(&UValueName, ValueName);

    Value = (KEY_VALUE_PARTIAL_INFORMATION *) valbuf;

    Status = ZwQueryValueKey(KeyHandle, &UValueName,
                             KeyValuePartialInformation,
                             Value, sizeof valbuf,
                             &ValueSize);
    if (NT_SUCCESS(Status)) {
        if ((Value->Type != REG_BINARY) ||
            (Value->DataLength != Length)) {
            Status = STATUS_INVALID_PARAMETER_MIX;
        }
        else {
            RtlCopyMemory(Buffer, Value->Data, Length);
            Status = STATUS_SUCCESS;
        }
    }

    return Status;
}

//* SetRegBinaryValue
//
//  Write a binary value into the registry from the supplied buffer.
//
NTSTATUS  // Returns: STATUS_SUCCESS or an appropriate failure code.
SetRegBinaryValue(
    HANDLE KeyHandle,  // Open handle to the parent key of the value to read.
    const WCHAR *ValueName,  // Name of the value to write.
    IN uchar *Buffer,  // Buffer from which to read the data.
    uint Length)        // Length of the buffer.
{
    UNICODE_STRING UValueName;
    NTSTATUS Status;

    PAGED_CODE();

    RtlInitUnicodeString(&UValueName, ValueName);

    Status = ZwSetValueKey(KeyHandle, &UValueName,
                           0, REG_BINARY,
                           Buffer, Length);

    return Status;
}
