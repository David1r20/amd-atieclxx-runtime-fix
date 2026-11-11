[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$target=Get-AmdTarget
$task=Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue
$monitor=Join-Path $env:ProgramFiles 'AmdAtieclxxRuntimeFix\AmdAtieclxxRuntimeFix.exe'
$patch=$null
if(Test-Path $monitor){
 $proc=Start-Process -FilePath $monitor -ArgumentList '--status' -PassThru -Wait -NoNewWindow
 $file=Join-Path $env:ProgramData 'AmdAtieclxxRuntimeFix\status.json'
 if(Test-Path $file){$patch=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json}
}
[pscustomobject]@{
 Supported=$target.Supported
 VendorFileSHA256=$target.ClientHash
 VendorSignature=$target.Signature
 ServiceState=$target.Service.State
 ServiceStartup=$target.Service.StartMode
 Task=$(if($task){$task.State.ToString()}else{'Not installed'})
 PatchStates=@($patch.Processes | ForEach-Object{$_.State})
 PatchedProcesses=@($patch.Processes | Where-Object State -eq 'Patched').Count
} | ConvertTo-Json -Depth 4 -Compress
