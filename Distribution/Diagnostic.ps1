#Requires -RunAsAdministrator
# Short, reversible diagnosis for AMD service currently stopped.
# NEVER installs a patch, modifies AMD binaries, or leaves a formerly stopped service running.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$target=$null
$initialMode=$null
$initialState=$null
$initialDelayed=$null
$serviceChanged=$false
$serviceStarted=$false
$measurement=$null
$errorMessage=$null
$restored=$false
$restoreError=$null
try {
    $target=Get-AmdTarget
    if(-not $target.DiagnosticEligible){
        throw "Identidade do cliente AMD nao verificada (assinatura/fornecedor). O diagnostico nao iniciara este servico."
    }
    if(Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue){
        throw 'A correção já possui tarefa registrada. Não execute um teste sem patch enquanto o monitor está instalado.'
    }
    $initialMode=[string]$target.Service.StartMode
    $initialState=[string]$target.Service.State
    $regPath="HKLM:\SYSTEM\CurrentControlSet\Services\$script:AmdServiceName"
    $originalDelayed=(Get-ItemProperty -LiteralPath $regPath -Name DelayedAutoStart -ErrorAction SilentlyContinue)
    if($originalDelayed){$initialDelayed=[int]$originalDelayed.DelayedAutoStart}
    if($initialState -eq 'Stopped'){
        if($initialMode -eq 'Disabled'){
            # Enable only for the temporary measurement, not permanently.
            Set-Service -Name $script:AmdServiceName -StartupType Manual -ErrorAction Stop
            $serviceChanged=$true
        }
        Start-Service -Name $script:AmdServiceName -ErrorAction Stop
        $serviceStarted=$true
    } elseif($initialState -ne 'Running') {
        throw "Estado inicial do serviço não suportado para diagnóstico: $initialState"
    }
    # Service may start the client with a short delay.
    for($i=0;$i -lt 20;$i++) {
        if(Get-CimInstance Win32_Process -Filter "Name='atieclxx.exe'" -ErrorAction SilentlyContinue){break}
        Start-Sleep -Milliseconds 200
    }
    $measurement=Get-ClientWriteRate 5
    if(-not $measurement.Measured){throw "Não foi possível obter a escrita de atieclxx: $($measurement.Reason)"}
}
catch { $errorMessage=$_.Exception.Message }
finally {
    try {
        if($serviceStarted){
            Stop-Service -Name $script:AmdServiceName -ErrorAction Stop
        }
        if($serviceChanged) {
            switch($initialMode){
                'Disabled' {Set-Service -Name $script:AmdServiceName -StartupType Disabled -ErrorAction Stop}
                'Auto'     {Set-Service -Name $script:AmdServiceName -StartupType Automatic -ErrorAction Stop}
                'Manual'   {Set-Service -Name $script:AmdServiceName -StartupType Manual -ErrorAction Stop}
                default    {throw "Modo inicial desconhecido $initialMode"}
            }
            if($null -ne $initialDelayed) {
                Set-ItemProperty -LiteralPath $regPath -Name DelayedAutoStart -Value $initialDelayed -ErrorAction Stop
            }
        }
        $actual=Get-CimInstance Win32_Service -Filter "Name='$script:AmdServiceName'"
        $restored=(($null -ne $initialMode) -and
          ([string]$actual.StartMode -eq $initialMode) -and
          ([string]$actual.State -eq $initialState))
        if(-not $restored){$restoreError="Estado esperado $initialState/$initialMode; obtido $($actual.State)/$($actual.StartMode)"}
    } catch {
        $restoreError=$_.Exception.Message
    }
}
$success=($null -eq $errorMessage -and $measurement -and $measurement.Measured -and $restored)
$result=[pscustomobject]@{
    Result=$(if($success){'DIAGNOSIS_OK'}else{'DIAGNOSIS_FAILED'})
    Measured=[bool]($measurement -and $measurement.Measured)
    ProcessWriteSample=$measurement
    ExcessiveWriteDetected=[bool]($measurement -and $measurement.Measured -and [double]$measurement.RateBps -gt 150000)
    MeasurementType='Process logical I/O plus total Windows physical-disk C: write counter'
    WasServiceTemporarilyStarted=$serviceStarted
    OriginalServiceState=$initialState
    OriginalServiceStartMode=$initialMode
    Restored=$restored
    RestoreError=$restoreError
    Error=$errorMessage
    ActualServiceState=$(if($actual){$actual.State}else{$null})
    ActualServiceStartMode=$(if($actual){$actual.StartMode}else{$null})
    DriverHash=$(if($target){$target.ClientHash}else{$null})
    PatchSupported=$(if($target){$target.Supported}else{$false})
    DiagnosticEligible=$(if($target){$target.DiagnosticEligible}else{$false})
    AMDImageUnmodified=$(if($target){((Get-FileHash $target.ClientExe -Algorithm SHA256).Hash -eq $target.ClientHash)}else{$null})
}
$result|ConvertTo-Json -Depth 5 -Compress
if(-not $success){ exit 1 }
