'
' Script to update an MCL installation with new binaries.
'
' Takes optional command line argument which contains the path of the
' kit directory from which to copy the updated files.
'
' We have four files to update:
'   mcl.exe - lives in %SystemRoot%\system32.
'   mcl.sys - lives in %SystemRoot%\system32\drivers.
'   hosts - lives in %SystemRoot%\system32\drivers\etc.
'   lqsr.dll - lives in <NetMon install dir>\parsers.
'
' Note that NetMon may not be installed on the system.
'

set WshShell = CreateObject("WScript.Shell")
set FileSys = CreateObject("Scripting.FileSystemObject")
set Args = WScript.Arguments

'
' Check command-line arguments for a kit directory.
' If no kit directory is specified, it is assumed that this update
' script resides in the kit directory.
'
if not Args.Count = 0 then
    KitFolder = Args(0)
else
    ScriptName = WScript.ScriptFullName
    KitFolder = Left(ScriptName, InstrRev(ScriptName, "\"))
end if
if FileSys.FolderExists(KitFolder) = False then
    MaybeEcho "KitFolder " & KitFolder & " cannot be accessed."
    WScript.Quit
end if
if not Right(KitFolder, 1) = "\" then
    KitFolder = KitFolder & "\"
end if

'
' Find out where Windows is installed on this system.
'
SystemRoot = WshShell.ExpandEnvironmentStrings("%SystemRoot%")
if SystemRoot = "%SystemRoot%" then
    MaybeEcho "SystemRoot environment variable not set!?!"
    WScript.Quit
end if
SystemSys = SystemRoot & "\system32\"

'
' Find the netmon installation directory, if there is one.
' We check the registry for the handler for netmon documents in
' order to find the path to the netmon executable.  From that key
' (HKCR\netmon.document\shell\open\command) we can extract the
' directory to which netmon was installed.
'
Key = "HKCR\netmon.document\shell\open\command\"
on error resume next
NetMonExe = WshShell.RegRead(Key)
if not err.number = 0 then
    MaybeEcho "NetMon appears to not be installed on this machine."
    NetMonFolder = ""
else
    LastSlash = InstrRev(NetMonExe, "\")
    NetMonFolder = Left(NetMonExe, LastSlash)
    if FileSys.FolderExists(NetMonFolder) = False then
        MaybeEcho "NetMon appears to not be installed on this machine."
        NetMonFolder = ""
    end if
end if

'
' Make up a list of the files to copy and their appropriate destinations.
'
set Dict = CreateObject("Scripting.Dictionary")
Dict.Add "mcl.exe", SystemSys
Dict.Add "mcl.sys", SystemSys & "drivers\"
Dict.Add "mcl.pdb", SystemSys & "drivers\"
if FileSys.FileExists("hosts") then
    Dict.Add "hosts", SystemSys & "drivers\etc\"
end if
if not NetMonFolder = "" then
    Dict.Add "lqsr.dll", NetMonFolder & "parsers\"
end if

'
' Code to do the actual copying.
'
for each Entry in Dict
    FileName = KitFolder & Entry
    if FileSys.FileExists(FileName) then
        '
        ' Copy the file, with overwrite flag to TRUE.
        '
        DestName = Dict.Item(Entry) & Entry
        if FileSys.FileExists(DestName) then
            set File = FileSys.GetFile(DestName)
            File.Attributes = File.Attributes and not 14000 and not 1
        end if
        set File = FileSys.GetFile(FileName)
        File.Copy DestName, TRUE
        MaybeEcho "Copied " & FileName & " to " & DestName
    else
        '
        ' Keep a list of files we wanted to copy but couldn't find.
        '
        Missing = Missing & FileName & " "
    end if
next
if not isEmpty(Missing) then
    MaybeEcho "Couldn't find following files to copy: " & Missing
end if


'
' Find the existing MCL adapter.
'
if GetMCLAdapterInfo(Index, Name, ConfigCode) = FALSE then
    MaybeEcho "Cannot find a LLSR adapter to disable/re-enable."
    WScript.Quit
end if
if ConfigCode = 22 then
    MaybeEcho "Found disabled LLSR adapter, leaving disabled."
    WScript.Quit
end if
if not ConfigCode = 0 then
    MaybeEcho "MCL adapter is having problems, leaving in current state."
    WScript.Quit
end if


'
' Disable MCL adapter.
'
MaybeEcho "Disabling MCL adapter..."
Command = SystemSys & "mcl.exe disable"
WshShell.Run Command, 0, TRUE

'
' Wait for it to become disabled.
'   
for Count = 1 to 10
    if GetMCLAdapterInfo(Index, Name, ConfigCode) = FALSE then
        MaybeEcho "Cannot find a MCL adapter."
        WScript.Quit
    end if
    if ConfigCode = 22 then
        exit for
    end if
    WScript.Echo "."
    WScript.Sleep 500
next

'
' Re-enable the MCL adapter.
'
MaybeEcho "Re-enabling MCL adapter..."
Command = SystemSys & "mcl.exe enable"
WshShell.Run Command, 0, TRUE

WScript.Quit


'
' Verson of WScript.Echo that is only chatty in interactive mode.
'
function MaybeEcho(Message)
    if WScript.Interactive then
        WScript.Echo Message
    end if
end function


function GetMCLAdapterInfo(Num, Id, Code)
    set WMIService = GetObject("winmgmts:\\.\root\cimv2")
    set Adapters = _
        WMIService.ExecQuery("Select * from Win32_NetworkAdapter",,48)
    GetMCLAdapterInfo = FALSE
    for each Adapter in Adapters
        if Adapter.Description = _
            "MCL (Mesh Connectivity Layer) Virtual Adapter" then
            GetMCLAdapterInfo = TRUE
            Num = Adapter.Index
            Id = Adapter.NetConnectionID
            Code = Adapter.ConfigManagerErrorCode
            exit for
        end if
    next
    set Adapters = Nothing
    set WMIService = Nothing
end function
