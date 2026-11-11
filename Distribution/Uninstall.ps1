#Requires -RunAsAdministrator
[CmdletBinding()]
param([switch]$RestoreOriginalRunningState)
$ErrorActionPreference='Stop'
$service='AMD External Events Utility'
$task='AmdAtieclxxRuntimeFix'
$state=Join-Path $env:ProgramData 'AmdAtieclxxRuntimeFix\installation.json'
$saved=$null
if(Test-Path -LiteralPath $state){$saved=Get-Content -LiteralPath $state -Raw|ConvertFrom-Json}
if(Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue){
  Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 1
  Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction Stop
}
Stop-Service -Name $service -ErrorAction SilentlyContinue
# Safe-by-default: no runtime patch means no unprotected self-copy writes.
Set-Service -Name $service -StartupType Disabled
if($RestoreOriginalRunningState -and $saved) {
   switch([string]$saved.OriginalStartMode) {
      'Auto' {Set-Service -Name $service -StartupType Automatic}
      'Manual' {Set-Service -Name $service -StartupType Manual}
      'Disabled' {Set-Service -Name $service -StartupType Disabled}
      default {throw 'Unsupported saved startup type'}
   }
   if([string]$saved.OriginalServiceState -eq 'Running') {
      Start-Service -Name $service
   }
}
$serviceNow=Get-CimInstance Win32_Service -Filter "Name='$service'"
[pscustomobject]@{
 Result='REMOVED'
 TaskAbsent=(-not [bool](Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue))
 ServiceState=$serviceNow.State
 ServiceStartup=$serviceNow.StartMode
 RestorationMode=$(if($RestoreOriginalRunningState){'Previous configuration restored on request; bug may resume'}else{'Conservative: disabled to prevent SSD writes'})
 InstallationFilesPreserved=$true
} | ConvertTo-Json -Compress
# Installation files and forensic logs are retained to enable auditing.
