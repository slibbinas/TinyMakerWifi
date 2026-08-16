' TinyMaker - vietinis ukis vienu paspaudimu.
'
' Nuoroda ant darbastalio be sito neveiktu: puslapiai guli faile, o jiems reikia
' serverio (dalis dalyku nesikrauna is file://). Sitas paleidejas:
'   1. susiranda scripts/dev kataloga (paprastai ta pati, kur guli sitas failas),
'   2. persigeneruoja pulta, kad datos butu tikros,
'   3. pakelia serveri TIK jei jo dar nera (antras paspaudimas nedubliuoja),
'   4. atidaro narsykle.
' Langas nerodomas - jokio juodo lango mirksejimo.

Option Explicit
Dim sh, fso, i, dirs, devDir, py, alive, http, url
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

url = "http://localhost:8899/"

' Kur gali gyventi scripts/dev: pirma sito failo paties katalogas (tada veikia ir
' pagrindiniame repo, ir worktree kopijoje, ir repo perkelus), tada numatytoji
' vieta. Ikoduoti kelio su vartotojo vardu cia negalima - kitam zmogui neveiktu,
' o worktree kelias dar ir laikinas.
dirs = Array( _
  fso.GetParentFolderName(WScript.ScriptFullName), _
  sh.ExpandEnvironmentStrings("%USERPROFILE%") & "\Documents\PlatformIO\Projects\TinyMakerWiFi\scripts\dev")

devDir = ""
For i = 0 To UBound(dirs)
  If devDir = "" And fso.FileExists(dirs(i) & "\make_hub.py") Then devDir = dirs(i)
Next

If devDir = "" Then
  MsgBox "Neradau scripts\dev katalogo su make_hub.py." & vbCrLf & _
         "Ar repo perkeltas? Pataisyk keliu sarasa faile ukis.vbs.", 48, "TinyMaker ukis"
  WScript.Quit
End If

py = sh.ExpandEnvironmentStrings("%USERPROFILE%") & "\.platformio\penv\Scripts\python.exe"
If Not fso.FileExists(py) Then py = "python"

' 1. Sviezios datos ir "kandidatai i archyva" - kaskart atidarant.
sh.Run """" & py & """ """ & devDir & "\make_hub.py""", 0, True

' 2. Serveris - tik jei dar negyvas.
alive = False
On Error Resume Next
Set http = CreateObject("MSXML2.XMLHTTP")
http.Open "GET", url, False
http.Send
If Err.Number = 0 Then alive = True
Err.Clear
On Error GoTo 0

If Not alive Then
  sh.CurrentDirectory = devDir
  sh.Run "cmd /c """"" & py & """ -m http.server 8899""", 0, False
  WScript.Sleep 1500
End If

' 3. Narsykle.
sh.Run url, 1, False
