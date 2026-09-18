' TinyMaker ukis - vietiniu irankiu registras ir puslapiai vienu paspaudimu.
'
' Paleidejas guli tame paciame aplanke kaip make_hub.py (install.ps1 ji
' kopijuoja i %USERPROFILE%\Tools\TinyMaker\scripts\dev). Pirma sugeneruoja
' index.html (datos ir planu kopijos turi buti sviezios), tada dalija si
' aplanka per localhost:8899. Langas nerodomas.

Option Explicit
Dim sh, fso, devDir, py, url, i

Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

devDir = fso.GetParentFolderName(WScript.ScriptFullName)
If Not fso.FileExists(devDir & "\make_hub.py") Then
  MsgBox "Neradau make_hub.py aplanke:" & vbCrLf & devDir & vbCrLf & vbCrLf & _
         "Idiekite is naujo: scripts\dev\resin-lab\install.ps1", 48, "TinyMaker ukis"
  WScript.Quit
End If

py = sh.ExpandEnvironmentStrings("%USERPROFILE%") & "\.platformio\penv\Scripts\python.exe"
If Not fso.FileExists(py) Then py = "python"

sh.Run """" & py & """ """ & devDir & "\make_hub.py""", 0, True
If Not fso.FileExists(devDir & "\index.html") Then
  MsgBox "Nepavyko sugeneruoti pulto (index.html)." & vbCrLf & vbCrLf & _
         "Katalogas: " & devDir & vbCrLf & "Python: " & py, 48, "TinyMaker ukis"
  WScript.Quit
End If

Function Serves(u)
  Dim http
  Serves = False
  On Error Resume Next
  Set http = CreateObject("MSXML2.XMLHTTP")
  http.Open "GET", u, False
  http.Send
  If Err.Number = 0 Then If http.Status = 200 Then Serves = True
  Err.Clear
  On Error GoTo 0
End Function

url = "http://localhost:8899/index.html"
If Not Serves(url) Then
  sh.CurrentDirectory = devDir
  sh.Run "cmd /c """"" & py & """ -m http.server 8899 --bind 127.0.0.1""", 0, False
  For i = 1 To 20
    If Serves(url) Then Exit For
    WScript.Sleep 400
  Next
End If

sh.Run url, 1, False
