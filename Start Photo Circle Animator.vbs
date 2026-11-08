Option Explicit

Dim shell, fileSystem, folder, batchPath, logPath, command, exitCode
Set shell = CreateObject("WScript.Shell")
Set fileSystem = CreateObject("Scripting.FileSystemObject")

folder = fileSystem.GetParentFolderName(WScript.ScriptFullName)
batchPath = fileSystem.BuildPath(folder, "Start Photo Circle Animator.bat")
logPath = fileSystem.BuildPath(folder, "PhotoCircleAnimator-build.log")

If Not fileSystem.FileExists(batchPath) Then
    MsgBox "The launcher file is missing:" & vbCrLf & batchPath, _
        vbOKOnly + vbCritical, "Photo Circle Animator"
    WScript.Quit 1
End If

command = Chr(34) & shell.ExpandEnvironmentStrings("%ComSpec%") & Chr(34) & _
    " /d /c " & Chr(34) & Chr(34) & batchPath & Chr(34) & Chr(34)
exitCode = shell.Run(command, 0, True)

If exitCode <> 0 Then
    MsgBox "The latest Photo Circle Animator could not be built or started. " & _
        "Close any running copy of PhotoCircleAnimator.exe, then double-click " & _
        "this launcher again. Do not start the old PhotoCircleAnimator.exe directly." & _
        vbCrLf & vbCrLf & "If the build compiler is missing, install the MSYS2 " & _
        "UCRT64 MinGW-w64 C++ compiler. Build details are in:" & vbCrLf & logPath, _
        vbOKOnly + vbExclamation, "Photo Circle Animator"
    If fileSystem.FileExists(logPath) Then
        shell.Run "notepad.exe " & Chr(34) & logPath & Chr(34), 1, False
    End If
End If
