param(
    [string]$Root = "D:\FireCAE",
    [string]$EvidenceRoot = "D:\FireCAE\docs\acceptance\pyrosim-workflow"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$directories = @(
    "screenshots",
    "screenshots\seven-tutorials-gui",
    "screenshots\seven-tutorials-results",
    "exported-fds",
    "projects",
    "results",
    "results\seven-tutorials",
    "comparisons",
    "logs"
)
foreach ($relative in $directories) {
    New-Item -ItemType Directory -Force -Path (Join-Path $EvidenceRoot $relative) |
        Out-Null
}

Copy-Item -LiteralPath (Join-Path $Root "docs\PYROSIM_2023_WORKFLOW_MATRIX.md") `
    -Destination (Join-Path $EvidenceRoot "PYROSIM_2023_WORKFLOW_MATRIX.md") -Force

$firstFire = Join-Path $EvidenceRoot "results\first_fire"
Copy-Item -LiteralPath (Join-Path $firstFire "first_fire.firecae") `
    -Destination (Join-Path $EvidenceRoot "projects\first_fire.firecae") -Force
Copy-Item -LiteralPath (Join-Path $firstFire "first_fire.fds") `
    -Destination (Join-Path $EvidenceRoot "exported-fds\first_fire.fds") -Force

$complexIfc = Join-Path $EvidenceRoot "results\complex-ifc"
if (Test-Path -LiteralPath (Join-Path $complexIfc "p27_complex_ifc.firecae")) {
    Copy-Item -LiteralPath (Join-Path $complexIfc "p27_complex_ifc.firecae") `
        -Destination (Join-Path $EvidenceRoot "projects\p27_complex_ifc.firecae") -Force
    Copy-Item -LiteralPath (Join-Path $complexIfc "p27_complex_ifc.fds") `
        -Destination (Join-Path $EvidenceRoot "exported-fds\p27_complex_ifc.fds") -Force
}

$cases = @(
    "activate_vents",
    "bucket_test_2",
    "couch",
    "couch_smoke_12s",
    "HVAC_aircoil",
    "tunnel_demo",
    "tunnel_smoke_10s"
)
foreach ($case in $cases) {
    $guiDirectory = Join-Path $Root "docs\acceptance\tutorials\blank-gui\$case"
    Copy-Item -LiteralPath (Join-Path $guiDirectory "$case.firecae") `
        -Destination (Join-Path $EvidenceRoot "projects\$case.firecae") -Force
    Copy-Item -LiteralPath (Join-Path $guiDirectory "$case.fds") `
        -Destination (Join-Path $EvidenceRoot "exported-fds\$case.fds") -Force

    $sourceResult = Join-Path $Root "tests\data\gui-generated\$case"
    $resultLink = Join-Path $EvidenceRoot "results\seven-tutorials\$case"
    if (-not (Test-Path -LiteralPath $resultLink)) {
        New-Item -ItemType Junction -Path $resultLink -Target $sourceResult | Out-Null
    }

    $comparisonSource = Join-Path $Root "docs\acceptance\comparisons\$case"
    $comparisonDestination = Join-Path $EvidenceRoot "comparisons\$case"
    New-Item -ItemType Directory -Force -Path $comparisonDestination | Out-Null
    Copy-Item -Path (Join-Path $comparisonSource "*") `
        -Destination $comparisonDestination -Recurse -Force
}

$blankGuiScreens = Join-Path $Root "docs\acceptance\tutorials\blank-gui\screenshots"
if (Test-Path -LiteralPath $blankGuiScreens) {
    Copy-Item -Path (Join-Path $blankGuiScreens "*") `
        -Destination (Join-Path $EvidenceRoot "screenshots\seven-tutorials-gui") `
        -Recurse -Force
}
$resultScreens = Join-Path $Root "docs\acceptance\screenshots\Smokeview-seven-tutorials"
if (Test-Path -LiteralPath $resultScreens) {
    Copy-Item -Path (Join-Path $resultScreens "*") `
        -Destination (Join-Path $EvidenceRoot "screenshots\seven-tutorials-results") `
        -Recurse -Force
}

$plumeComparison = Join-Path $Root "docs\acceptance\comparisons\plume_average"
if (Test-Path -LiteralPath $plumeComparison) {
    $plumeDestination = Join-Path $EvidenceRoot "comparisons\plume_average"
    New-Item -ItemType Directory -Force -Path $plumeDestination | Out-Null
    Copy-Item -Path (Join-Path $plumeComparison "*") `
        -Destination $plumeDestination -Recurse -Force
}

$manifestFiles = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
foreach ($relative in @("projects", "exported-fds", "comparisons", "screenshots")) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $EvidenceRoot $relative) `
                                   -Recurse -File) {
        $manifestFiles.Add($file)
    }
}
foreach ($file in Get-ChildItem -LiteralPath $firstFire -File) {
    $manifestFiles.Add($file)
}
if (Test-Path -LiteralPath $complexIfc) {
    foreach ($file in Get-ChildItem -LiteralPath $complexIfc -File) {
        $manifestFiles.Add($file)
    }
}
foreach ($file in Get-ChildItem -LiteralPath $EvidenceRoot -Filter "*.md" -File) {
    $manifestFiles.Add($file)
}
foreach ($case in $cases) {
    $sourceResult = Join-Path $Root "tests\data\gui-generated\$case"
    foreach ($name in @("$case.fds", "$case.firecae", "$case.out", "$case.smv")) {
        $path = Join-Path $sourceResult $name
        if (Test-Path -LiteralPath $path) {
            $manifestFiles.Add((Get-Item -LiteralPath $path))
        }
    }
}

$manifest = foreach ($file in $manifestFiles | Sort-Object FullName -Unique) {
    $hash = Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256
    [pscustomobject]@{
        Path = $file.FullName
        Bytes = $file.Length
        SHA256 = $hash.Hash
        LastWriteTime = $file.LastWriteTime.ToString("o")
    }
}
$manifestPath = Join-Path $EvidenceRoot "logs\P27-evidence-manifest.csv"
$manifest | Export-Csv -LiteralPath $manifestPath -NoTypeInformation -Encoding UTF8

Write-Host "P27 evidence collected: $($manifest.Count) hashed files"
Write-Host "Manifest: $manifestPath"
