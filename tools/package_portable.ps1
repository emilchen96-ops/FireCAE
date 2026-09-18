[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [string]$SourceDirectory = (Split-Path -Parent $PSScriptRoot),
    [string]$Version = '0.3.0',
    [string]$QtLicenseDirectory = 'E:\Qt\Licenses',
    [string]$OpenCascadeDirectory = 'D:\occt-vc7.8.0\occt-vc143-64',
    [switch]$SkipStartupTest,
    [string]$AcceptanceReportDirectory = ''
)

$ErrorActionPreference = 'Stop'

function Resolve-ExistingDirectory([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label does not exist: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Copy-RequiredItem([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Required portable-package input is missing: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

$sourceRoot = Resolve-ExistingDirectory $SourceDirectory 'Source directory'
$buildRoot = Resolve-ExistingDirectory $BuildDirectory 'Build directory'
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$packageName = "FireCAE-$Version-windows-x64"
$stageRoot = [IO.Path]::GetFullPath((Join-Path $outputRoot $packageName))
$relativeStage = [IO.Path]::GetRelativePath($outputRoot, $stageRoot)
if ($relativeStage.StartsWith('..') -or [IO.Path]::IsPathRooted($relativeStage)) {
    throw "Refusing to stage outside the requested output directory: $stageRoot"
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null

Copy-RequiredItem (Join-Path $buildRoot 'FireCAE.exe') $stageRoot
Copy-RequiredItem (Join-Path $buildRoot 'vc_redist.x64.exe') $stageRoot
Get-ChildItem -LiteralPath $buildRoot -File -Filter '*.dll' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $stageRoot -Force
}
$manifest = Join-Path $buildRoot 'FireCAE.exe.manifest'
if (Test-Path -LiteralPath $manifest) {
    Copy-Item -LiteralPath $manifest -Destination $stageRoot -Force
}

$runtimeDirectories = @(
    'platforms', 'styles', 'iconengines', 'imageformats', 'generic',
    'networkinformation', 'tls', 'fds', 'smokeview', 'ifcopenshell', 'translations'
)
foreach ($directory in $runtimeDirectories) {
    Copy-RequiredItem (Join-Path $buildRoot $directory) (Join-Path $stageRoot $directory)
}

$docsTarget = Join-Path $stageRoot 'docs'
New-Item -ItemType Directory -Path $docsTarget -Force | Out-Null
Copy-RequiredItem (Join-Path $sourceRoot 'docs\PORTABLE_README.zh-CN.md') (Join-Path $stageRoot 'README.md')
foreach ($document in @('docs\FDS_TUTORIAL_CASES.md',
                         'docs\GUI_END_TO_END_ACCEPTANCE.md')) {
    Copy-RequiredItem (Join-Path $sourceRoot $document) $docsTarget
}

if (-not [string]::IsNullOrWhiteSpace($AcceptanceReportDirectory)) {
    $acceptanceRoot = Resolve-ExistingDirectory $AcceptanceReportDirectory 'Acceptance report directory'
    $acceptanceTarget = Join-Path $docsTarget 'acceptance'
    New-Item -ItemType Directory -Path $acceptanceTarget -Force | Out-Null
    # Keep the portable package limited to readable reports and manifests.
    # Raw logs, images, projects and solver results remain in the external
    # evidence directory; never recurse through that potentially large tree.
    $reports = @(Get-ChildItem -LiteralPath $acceptanceRoot -File |
        Where-Object { $_.Extension -in @('.md', '.csv') } |
        Sort-Object Name)
    foreach ($report in $reports) {
        Copy-RequiredItem $report.FullName (Join-Path $acceptanceTarget $report.Name)
    }
    $externalEvidence = $acceptanceRoot.Replace('\', '/')
    $index = @(
        '# 便携包验收报告索引',
        '',
        '此目录仅包含打包时已有的顶层 Markdown 报告和 CSV 清单；文件是否通过验收，以报告中的明确状态为准。缺少最终总结或未执行项不能视为通过。',
        '',
        "完整外部证据目录（未包含在本 ZIP 中）：[交付证据目录](<$externalEvidence>)。", 
        '',
        '原始日志、截图、工程、计算输出及修复补丁在上述独立目录。移交时应同时提供该目录或交付总索引中指明的证据副本；更换机器或移动目录后，应按交付总索引查找，不依赖原构建机路径。',
        '',
        '本目录报告中的原始绝对路径和未随包复制的相对链接指向外部证据。它们是追溯信息，不是便携程序的运行依赖。',
        '',
        '## 随包报告',
        ''
    )
    foreach ($report in $reports) {
        $target = [Uri]::EscapeDataString($report.Name)
        $index += "- [$($report.Name)](./$target)"
    }
    if ($reports.Count -eq 0) { $index += '指定目录当前没有顶层 Markdown 或 CSV 报告。' }
    $index | Set-Content -LiteralPath (Join-Path $acceptanceTarget 'PORTABLE_REPORT_INDEX.md') -Encoding UTF8
}

$examplesTarget = Join-Path $stageRoot 'examples\fds-tutorials'
New-Item -ItemType Directory -Path $examplesTarget -Force | Out-Null
$tutorialRoot = Join-Path $sourceRoot 'tests\data\fds-tutorials'
Get-ChildItem -LiteralPath $tutorialRoot -File -Recurse -Filter '*.fds' | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($tutorialRoot, $_.FullName)
    $destination = Join-Path $examplesTarget $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
}

$licensesTarget = Join-Path $stageRoot 'licenses'
New-Item -ItemType Directory -Path $licensesTarget -Force | Out-Null
Copy-RequiredItem (Join-Path $sourceRoot 'licenses\FDS-LICENSE.md') $licensesTarget
Copy-RequiredItem (Join-Path $sourceRoot 'licenses\SMOKEVIEW-LICENSE.md') $licensesTarget
Copy-RequiredItem (Join-Path $sourceRoot 'licenses\THIRD_PARTY_NOTICES.md') $licensesTarget

$ifcLicenses = Join-Path $licensesTarget 'IfcOpenShell'
New-Item -ItemType Directory -Path $ifcLicenses -Force | Out-Null
foreach ($name in @('COPYING', 'COPYING.LESSER', 'NOTICE.md')) {
    Copy-RequiredItem (Join-Path $sourceRoot "third_party\ifcopenshell\$name") $ifcLicenses
}

$occtRoot = Resolve-ExistingDirectory $OpenCascadeDirectory 'OpenCascade directory'
$occtLicenses = Join-Path $licensesTarget 'OpenCascade'
New-Item -ItemType Directory -Path $occtLicenses -Force | Out-Null
foreach ($name in @('LICENSE_LGPL_21.txt', 'OCCT_LGPL_EXCEPTION.txt')) {
    Copy-RequiredItem (Join-Path $occtRoot $name) $occtLicenses
}

$qtRoot = Resolve-ExistingDirectory $QtLicenseDirectory 'Qt license directory'
Copy-RequiredItem $qtRoot (Join-Path $licensesTarget 'Qt')

$fileEntries = Get-ChildItem -LiteralPath $stageRoot -File -Recurse | ForEach-Object {
    [ordered]@{
        path = [IO.Path]::GetRelativePath($stageRoot, $_.FullName).Replace('\', '/')
        size = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$packageManifest = [ordered]@{
    formatVersion = 1
    product = 'FireCAE'
    version = $Version
    platform = 'windows-x64'
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    resultsEmbeddedByDefault = $false
    fileCount = @($fileEntries).Count
    files = @($fileEntries)
}
$packageManifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $stageRoot 'package-manifest.json') -Encoding UTF8

if (-not $SkipStartupTest) {
    $savedPath = $env:PATH
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    try {
        $process = Start-Process -FilePath (Join-Path $stageRoot 'FireCAE.exe') `
            -ArgumentList '--startup-smoke' -WorkingDirectory $stageRoot `
            -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit(30000)) {
            Stop-Process -Id $process.Id -Force
            throw 'Portable startup smoke test exceeded 30 seconds.'
        }
        if ($process.ExitCode -ne 0) {
            throw "Portable startup smoke test failed with exit code $($process.ExitCode)."
        }
    }
    finally {
        $env:PATH = $savedPath
    }
}

$archivePath = Join-Path $outputRoot "$packageName.zip"
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}
Compress-Archive -LiteralPath $stageRoot -DestinationPath $archivePath -CompressionLevel Optimal

Write-Output "Portable directory: $stageRoot"
Write-Output "Portable archive: $archivePath"
Write-Output "Files: $(@($fileEntries).Count)"
