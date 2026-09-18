# TinyMaker Resin Lab - idiegimas ir atnaujinimas (Windows PowerShell 5.1).
#
# Iranki kopijuoja i atskira aplanka (numatyta %USERPROFILE%\Tools\resin-lab),
# kad jis nepriklausytu nuo repo darbo katalogu, kurie keiciasi ir trinami.
# Ta pati komanda ir atnaujina: failai perrasomi, local.json (keliai, printerio
# adresas) lieka.
#
#   powershell -ExecutionPolicy Bypass -File install.ps1
#
# Parametrai:
#   -Target     kur diegti
#   -Ref        GitHub saka arba zyme (numatyta main)
#   -Source     vietinis repo katalogas vietoj GitHub (bandymams)
#   -NoShortcut nekurti darbastalio nuorodos
#   -NoStart    nestabdyti veikiancio serverio ir nepaleisti naujo

param(
  [string]$Target = (Join-Path $env:USERPROFILE 'Tools\resin-lab'),
  [string]$Ref = 'main',
  [string]$Source = '',
  [switch]$NoShortcut,
  [switch]$NoStart
)
$ErrorActionPreference = 'Stop'
$Repo = 'slibbinas/TinyMakerWiFi'
# Be sito aplanko irankis dar naudoja resin-fields.js (bendra firmware ribu
# lentele), resin-publish.html (irasas i biblioteka) ir PrusaSlicer profili
# pjaustymui. Keliai issaugomi tokie pat kaip repo: server.py juos skaiciuoja
# nuo savo vietos.
$Extra = @('scripts/dev/resin-fields.js', 'scripts/dev/resin-publish.html', 'PrusaSlicer/TinyMaker.ini')
$LabRel = 'scripts/dev/resin-lab'

function Test-Wanted($p) {
  ($p.StartsWith("$LabRel/") -and $p -ne "$LabRel/local.json") -or ($Extra -contains $p)
}

# 1. Failu sarasas
if ($Source) {
  $Source = (Resolve-Path $Source).Path
  $files = Get-ChildItem -Path (Join-Path $Source $LabRel) -File -Recurse |
    ForEach-Object { $_.FullName.Substring($Source.Length + 1).Replace('\', '/') }
  $files = @($files) + $Extra | Where-Object { Test-Wanted $_ }
} else {
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  $hdr = @{ 'User-Agent' = 'resin-lab-install' }
  $tree = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/git/trees/${Ref}?recursive=1" -Headers $hdr
  $files = $tree.tree | Where-Object { $_.type -eq 'blob' -and (Test-Wanted $_.path) } | ForEach-Object { $_.path }
}
$files = @($files)
if (-not ($files -contains "$LabRel/server.py")) { throw "Saltinyje nerasta $LabRel/server.py" }

# 2. Kopijavimas
Write-Host "Diegiu $($files.Count) failus i $Target"
foreach ($f in $files) {
  $dst = Join-Path $Target ($f.Replace('/', '\'))
  New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
  if ($Source) {
    Copy-Item -Force -Path (Join-Path $Source ($f.Replace('/', '\'))) -Destination $dst
  } else {
    Invoke-WebRequest -UseBasicParsing -Headers $hdr -Uri "https://raw.githubusercontent.com/$Repo/$Ref/$f" -OutFile $dst
  }
}
$lab = Join-Path $Target ($LabRel.Replace('/', '\'))
$launcher = Join-Path $lab 'launcher.vbs'

# 3. Nustatymai: pirmo diegimo metu perimami is seno darbo katalogo, jei ten yra.
$cfg = Join-Path $lab 'local.json'
if (-not (Test-Path $cfg)) {
  $proj = Join-Path $env:USERPROFILE 'Documents\PlatformIO\Projects\TinyMakerWiFi'
  $cands = @(Join-Path $proj 'scripts\dev\resin-lab\local.json')
  $wt = Join-Path $proj '.claude\worktrees'
  if (Test-Path $wt) {
    $cands += Get-ChildItem -Path $wt -Directory | ForEach-Object { Join-Path $_.FullName 'scripts\dev\resin-lab\local.json' }
  }
  $old = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
  if ($old) {
    Copy-Item -Path $old -Destination $cfg
    Write-Host "Nustatymai perimti is $old"
  }
}

# 4. Python
$py = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path $py) -and -not (Get-Command python -ErrorAction SilentlyContinue)) {
  Write-Warning 'Neradau Python (nei PlatformIO, nei PATH) - serveris nepasileis.'
}

# 5. Darbastalio nuoroda
if (-not $NoShortcut) {
  $desk = [Environment]::GetFolderPath('Desktop')
  $ws = New-Object -ComObject WScript.Shell
  $lnk = $ws.CreateShortcut((Join-Path $desk '00 TinyMaker dervu testai.lnk'))
  $lnk.TargetPath = Join-Path $env:WINDIR 'System32\wscript.exe'
  $lnk.Arguments = '"' + $launcher + '"'
  $lnk.WorkingDirectory = $lab
  $lnk.IconLocation = (Join-Path $lab 'resin-lab.ico') + ',0'
  $lnk.Description = 'TinyMaker Resin Lab'
  $lnk.Save()
  Write-Host 'Darbastalio nuoroda atnaujinta.'
}

# 6. Serveris laiko koda atmintyje: senas procesas stabdomas, paleidziamas naujas.
if (-not $NoStart) {
  Get-CimInstance Win32_Process |
    Where-Object { $_.CommandLine -like '*resin-lab*server.py*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  Start-Process -FilePath (Join-Path $env:WINDIR 'System32\wscript.exe') -ArgumentList ('"' + $launcher + '"')
}
Write-Host 'Baigta.'
