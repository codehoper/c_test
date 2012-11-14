'
' Script to create Mesh Connectivity Layer (MCL) install kit.
'

'
' Read some environment variables.
'
set WshShell = CreateObject("WScript.Shell")
NTDrive = WshShell.ExpandEnvironmentStrings("%_NTDRIVE%")
if NTDrive = "%_NTDRIVE%" then
    MsgBox("Please run this command from a razzle shell")
    Wscript.Quit
end if
BuildType = WshShell.ExpandEnvironmentStrings("%_BuildType%")
BuildArch = WshShell.ExpandEnvironmentStrings("%_BuildArch%")
if BuildArch = "x86" then
    BuildArch = "i386"
end if
ObjDir = "obj" & BuildType & "\" & BuildArch & "\"
MclRoot = WshShell.ExpandEnvironmentStrings("%MCLRoot%")
if MclRoot = "%MCLRoot%" then
    '
    ' If the user hasn't specified a root directory via an environment
    ' variable, we assume this script lives in the mcl subdirectory.
    '
    ScriptName = Wscript.ScriptFullName
    LlsrFolder = Left(ScriptName, InstrRev(ScriptName, "\"))
else
    if not Right(MclRoot, 1) = "\" then
        MclRoot = MclRoot & "\"
    end if
    MclFolder = MclRoot & "mcl\"
end if

'
' Source directories.
'
InfFolder = MclFolder & "inf\"
BinExeFolder = MclFolder & "exe\" & ObjDir
BinExpertFolder = MclFolder & "expert\" & ObjDir
BinParserFolder = MclFolder & "parser\" & ObjDir
BinSysFolder = MclFolder & "sys\" & ObjDir

'
' Destination directory.
' Check command-line arguments for a destination kit directory.
' If no kit directory is specified, we put it into a "MclKit"
' directory in the root of the drive used to build the kit.
'
set Args = Wscript.Arguments
if not Args.Count = 0 then
    KitFolder = Args(0)
    if not Right(KitFolder, 1) = "\" then
        KitFolder = KitFolder & "\"
    end if
else
    KitFolder = NTDrive & "\MclKit\"
end if


'
' Dictionary listing of files to copy, and where to find them.
'
set Dict = CreateObject("Scripting.Dictionary")
Dict.Add "ReadMe.txt", MclFolder
Dict.Add "update.vbs", MclFolder
Dict.Add "license.txt", MclFolder
Dict.Add "mclmp.inf", InfFolder
Dict.Add "mcltp.inf", InfFolder
Dict.Add "mcl.exe", BinExeFolder
Dict.Add "lqsr.dll", BinParserFolder
Dict.Add "mcl.sys", BinSysFolder
Dict.Add "mcl.pdb", BinSysFolder


'
' Code to do the actual copying.
'
set FileSys = CreateObject("Scripting.FileSystemObject")
if FileSys.FolderExists(KitFolder) = False then
    FileSys.CreateFolder(KitFolder)
end if
for each Entry in Dict
    FileName = Dict.Item(Entry) & Entry
    if FileSys.FileExists(FileName) then
        '
        ' Copy the file, with overwrite flag to TRUE.
        '
        DestName = KitFolder & Entry
        if FileSys.FileExists(DestName) then
            set File = FileSys.GetFile(DestName)
            File.Attributes = File.Attributes and not 14000 and not 1
        end if
        set File = FileSys.GetFile(FileName)
        File.Copy DestName, TRUE
    else
        '
        ' Keep a list of files we wanted to copy but couldn't find.
        '
        Missing = Missing & FileName & vbCr
    end if
next

if not isEmpty(Missing) then
    MsgBox("Couldn't find following files to copy:" & vbCr & Missing)
end if
