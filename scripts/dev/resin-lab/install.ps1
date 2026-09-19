# TinyMaker vietiniai irankiai - idiegimas ir atnaujinimas (Windows PowerShell 5.1).
#
# Kopijuoja i atskira aplanka (numatyta %USERPROFILE%\Tools\TinyMaker), kad
# irankiai nepriklausytu nuo repo darbo katalogu, kurie keiciasi ir trinami:
#   Resin Lab (dervu testai)       localhost:8893/resin-lab/
#   dervu bibliotekos tvarkymas    localhost:8893/resin-publish.html
#   ukis (irankiu registras)       localhost:8899/
# Ta pati komanda ir atnaujina: failai perrasomi, asmeniniai nustatymai
# (resin-lab\local.json, local-links.json) lieka.
#
#   powershell -ExecutionPolicy Bypass -File install.ps1
#
# Parametrai:
#   -Target     kur diegti
#   -Ref        GitHub saka arba zyme (numatyta main)
#   -Source     vietinis repo katalogas vietoj GitHub (bandymams)
#   -NoShortcut nekurti darbastalio nuorodu
#   -NoStart    nestabdyti veikianciu serveriu ir nepaleisti nauju

param(
  [string]$Target = (Join-Path $env:USERPROFILE 'Tools\TinyMaker'),
  [string]$Ref = 'main',
  [string]$Source = '',
  [switch]$NoShortcut,
  [switch]$NoStart
)
$ErrorActionPreference = 'Stop'
$Repo = 'slibbinas/TinyMakerWiFi'
$LabRel = 'scripts/dev/resin-lab'
# Keliai issaugomi tokie pat kaip repo: server.py ir make_hub.py juos
# skaiciuoja nuo savo vietos.
$Extra = @('scripts/dev/make_hub.py', 'scripts/dev/ukis.vbs', 'PrusaSlicer/TinyMaker.ini')

function Test-Wanted($p) {
  if ($p -eq "$LabRel/local.json") { return $false }
  if ($p.StartsWith("$LabRel/") -or ($Extra -contains $p)) { return $true }
  if ($p.StartsWith('scripts/dev/archyvas/') -and $p.EndsWith('.html')) { return $true }
  # Ukio puslapiai ir ju bendri failai: scripts/dev virsaus .html ir .js (be sugeneruoto index.html).
  return ($p -match '^scripts/dev/[^/]+\.(html|js)$') -and ($p -ne 'scripts/dev/index.html')
}

# 1. Failu sarasas
if ($Source) {
  $Source = (Resolve-Path $Source).Path
  $files = Get-ChildItem -Path (Join-Path $Source 'scripts\dev') -File -Recurse |
    ForEach-Object { $_.FullName.Substring($Source.Length + 1).Replace('\', '/') }
  $files = @($files) + $Extra | Where-Object { Test-Wanted $_ } | Select-Object -Unique
} else {
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  $hdr = @{ 'User-Agent' = 'tinymaker-tools-install' }
  $tree = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/git/trees/${Ref}?recursive=1" -Headers $hdr
  $files = $tree.tree | Where-Object { $_.type -eq 'blob' -and (Test-Wanted $_.path) } | ForEach-Object { $_.path }
}
$files = @($files)
foreach ($must in "$LabRel/server.py", 'scripts/dev/make_hub.py') {
  if (-not ($files -contains $must)) { throw "Saltinyje nerasta $must" }
}

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
$dev = Join-Path $Target 'scripts\dev'
$lab = Join-Path $dev 'resin-lab'

# 3. Asmeniniai nustatymai: pirmo diegimo metu perimami is senu vietu.
$proj = Join-Path $env:USERPROFILE 'Documents\PlatformIO\Projects\TinyMakerWiFi'
$oldInstall = Join-Path $env:USERPROFILE 'Tools\resin-lab'
function Get-Old($rel) {
  $cands = @((Join-Path $oldInstall $rel), (Join-Path $proj $rel))
  $wt = Join-Path $proj '.claude\worktrees'
  if (Test-Path $wt) { $cands += Get-ChildItem -Path $wt -Directory | ForEach-Object { Join-Path $_.FullName $rel } }
  $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
}
foreach ($rel in 'scripts\dev\resin-lab\local.json', 'scripts\dev\local-links.json') {
  $dst = Join-Path $Target $rel
  if (-not (Test-Path $dst)) {
    $old = Get-Old $rel
    if ($old) { Copy-Item -Path $old -Destination $dst; Write-Host "Perimta: $old" }
  }
}

# 4. Python
$py = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path $py) -and -not (Get-Command python -ErrorAction SilentlyContinue)) {
  Write-Warning 'Neradau Python (nei PlatformIO, nei PATH) - serveriai nepasileis.'
}

# 5. Darbastalio nuorodos
$wscript = Join-Path $env:WINDIR 'System32\wscript.exe'
if (-not $NoShortcut) {
  $desk = [Environment]::GetFolderPath('Desktop')
  $ws = New-Object -ComObject WScript.Shell
  $icon = (Join-Path $lab 'resin-lab.ico') + ',0'
  $links = @(
    @('00 TinyMaker dervu testai', (Join-Path $lab 'launcher.vbs'), '', $icon, 'TinyMaker Resin Lab'),
    @('00 TinyMaker dervos', (Join-Path $lab 'launcher.vbs'), 'resin-publish.html', $icon, 'TinyMaker dervu biblioteka'),
    @('00 TinyMaker ukis', (Join-Path $dev 'ukis.vbs'), '', 'shell32.dll,14', 'TinyMaker ukis - irankiu registras')
  )
  foreach ($l in $links) {
    $s = $ws.CreateShortcut((Join-Path $desk ($l[0] + '.lnk')))
    $s.TargetPath = $wscript
    $s.Arguments = ('"' + $l[1] + '" ' + $l[2]).Trim()
    $s.WorkingDirectory = Split-Path $l[1]
    $s.IconLocation = $l[3]
    $s.Description = $l[4]
    $s.Save()
  }
  Write-Host 'Darbastalio nuorodos atnaujintos.'
}

# 6. Serveriai laiko koda atmintyje: senieji stabdomi, Resin Lab paleidziamas is naujo.
if (-not $NoStart) {
  Get-CimInstance Win32_Process |
    Where-Object { $_.CommandLine -like '*resin-lab*server.py*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  foreach ($port in 8898, 8899) {   # ukis: sena kopija galejo tarnauti is repo katalogo
    Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue | ForEach-Object {
      $p = Get-CimInstance Win32_Process -Filter "ProcessId=$($_.OwningProcess)"
      if ($p -and $p.CommandLine -like '*http.server*') { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
    }
  }
  Start-Process -FilePath $wscript -ArgumentList ('"' + (Join-Path $lab 'launcher.vbs') + '"')
}

# 7. Sena vieta (iki 2026-09-19 - Tools\resin-lab): nustatymai jau perimti, failai - tik musu kopija.
# Valoma tik tada, kai nuorodos ir serveriai jau perkelti i nauja vieta.
if (-not $NoShortcut -and -not $NoStart -and ($oldInstall -ne $Target) -and
    (Test-Path (Join-Path $oldInstall 'scripts\dev\resin-lab\launcher.vbs'))) {
  Remove-Item -Recurse -Force $oldInstall
  Write-Host "Sena vieta isvalyta: $oldInstall"
}
Write-Host 'Baigta.'
