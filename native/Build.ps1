# Native Win64 C++ portable EXE; no .NET / PowerShell at runtime.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$repo=Split-Path -Parent $root
$out=Join-Path $repo 'dist-native'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$exe=Join-Path $out 'AMD-atieclxx-Fix-Native.exe'
$obj=Join-Path $out 'native.obj'
$src=Join-Path $root 'main.cpp'
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs=$(if(Test-Path $vswhere){(& $vswhere -latest -products '*' -property installationPath|Select-Object -First 1)}else{$null})
if(-not $vs){ throw 'Visual Studio C++ Build Tools not found' }
$tools=Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
if(-not $tools){throw 'MSVC installation missing'}
$msvc=$tools.FullName
$kits='C:\Program Files (x86)\Windows Kits\10'
$ver=Get-ChildItem (Join-Path $kits 'Lib') -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name
if(-not $ver){throw 'Windows SDK missing'}
$env:INCLUDE=@("$msvc\include","$kits\Include\$ver\ucrt","$kits\Include\$ver\shared","$kits\Include\$ver\um","$kits\Include\$ver\winrt") -join ';'
$env:LIB=@("$msvc\lib\x64","$kits\Lib\$ver\ucrt\x64","$kits\Lib\$ver\um\x64") -join ';'
$cl=Get-Command cl.exe -ErrorAction SilentlyContinue
if(-not $cl){$cl=Get-Item (Join-Path $msvc 'bin\Hostx86\x64\cl.exe') -ErrorAction SilentlyContinue}
if($cl){
 $compiler=$cl.Source
 if(-not $compiler){$compiler=$cl.FullName}
 $args=@('/nologo','/std:c++17','/O2','/MT','/EHsc','/utf-8','/W3','/DUNICODE','/D_UNICODE',
          "/Fo$obj","/Fe$exe",$src,'/link','/SUBSYSTEM:WINDOWS','/MACHINE:X64',
          '/OPT:REF','/OPT:ICF','/INCREMENTAL:NO')
}else{
 $compiler='C:\Program Files\AMD\ROCm\6.4\bin\clang-cl.exe'
 if(-not(Test-Path $compiler)){throw 'Neither cl.exe nor clang-cl.exe available'}
 $args=@('/nologo','/std:c++17','/O2','/MT','/EHsc','/utf-8','/W3','/DUNICODE','/D_UNICODE',
          "/Fo$obj","/Fe$exe",$src,'/link','/SUBSYSTEM:WINDOWS','/MACHINE:X64',
          '/OPT:REF','/OPT:ICF','/INCREMENTAL:NO',"/LIBPATH:$msvc\lib\x64",
          "/LIBPATH:$kits\Lib\$ver\ucrt\x64","/LIBPATH:$kits\Lib\$ver\um\x64")
}
$ErrorActionPreference='Continue'
& $compiler @args 2>&1 | ForEach-Object {"$_"}
$exit=$LASTEXITCODE
$ErrorActionPreference='Stop'
if($exit -ne 0){throw "Native compile failed with $exit"}
if(!(Test-Path $exe)){throw "Missing output $exe"}
$size=(Get-Item $exe).Length
if($size -gt 5MB){throw "Oversized native executable $size; expected under 5MiB"}
[pscustomobject]@{Output=$exe;Bytes=$size;SHA256=(Get-FileHash -LiteralPath $exe).Hash;Compiler=$compiler}|ConvertTo-Json -Compress
