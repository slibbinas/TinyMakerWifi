' TinyMaker Resin Lab - dervu testai vienu paspaudimu.
'
' Irankiui reikia savo serverio (server.py salia): jis raso irasus ir
' nuotraukas i Google Drive aplanka, o narsykle to pati negali. Diegti nieko
' nereikia - serveris naudoja tik Python standartine biblioteka, o Python
' imamas is PlatformIO (arba is PATH).
'
' Paleidejas guli tame paciame aplanke kaip server.py: install.ps1 ji kopijuoja
' i %USERPROFILE%\Tools\TinyMaker kartu su irankiu, ir darbastalio nuorodos
' rodo cia. Argumentas - kuri puslapi atidaryti (numatyta resin-lab/; dervu
' bibliotekos nuoroda perduoda resin-publish.html). Langas nerodomas.

Option Explicit
Dim sh, fso, labDir, py, page, base, i

Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

labDir = fso.GetParentFolderName(WScript.ScriptFullName)
If Not fso.FileExists(labDir & "\server.py") Then
  MsgBox "Neradau server.py aplanke:" & vbCrLf & labDir & vbCrLf & vbCrLf & _
         "Idiekite iranki is naujo: scripts\dev\resin-lab\install.ps1", 48, _
         "TinyMaker - dervu testai"
  WScript.Quit
End If

page = "resin-lab/"
If WScript.Arguments.Count > 0 Then page = WScript.Arguments(0)

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
base = "http://localhost:8893/"
If Not Serves(base & "api/lab/config") Then
  sh.Run "cmd /c """"" & py & """ """ & labDir & "\server.py""""", 0, False
  ' Laukiam, kol serveris atsilieps, o ne fiksuota sekundziu skaiciu: saltas
  ' python startas kartais uztrunka ilgiau.
  For i = 1 To 20
    If Serves(base & "api/lab/config") Then Exit For
    WScript.Sleep 400
  Next
End If

sh.Run base & page, 1, False
