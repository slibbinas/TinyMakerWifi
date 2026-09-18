' TinyMaker Resin Lab - dervu testai vienu paspaudimu.
'
' Irankiui reikia savo serverio (server.py salia): jis raso irasus ir
' nuotraukas i Google Drive aplanka, o narsykle to pati negali. Diegti nieko
' nereikia - serveris naudoja tik Python standartine biblioteka, o Python
' imamas is PlatformIO (arba is PATH).
'
' Paleidejas guli tame paciame aplanke kaip server.py: install.ps1 ji kopijuoja
' i %USERPROFILE%\Tools\resin-lab kartu su irankiu, ir darbastalio nuoroda
' rodo cia. Langas nerodomas - jokio juodo lango mirksejimo.

Option Explicit
Dim sh, fso, labDir, py, url, i

Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

labDir = fso.GetParentFolderName(WScript.ScriptFullName)
If Not fso.FileExists(labDir & "\server.py") Then
  MsgBox "Neradau server.py aplanke:" & vbCrLf & labDir & vbCrLf & vbCrLf & _
         "Idiekite iranki is naujo: scripts\dev\resin-lab\install.ps1", 48, _
         "TinyMaker - dervu testai"
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

py = sh.ExpandEnvironmentStrings("%USERPROFILE%") & "\.platformio\penv\Scripts\python.exe"
If Not fso.FileExists(py) Then py = "python"

' Serveris: jei jau veikia - naudojam ji, jei ne - keliam.
url = "http://localhost:8897/resin-lab/"
If Not Serves("http://localhost:8897/api/lab/config") Then
  sh.Run "cmd /c """"" & py & """ """ & labDir & "\server.py""""", 0, False
  ' Laukiam, kol serveris atsilieps, o ne fiksuota sekundziu skaiciu: saltas
  ' python startas kartais uztrunka ilgiau.
  For i = 1 To 20
    If Serves("http://localhost:8897/api/lab/config") Then Exit For
    WScript.Sleep 400
  Next
End If

sh.Run url, 1, False
