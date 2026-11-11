#Requires -RunAsAdministrator
[CmdletBinding()]
param([switch]$AllowServiceRestart)
$ErrorActionPreference='Stop'
if(-not $AllowServiceRestart){throw 'Explicit opt-in required: -AllowServiceRestart'}
$task='AmdAtieclxxRuntimeFix'
$service='AMD External Events Utility'
$fix=Join-Path $env:ProgramFiles 'AmdAtieclxxRuntimeFix\AmdAtieclxxRuntimeFix.exe'
$amd='C:\Windows\System32\DriverStore\FileRepository\u0205306.inf_amd64_ed4bdb51fc506ec7\B026676\atieclxx.exe'
$expected='046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066'
if((Get-FileHash -LiteralPath $amd).Hash -ne $expected){throw 'Unsupported AMD driver build'}
if((Get-ScheduledTask -TaskName $task).State -ne 'Running'){throw 'Watchdog must be running'}
$s=Get-CimInstance Win32_Service -Filter "Name='$service'"
if($s.State -ne 'Running' -or $s.StartMode -ne 'Auto'){throw 'AMD service must be Running / Auto'}
$ok=$false
try{
 Stop-Service -Name $service -ErrorAction Stop
 Start-Sleep -Milliseconds 600
 Start-Service -Name $service -ErrorAction Stop
 Start-Sleep -Seconds 3
 $pr=Get-CimInstance Win32_Process -Filter "Name='atieclxx.exe'" | Select-Object -First 1
 if(-not $pr){throw 'AMD client missing after service restart'}
 $p=Start-Process -FilePath $fix -ArgumentList '--status' -Wait -NoNewWindow -PassThru
 $st=Get-Content -LiteralPath (Join-Path $env:ProgramData 'AmdAtieclxxRuntimeFix\status.json') -Raw | ConvertFrom-Json
 if($p.ExitCode -ne 0 -or $st.Processes[0].State -ne 'Patched') {throw 'Patch was not applied'}
 $a=[double](Get-CimInstance Win32_Process -Filter "ProcessId=$($pr.ProcessId)").WriteTransferCount
 Start-Sleep -Seconds 7
 $b=[double](Get-CimInstance Win32_Process -Filter "ProcessId=$($pr.ProcessId)").WriteTransferCount
 $delta=$b-$a
 if($delta -gt 262144){throw "Excessive writes $delta bytes in 7 seconds"}
 $ok=$true
 [pscustomobject]@{Status='PASS';Pid=$pr.ProcessId;PatchState=$st.Processes[0].State;WriteBytes7s=$delta;TaskState=(Get-ScheduledTask -TaskName $task).State.ToString()} | ConvertTo-Json -Compress
}
finally{
 if(-not $ok){
  Stop-Service -Name $service -ErrorAction SilentlyContinue
  Set-Service -Name $service -StartupType Disabled -ErrorAction SilentlyContinue
  Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
  Write-Warning 'SAFETY ROLLBACK: service stopped/disabled and watchdog task stopped.'
 }
}