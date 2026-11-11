# Common compatibility checks for portable release (Windows PowerShell 5.1).
$script:ExpectedAmdHash = '046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066'
$script:AmdServiceName = 'AMD External Events Utility'
$script:TaskName = 'AmdAtieclxxRuntimeFix'
function Get-AmdTarget {
    $service = Get-CimInstance Win32_Service -Filter "Name='$script:AmdServiceName'" -ErrorAction Stop
    if (-not $service) { throw 'AMD External Events Utility service not found.' }
    $command = [string]$service.PathName
    $binary = $null
    if ($command -match '^\s*"([^"]+\.exe)"') { $binary = $Matches[1] }
    elseif ($command -match '^\s*(.+?\.exe)(?:\s|$)') { $binary = $Matches[1] }
    if (-not $binary -or -not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "Cannot resolve AMD service executable: $command"
    }
    if ((Split-Path $binary -Leaf) -ine 'atiesrxx.exe') {
        throw "Unexpected AMD service executable: $binary"
    }
    $client = Join-Path (Split-Path -Parent $binary) 'atieclxx.exe'
    if (-not (Test-Path -LiteralPath $client -PathType Leaf)) {
        throw "AMD client executable not found alongside service: $client"
    }
    $hash = (Get-FileHash -LiteralPath $client -Algorithm SHA256).Hash
    $signature = (Get-AuthenticodeSignature -LiteralPath $client).Status.ToString()
    $serviceRegPath = "HKLM:\SYSTEM\CurrentControlSet\Services\$script:AmdServiceName"
    $startReg = (Get-ItemProperty -LiteralPath $serviceRegPath -ErrorAction Stop).Start
    $version = (Get-Item -LiteralPath $client).VersionInfo
    # WHQL-signed AMD clients may be signed by Microsoft's Hardware Compatibility Publisher.
    # Require a valid Authenticode signature AND AMD company/product metadata from the binary,
    # plus an AMD service entry pointing to atiesrxx.exe. Hash is NOT required for diagnosis.
    $amdVendor = ($signature -eq 'Valid' -and [string]$version.CompanyName -match '^AMD\b|Advanced Micro Devices' -and
      [string]$version.ProductName -match 'AMD External Events')
    [pscustomobject]@{
        Service = $service
        ServiceExe = $binary
        ClientExe = $client
        ClientHash = $hash
        Signature = $signature
        CompanyName = [string]$version.CompanyName
        ProductName = [string]$version.ProductName
        DiagnosticEligible = [bool]$amdVendor
        Supported = ($hash -eq $script:ExpectedAmdHash -and $amdVendor)
        StartRegistry = [int]$startReg
    }
}
function Get-CVolumePhysicalDiskCounter {
    # PhysicalDisk counter is a Windows kernel disk-write counter, NOT NAND/TBW.
    # Only select the Windows disk containing C: when a matching instance exists.
    $disks = @(Get-CimInstance Win32_PerfRawData_PerfDisk_PhysicalDisk -ErrorAction Stop |
        Where-Object { $_.Name -match '(^|\s)C:($|\s)' })
    if($disks.Count -eq 1){return $disks[0]}
    return $null
}
function Get-ClientWriteRate([int]$seconds=4) {
    $processes = @(Get-CimInstance Win32_Process -Filter "Name='atieclxx.exe'" -ErrorAction Stop)
    if ($processes.Count -ne 1) {
        return [pscustomobject]@{
            Measured=$false; Reason="atieclxx not running (process count $($processes.Count))"
            RateBps=$null; WriteBytes=$null; Seconds=$null
            DiskMeasured=$false; DiskRateBps=$null; DiskName=$null; DiskWriteBytes=$null
        }
    }
    $p = $processes[0]
    $beforeWrite = [double]$p.WriteTransferCount
    $diskStart=$null
    try { $diskStart=Get-CVolumePhysicalDiskCounter }catch {}
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds $seconds
    $end = Get-CimInstance Win32_Process -Filter "ProcessId=$($p.ProcessId)" -ErrorAction SilentlyContinue
    $diskEnd=$null
    if($diskStart){try{$diskEnd=Get-CVolumePhysicalDiskCounter}catch{}}
    $watch.Stop()
    if(-not $end){
        return [pscustomobject]@{
            Measured=$false;Reason='AMD client exited during measurement'
            RateBps=$null;WriteBytes=$null;Seconds=[math]::Round($watch.Elapsed.TotalSeconds,2)
            DiskMeasured=$false;DiskRateBps=$null;DiskName=$null;DiskWriteBytes=$null
        }
    }
    $delta = [math]::Max(0,([double]$end.WriteTransferCount - $beforeWrite))
    $diskMeasured=$false
    $diskRateBps=$null
    $diskBytes=$null
    $diskName=$null
    if($diskStart -and $diskEnd -and $diskStart.Name -eq $diskEnd.Name) {
        $ticks=[double]$diskEnd.Timestamp_PerfTime-[double]$diskStart.Timestamp_PerfTime
        $freq=[double]$diskEnd.Frequency_PerfTime
        $diskElapsed=$(if($freq -gt 0){$ticks/$freq}else{0})
        $physicalDelta=[double]$diskEnd.DiskWriteBytesPersec-[double]$diskStart.DiskWriteBytesPersec
        if($diskElapsed -gt 0 -and $physicalDelta -ge 0) {
            $diskMeasured=$true
            $diskName=[string]$diskEnd.Name
            $diskBytes=[math]::Round($physicalDelta,0)
            $diskRateBps=[math]::Round($physicalDelta/$diskElapsed,0)
        }
    }
    [pscustomobject]@{
        Measured=$true;Pid=$p.ProcessId;Seconds=[math]::Round($watch.Elapsed.TotalSeconds,2)
        WriteBytes=[math]::Round($delta,0)
        RateBps=[math]::Round($delta/$watch.Elapsed.TotalSeconds,0)
        DiskMeasured=$diskMeasured;DiskRateBps=$diskRateBps;DiskName=$diskName;DiskWriteBytes=$diskBytes
        Reason='Measured process I/O; disk number covers all C: writers, not AMD exclusively'
    }
}
