# Installs the latest obs-soundboard build, replacing any stale/conflicting
# copy first. Run as Administrator (writes to Program Files).
#
# Usage:
#   irm https://raw.githubusercontent.com/chrisgrimm-jm/obs-soundboard/main/install.ps1 | iex

$ErrorActionPreference = "Stop"

$pluginDir = "C:\Program Files\obs-studio\obs-plugins\64bit"
$dataDir = "$pluginDir\data\obs-plugins\obs-soundboard"
$tmp = Join-Path $env:TEMP "obs-soundboard-install"

Write-Host "Closing OBS if it's running..."
Get-Process "obs64" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

Write-Host "Removing any existing obs-soundboard install..."
Remove-Item "$pluginDir\obs-soundboard.dll" -Force -ErrorAction SilentlyContinue
Remove-Item "$pluginDir\obs-soundboard.pdb" -Force -ErrorAction SilentlyContinue
Remove-Item $dataDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$env:APPDATA\obs-studio\plugins\obs-soundboard.dll" -Force -ErrorAction SilentlyContinue
Remove-Item "$env:APPDATA\obs-studio\plugins\obs-soundboard" -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "Downloading the latest build..."
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $tmp | Out-Null
$zipPath = Join-Path $tmp "obs-soundboard.zip"
$asset = (Invoke-RestMethod "https://api.github.com/repos/chrisgrimm-jm/obs-soundboard/releases/tags/dev-build").assets |
	Where-Object { $_.name -like "*windows-x64.zip" } | Select-Object -First 1
Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath
Expand-Archive -Path $zipPath -DestinationPath $tmp -Force

Write-Host "Installing..."
New-Item -ItemType Directory -Path $dataDir\locale -Force | Out-Null
Copy-Item "$tmp\obs-soundboard\bin\64bit\obs-soundboard.dll" $pluginDir -Force
Copy-Item "$tmp\obs-soundboard\bin\64bit\obs-soundboard.pdb" $pluginDir -Force
Copy-Item "$tmp\obs-soundboard\data\locale\en-US.ini" "$dataDir\locale\en-US.ini" -Force

Write-Host ""
Write-Host "Done. Start OBS and open Docks -> Soundboard." -ForegroundColor Green
Write-Host "You'll know it's the right one: a grid of dark sound buttons with STOP ALL / Settings on top," -ForegroundColor Green
Write-Host "and right-clicking a button opens a Clip Settings window (not a dropdown menu)." -ForegroundColor Green
