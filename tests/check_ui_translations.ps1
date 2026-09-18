param(
    [string]$SourceRoot = (Join-Path $PSScriptRoot '..')
)

$ErrorActionPreference = 'Stop'
$sourcePath = (Resolve-Path -LiteralPath $SourceRoot).Path
$languageFile = Join-Path $sourcePath 'src/ui/UiLanguage.cpp'
$languageSource = Get-Content -LiteralPath $languageFile -Raw -Encoding UTF8
$dictionaryStart = $languageSource.IndexOf('const QHash<QString, QString>& chineseTranslations()', [StringComparison]::Ordinal)
if ($dictionaryStart -lt 0) { throw 'Chinese translation dictionary not found.' }
$dictionarySource = $languageSource.Substring($dictionaryStart)

$entryPattern = '\{QStringLiteral\("((?:[^"\\]|\\.)*)"\),\s*QStringLiteral\("((?:[^"\\]|\\.)*)"\)\}'
$entries = [regex]::Matches($dictionarySource, $entryPattern)
$known = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$issues = [Collections.Generic.List[string]]::new()
foreach ($entry in $entries) {
    $key = $entry.Groups[1].Value
    if (-not $known.Add($key)) { $issues.Add("Duplicate translation key: $key") }
    $sourcePlaceholders = ([regex]::Matches($key, '%\d+') |
        ForEach-Object Value | Sort-Object) -join ','
    $targetPlaceholders = ([regex]::Matches($entry.Groups[2].Value, '%\d+') |
        ForEach-Object Value | Sort-Object) -join ','
    if ($sourcePlaceholders -cne $targetPlaceholders) {
        $issues.Add("Translation placeholder mismatch: $key")
    }
}

# This is a deliberately limited static gate: literal application-UI calls,
# including C++ adjacent string literals. It does not pretend to parse runtime
# errors, backend log messages, schemas, generated text or external Smokeview.
$callPattern = '\b(?:uiText|u|t|trText|UiLanguageManager::text)\(\s*(?:QStringLiteral\()?\s*((?:"(?:[^"\\]|\\.)*"\s*)+)'
$literalPattern = '"((?:[^"\\]|\\.)*)"'
$files = @(Get-ChildItem -LiteralPath (Join-Path $sourcePath 'src/ui') -Filter '*.cpp' |
    Where-Object Name -ne 'UiLanguage.cpp') + @(Get-Item -LiteralPath (Join-Path $sourcePath 'src/app/MainWindow.cpp'))
$callCount = 0
foreach ($file in $files) {
    $source = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
    foreach ($call in [regex]::Matches($source, $callPattern)) {
        $callCount++
        $key = ([regex]::Matches($call.Groups[1].Value, $literalPattern) |
            ForEach-Object { $_.Groups[1].Value }) -join ''
        if (-not $known.Contains($key)) {
            $line = ([regex]::Matches($source.Substring(0, $call.Index), "`n")).Count + 1
            $issues.Add("Missing translation at $($file.Name):${line}: $key")
        }
    }
}

Write-Output "UI files checked: $($files.Count)"
Write-Output "Static translation calls checked: $callCount"
Write-Output "Chinese dictionary entries: $($known.Count)"
if ($issues.Count) {
    $issues | Sort-Object -Unique | Write-Output
    exit 1
}
Write-Output 'PASS: no missing static keys, duplicate dictionary keys or placeholder mismatches.'
Write-Output 'Scope excludes runtime backend diagnostics, user data and external Smokeview UI.'
exit 0
