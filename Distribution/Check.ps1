[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
try {
    $t=Get-AmdTarget
    $rate=if($t.DiagnosticEligible){Get-ClientWriteRate 3}else{[pscustomobject]@{Measured=$false;Reason='AMD signature/vendor not verified';RateBps=$null;DiskMeasured=$false;DiskRateBps=$null}}
    $installed = [bool](Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue)
    $detected = $rate.Measured -and ([double]$rate.RateBps -gt 150000)
    $recommendation = if (-not $t.DiagnosticEligible) {'Untrusted driver identity: diagnostic refused'}
                      elseif (-not $t.Supported -and $detected) {'Excessive AMD writes detected; diagnosis available, no patch validated for this hash'}
                      elseif (-not $t.Supported) {'AMD signature verified; measurement available, memory patch not supported'}
                      elseif ($installed) {'Already installed: use status command'}
                      elseif ($rate.Measured -and (-not $detected)) {'No excessive writes detected: installation unnecessary'}
                      elseif ($detected) {'Excessive process writes detected: installation candidate'}
                      else {'Not measurable: investigate before installing'}
    $result=[pscustomobject]@{
        Check='AMD atieclxx compatibility'
        Compatible=$t.Supported
        DiagnosticEligible=$t.DiagnosticEligible
        VerifiedVendor=$t.DiagnosticEligible
        ExcessiveWriteDetected=$detected
        Recommendation=$recommendation
        Reason=$(if($t.Supported){'Patch validated for this exact AMD executable hash'}
                 elseif($t.DiagnosticEligible){'Authentic AMD executable; hash not validated for the patch'}
                 else{'AMD signature or vendor identity not verified'})
        VendorHash=$t.ClientHash
        CompanyName=$t.CompanyName
        ProductName=$t.ProductName
        Signed=$t.Signature
        AMDService=$t.Service.State
        AMDStartup=$t.Service.StartMode
        ExistingRuntimeFix=$installed
        ProcessWriteSample=$rate
        WouldModifySystem=$false
    }
    $result | ConvertTo-Json -Depth 4 -Compress
    if($t.DiagnosticEligible){ exit 0 } else { exit 2 }
}
catch {
    [pscustomobject]@{Compatible=$false;Error=$_.Exception.Message;WouldModifySystem=$false}|ConvertTo-Json -Compress
    exit 3
}