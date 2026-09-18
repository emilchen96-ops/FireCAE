param(
    [string]$BuildDirectory = "D:\FireCAE\build\release",
    [string]$EvidenceDirectory = "D:\FireCAE\docs\acceptance\pyrosim-workflow\logs"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$fireCae = Join-Path $BuildDirectory "FireCAE.exe"
$uiTests = Join-Path $BuildDirectory "FireCAEUiTests.exe"
$runnerTests = Join-Path $BuildDirectory "FireCAEFdsRunnerTests.exe"
foreach ($required in @($fireCae, $uiTests, $runnerTests)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required P27 executable is missing: $required"
    }
}

New-Item -ItemType Directory -Force -Path $EvidenceDirectory | Out-Null
$processNames = @("fds", "fds_openmp", "mpiexec", "smokeview")
$baseline = @{}
foreach ($name in $processNames) {
    $baseline[$name] = @(Get-Process -Name $name -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Id })
}

$records = [System.Collections.Generic.List[object]]::new()

function Invoke-P27Iteration {
    param(
        [string]$Category,
        [int]$Iteration,
        [string]$Executable,
        [string[]]$Arguments
    )

    $started = Get-Date
    $argumentText = $Arguments -join " "
    if ($Arguments.Count -gt 0) {
        $process = Start-Process -FilePath $Executable -ArgumentList $Arguments `
            -WorkingDirectory $BuildDirectory -Wait -PassThru
    } else {
        $process = Start-Process -FilePath $Executable `
            -WorkingDirectory $BuildDirectory -Wait -PassThru
    }
    Start-Sleep -Milliseconds 250

    $unexpected = [System.Collections.Generic.List[string]]::new()
    foreach ($name in $processNames) {
        foreach ($candidate in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
            if ($baseline[$name] -notcontains $candidate.Id) {
                $unexpected.Add("${name}:$($candidate.Id)")
            }
        }
    }

    $finished = Get-Date
    $passed = $process.ExitCode -eq 0 -and $unexpected.Count -eq 0
    $records.Add([pscustomobject]@{
        Category = $Category
        Iteration = $Iteration
        Command = "`"$Executable`" $argumentText".Trim()
        Started = $started.ToString("o")
        DurationSeconds = [Math]::Round(($finished - $started).TotalSeconds, 3)
        ExitCode = $process.ExitCode
        UnexpectedProcesses = $unexpected -join ";"
        Result = if ($passed) { "PASS" } else { "FAIL" }
    })
}

for ($iteration = 1; $iteration -le 10; ++$iteration) {
    Invoke-P27Iteration "production-startup-close" $iteration $fireCae @("--startup-smoke")
}

for ($iteration = 1; $iteration -le 10; ++$iteration) {
    Invoke-P27Iteration "new-save-open-reopen" $iteration $uiTests @()
}

for ($iteration = 1; $iteration -le 5; ++$iteration) {
    Invoke-P27Iteration "ifc-import-cancel" $iteration $uiTests @("--p22-import-maturity-smoke")
}

for ($iteration = 1; $iteration -le 5; ++$iteration) {
    Invoke-P27Iteration "solver-start-stop" $iteration $runnerTests @()
}

for ($iteration = 1; $iteration -le 5; ++$iteration) {
    Invoke-P27Iteration "results-load-play-close" $iteration $uiTests @("--p24-results-maturity-smoke")
}

$csvPath = Join-Path $EvidenceDirectory "P27-stability-iterations.csv"
$records | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

$summary = $records | Group-Object Category | ForEach-Object {
    [pscustomobject]@{
        Category = $_.Name
        Iterations = $_.Count
        Passed = @($_.Group | Where-Object Result -eq "PASS").Count
        Failed = @($_.Group | Where-Object Result -ne "PASS").Count
        TotalSeconds = [Math]::Round(($_.Group | Measure-Object DurationSeconds -Sum).Sum, 3)
    }
}
$summaryPath = Join-Path $EvidenceDirectory "P27-stability-summary.csv"
$summary | Export-Csv -LiteralPath $summaryPath -NoTypeInformation -Encoding UTF8
$summary | Format-Table -AutoSize

if (@($records | Where-Object Result -ne "PASS").Count -ne 0) {
    throw "P27 stability loop failed. Inspect $csvPath"
}

Write-Host "P27 stability loop passed. Evidence: $summaryPath"
