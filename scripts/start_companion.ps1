[CmdletBinding()]
param(
    [string]$ListenAddress = "127.0.0.1",
    [ValidateRange(1, 65535)]
    [int]$Port = 8765,
    [string]$DatabasePath,
    [string]$TokenFile,
    [string]$ConfigurePort,
    [string]$DashboardUrl,
    [string]$SerialPort
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $DatabasePath) {
    $DatabasePath = Join-Path $projectRoot "companion\data\dashboard.db"
}
$databasePath = [IO.Path]::GetFullPath($DatabasePath)
$useAuthentication = $ListenAddress -notin @("127.0.0.1", "::1", "localhost") -or
    -not [string]::IsNullOrWhiteSpace($TokenFile)

if ($useAuthentication) {
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
} else {
    # Old shell settings must not restore the removed local credential prompt.
    Remove-Item Env:DASHBOARD_TOKEN -ErrorAction SilentlyContinue
}

if ([bool]$ConfigurePort -ne [bool]$DashboardUrl) {
    throw "-ConfigurePort and -DashboardUrl must be supplied together"
}
if ($SerialPort -and $ConfigurePort) {
    throw "Use -SerialPort for USB sync or -ConfigurePort/-DashboardUrl for HTTP configuration, not both"
}
if ($ConfigurePort) {
    $configureArguments = @((Join-Path $PSScriptRoot "configure_device.py"), "--port", $ConfigurePort, "--url", $DashboardUrl)
    if ($useAuthentication) {
        $configureArguments += @("--token-env", "DASHBOARD_TOKEN")
    } else {
        $configureArguments += "--clear-token"
    }
    & python @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Device configuration failed; companion was not started"
    }
}

Write-Host "Starting Dashboard companion on ${ListenAddress}:$Port"
Write-Host "Using shared Dashboard database: $databasePath"
if ($useAuthentication) {
    Write-Host "Reusing token from: $tokenPath"
} else {
    Write-Host "Local access: no credential required"
}
$serviceArguments = @((Join-Path $projectRoot "companion\dashboard_service.py"), "--host", $ListenAddress, "--port", "$Port", "--db", $databasePath)
if ($SerialPort) {
    $serviceArguments += @("--serial-port", $SerialPort)
}
& python @serviceArguments
exit $LASTEXITCODE
