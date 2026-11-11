[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$payloadDir=Join-Path $root 'Distribution\payload'
$dist=Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $payloadDir,$dist|Out-Null
Write-Host '[1/2] Compilando monitor autossuficiente (.NET 8 / win-x64)'
& dotnet publish (Join-Path $root 'AmdAtieclxxRuntimeFix.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=None -p:DebugSymbols=false -o $payloadDir --nologo
if($LASTEXITCODE -ne 0){throw 'Monitor build failed'}
$monitor=Join-Path $payloadDir 'AmdAtieclxxRuntimeFix.exe'
if(!(Test-Path $monitor)){throw 'Monitor executable missing'}
Write-Host '[2/2] Compilando GUI WPF autossuficiente'
& dotnet publish (Join-Path $root 'SetupGui\AtieclxxSetup.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -p:DebugType=None -p:DebugSymbols=false -o $dist --nologo
if($LASTEXITCODE -ne 0){throw 'GUI build failed'}
$src=Join-Path $dist 'AMD-atieclxx-Setup.exe'
$dst=Join-Path $dist 'AMD-atieclxx-Fix-Setup.exe'
if(!(Test-Path $src)){throw 'GUI executable missing'}
Move-Item -LiteralPath $src -Destination $dst -Force
[pscustomobject]@{File=$dst;Bytes=(Get-Item $dst).Length;SHA256=(Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash}|ConvertTo-Json -Compress
