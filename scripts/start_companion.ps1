[CmdletBinding()]
param(
    [string]$ListenAddress = "0.0.0.0",
    [ValidateRange(1, 65535)]
    [int]$Port = 8765,
    [string]$TokenFile,
    [string]$ConfigurePort,
    [string]$DashboardUrl
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $TokenFile) {
    $TokenFile = Join-Path $projectRoot "config\dashboard.token"
}
$tokenPath = [IO.Path]::GetFullPath($TokenFile)

if (Test-Path -LiteralPath $tokenPath -PathType Leaf) {
    $dashboardToken = (Get-Content -LiteralPath $tokenPath -Raw).Trim()
} else {
    $tokenDirectory = Split-Path -Parent $tokenPath
    [IO.Directory]::CreateDirectory($tokenDirectory) | Out-Null
    $randomBytes = New-Object byte[] 32
    $randomGenerator = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $randomGenerator.GetBytes($randomBytes)
    } finally {
        $randomGenerator.Dispose()
    }
    $dashboardToken = -join ($randomBytes | ForEach-Object { $_.ToString("x2") })
    $utf8NoBom = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($tokenPath, $dashboardToken, $utf8NoBom)
    Write-Host "Created local Dashboard token file: $tokenPath"
}

if (
    [string]::IsNullOrWhiteSpace($dashboardToken) -or
    $dashboardToken.Length -gt 192 -or
    $dashboardToken -match "\s"
) {
    throw "Dashboard token file is empty, too long, or contains whitespace: $tokenPath"
}
$env:DASHBOARD_TOKEN = $dashboardToken

if ([bool]$ConfigurePort -ne [bool]$DashboardUrl) {
    throw "-ConfigurePort and -DashboardUrl must be supplied together"
}
if ($ConfigurePort) {
    & python (Join-Path $PSScriptRoot "configure_device.py") `
        --port $ConfigurePort `
        --url $DashboardUrl `
        --token-env DASHBOARD_TOKEN
    if ($LASTEXITCODE -ne 0) {
        throw "Device configuration failed; companion was not started"
    }
}

Write-Host "Starting Dashboard companion on ${ListenAddress}:$Port"
Write-Host "Reusing token from: $tokenPath"
& python (Join-Path $projectRoot "companion\dashboard_service.py") `
    --host $ListenAddress `
    --port $Port
exit $LASTEXITCODE
