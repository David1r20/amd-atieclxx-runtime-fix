#Requires -RunAsAdministrator
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$target=$null
$serviceOriginal=$null
$taskCreated=$false
$changedService=$false
$installationDir=Join-Path $env:ProgramFiles 'AmdAtieclxxRuntimeFix'
$stateDir=Join-Path $env:ProgramData 'AmdAtieclxxRuntimeFix'
$packageExe=Join-Path $PSScriptRoot 'AmdAtieclxxRuntimeFix.exe'
$installedExe=Join-Path $installationDir 'AmdAtieclxxRuntimeFix.exe'
$stateFile=Join-Path $stateDir 'installation.json'
try {
    # Perform all compatibility checks BEFORE stopping services or writing files.
    $target=Get-AmdTarget
    if(-not $target.Supported) { throw "Unsupported signed AMD client: SHA256=$($target.ClientHash) signature=$($target.Signature)" }
    if(-not (Test-Path -LiteralPath $packageExe -PathType Leaf)){throw 'Missing packaged monitor executable'}
    if((Get-Item $packageExe).Length -lt 1000000){ throw 'Expected self-contained monitor; packaged executable too small' }
    if(Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue){throw 'Correção já instalada. Use o botão Desinstalar da interface antes de instalar outra versão.'}
    # The uninstaller deliberately preserves installation.json for auditing.
    # If no task exists, a new install may archive that previous state safely.
    if(Test-Path -LiteralPath $stateFile){
        $archive = Join-Path $stateDir ('installation.previous.' + (Get-Date -Format 'yyyyMMddHHmmss') + '.json')
        Move-Item -LiteralPath $stateFile -Destination $archive -ErrorAction Stop
    }
    $serviceOriginal=$target.Service
    $originalRunning=($serviceOriginal.State -eq 'Running')
    if($originalRunning) {
        $beforeRate = Get-ClientWriteRate 4
        if($beforeRate.Measured -and ([double]$beforeRate.RateBps -lt 150000)) {
            throw "No excessive I/O detected on current AMD client ($($beforeRate.RateBps) B/s). Refusing unnecessary memory modification."
        }
    }
    $originalStartMode=[string]$serviceOriginal.StartMode
    $originalStartReg=[int]$target.StartRegistry
    $serviceReg="HKLM:\SYSTEM\CurrentControlSet\Services\$script:AmdServiceName"
    $delayed=(Get-ItemProperty -LiteralPath $serviceReg -Name DelayedAutoStart -ErrorAction SilentlyContinue).DelayedAutoStart

    # Stop the old client briefly. Task is started before the service resumes.
    if($originalRunning) {
       Stop-Service -Name $script:AmdServiceName -ErrorAction Stop
       $changedService=$true
    }
    New-Item -ItemType Directory -Path $installationDir,$stateDir -Force|Out-Null
    Copy-Item -LiteralPath $packageExe -Destination $installedExe -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Uninstall.ps1') -Destination (Join-Path $installationDir 'Uninstall.ps1') -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Verify.ps1') -Destination (Join-Path $installationDir 'Verify.ps1') -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Common.ps1') -Destination (Join-Path $installationDir 'Common.ps1') -Force
    # Program Files must not be writable by standard users.
    $acl=Get-Acl -LiteralPath $installationDir
    foreach($entry in $acl.Access) {
       if($entry.AccessControlType -eq 'Allow' -and
          "$($entry.IdentityReference)" -match '(?i)(BUILTIN\\Users|BUILTIN\\Usuários|Authenticated Users|Usuários Autenticados|Everyone|Todos)' -and
          ($entry.FileSystemRights -band [Security.AccessControl.FileSystemRights]::Write) -ne 0) {
          throw 'Unsafe installation folder ACL: standard user write access.'
       }
    }
    [pscustomobject]@{
        InstalledOn=(Get-Date).ToString('o')
        OriginalServiceState=[string]$serviceOriginal.State
        OriginalStartMode=$originalStartMode
        OriginalStartValue=$originalStartReg
        OriginalDelayedAutoStart=$delayed
        AMDExecutablePath=$target.ClientExe
        AMDExecutableHash=$target.ClientHash
        MonitorSha256=(Get-FileHash -LiteralPath $installedExe).Hash
        TaskName=$script:TaskName
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $stateFile -Encoding UTF8

    $principal=New-ScheduledTaskPrincipal -UserId 'S-1-5-18' -LogonType ServiceAccount -RunLevel Highest
    $action=New-ScheduledTaskAction -Execute $installedExe -Argument '--watch' -WorkingDirectory $installationDir
    $trigger=New-ScheduledTaskTrigger -AtStartup
    $settings=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    Register-ScheduledTask -TaskName $script:TaskName -Principal $principal -Action $action -Trigger $trigger -Settings $settings -Description 'Version-pinned AMD atieclxx runtime memory fix' -ErrorAction Stop | Out-Null
    $taskCreated=$true
    Start-ScheduledTask -TaskName $script:TaskName -ErrorAction Stop
    $monitorStarted=$false
    for($i=0;$i -lt 30;$i++){
        Start-Sleep -Milliseconds 200
        if((Get-ScheduledTask -TaskName $script:TaskName).State -eq 'Running'){$monitorStarted=$true;break}
    }
    if(-not $monitorStarted){throw 'Monitor failed to start as SYSTEM'}

    Set-Service -Name $script:AmdServiceName -StartupType Automatic
    $changedService=$true
    Start-Service -Name $script:AmdServiceName -ErrorAction Stop
    Start-Sleep -Seconds 4

    $client=@(Get-CimInstance Win32_Process -Filter "Name='atieclxx.exe'")
    if($client.Count -ne 1){throw "Expected one AMD client, found $($client.Count)"}
    $status=Start-Process -FilePath $installedExe -ArgumentList '--status' -PassThru -Wait -NoNewWindow
    if($status.ExitCode -ne 0){throw "Monitor status check failed code=$($status.ExitCode)"}
    $result=Get-Content -LiteralPath (Join-Path $stateDir 'status.json') -Raw | ConvertFrom-Json
    if($result.Processes[0].State -ne 'Patched' -or $result.Processes[0].Opcode -ne 'B80100000090'){throw 'Patch not confirmed on AMD client'}
    $pidNow=[int]$client[0].ProcessId
    $start=(Get-CimInstance Win32_Process -Filter "ProcessId=$pidNow").WriteTransferCount
    Start-Sleep -Seconds 6
    $end=Get-CimInstance Win32_Process -Filter "ProcessId=$pidNow"
    if(-not $end){throw 'Client exited unexpectedly'}
    $writes=[double]$end.WriteTransferCount-[double]$start
    if($writes -gt 262144){throw "Excessive writes despite patch: $writes bytes in 6 seconds"}
    [pscustomobject]@{
        Result='INSTALLED_AND_TESTED'
        Task=(Get-ScheduledTask -TaskName $script:TaskName).State.ToString()
        AMDService=(Get-CimInstance Win32_Service -Filter "Name='$script:AmdServiceName'").State
        ClientPid=$pidNow
        Patched=$result.Processes[0].State
        AdditionalWriteBytes6s=$writes
        OriginalAMDFilePreserved=((Get-FileHash $target.ClientExe).Hash -eq $script:ExpectedAmdHash)
        MonitorHash=(Get-FileHash $installedExe).Hash
    }|ConvertTo-Json -Compress
}
catch {
    $reason=$_.Exception.Message
    if($taskCreated){
        Stop-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $script:TaskName -Confirm:$false -ErrorAction SilentlyContinue
    }
    if($changedService){
       # Fail safe: never leave an unpatched, high-I/O AMD client running after failure.
       Stop-Service -Name $script:AmdServiceName -ErrorAction SilentlyContinue
       Set-Service -Name $script:AmdServiceName -StartupType Disabled -ErrorAction SilentlyContinue
    }
    [pscustomobject]@{
      Result='INSTALL_FAILED'
      Reason=$reason
      ServiceState=$(if($target){(Get-CimInstance Win32_Service -Filter "Name='$script:AmdServiceName'").State}else{'Unchanged'})
      FailSafe='Stopped and disabled if installation had already altered the service; previous service settings saved if state file was written'
    } | ConvertTo-Json -Compress
    exit 1
}
