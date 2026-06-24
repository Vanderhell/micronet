param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = 'Stop'

function Write-GuardError {
    param([string]$Message)
    Write-Host $Message
    exit 1
}

Set-Location $RepoRoot

$tracked = git ls-files
$trackedViolations = New-Object System.Collections.Generic.List[string]
foreach ($path in $tracked) {
    if ($path -match '(^|/)(build_ninja|build|build_[^/]+)(/|$)') {
        $trackedViolations.Add($path)
        continue
    }
    if ($path -match '^tools/MicronetViz/(bin|obj)/') {
        $trackedViolations.Add($path)
        continue
    }
    if ($path -match '(^|/)(CMakeCache\.txt|CMakeFiles|CTestTestfile\.cmake|cmake_install\.cmake|compile_commands\.json|build\.ninja|\.ninja_[^/]+)$') {
        $trackedViolations.Add($path)
        continue
    }
    if ($path -match '(^|/)secrets\.h$' -and $path -notmatch '\.example$') {
        $trackedViolations.Add($path)
        continue
    }
}

if ($trackedViolations.Count -gt 0) {
    Write-GuardError ("Tracked build or secret paths found:`n" + ($trackedViolations -join "`n"))
}

$absolutePathPattern = [regex]'(?i)([A-Z]:\\|/Users/|/home/|/mnt/|C:/Users/)'
$activeBuildPatterns = @(
    '(^|/)(build|build_[^/]+)(/|$)',
    '^tools/MicronetViz/(bin|obj)/',
    '(^|/)(managed_components|\.idf)(/|$)',
    '(^|/)(CMakeCache\.txt|CMakeFiles|CTestTestfile\.cmake|cmake_install\.cmake|compile_commands\.json|build\.ninja|project_description\.json|flasher_args\.json|sdkconfig|sdkconfig\.old|dependencies\.lock)$'
)

$buildViolations = New-Object System.Collections.Generic.List[string]
Get-ChildItem -LiteralPath $RepoRoot -Recurse -File -Force | ForEach-Object {
    $relative = $_.FullName.Substring($RepoRoot.Length).TrimStart('\', '/')
    $normalized = $relative -replace '\\', '/'
    $matchesBuildPath = $false
    foreach ($pattern in $activeBuildPatterns) {
        if ($normalized -match $pattern) {
            $matchesBuildPath = $true
            break
        }
    }
    if (-not $matchesBuildPath) {
        return
    }

    $content = $null
    try {
        $content = Get-Content -LiteralPath $_.FullName -Raw
    } catch {
        return
    }

    if ($content -match $absolutePathPattern) {
        $buildViolations.Add($normalized)
    }
}

if ($buildViolations.Count -gt 0) {
    Write-GuardError ("Active build files contain absolute local paths:`n" + ($buildViolations -join "`n"))
}

$definedTests = New-Object System.Collections.Generic.HashSet[string]
$registeredTests = New-Object System.Collections.Generic.HashSet[string]
Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'tests') -Filter '*.c' -File | ForEach-Object {
    foreach ($line in Get-Content -LiteralPath $_.FullName) {
        if ($line -match '^\s*MTEST\(([^)]+)\)') {
            [void]$definedTests.Add($Matches[1])
        }
        if ($line -match '^\s*MTEST_RUN\(([^)]+)\)') {
            [void]$registeredTests.Add($Matches[1])
        }
    }
}

$deadTests = @()
foreach ($name in $definedTests) {
    if (-not $registeredTests.Contains($name)) {
        $deadTests += $name
    }
}

if ($deadTests.Count -gt 0) {
    $deadTestsText = ($deadTests | Sort-Object -Unique) -join "`n"
    Write-GuardError ("Defined tests not added to a suite:`n" + $deadTestsText)
}

Write-Host "Repository guard passed."
