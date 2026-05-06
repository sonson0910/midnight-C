# Read mnemonic from environment variable (never hardcode secrets)
$MNEMONIC = $env:MIDNIGHT_MNEMONIC
if ([string]::IsNullOrEmpty($MNEMONIC)) {
    Write-Error "MIDNIGHT_MNEMONIC environment variable is not set"
    exit 1
}

$ErrorActionPreference = "Stop"
$exePath = "d:\venera\midnight\night_fund\.cmake-build\manual\bin\e2e_transfer_night.exe"

Write-Host "=== Realtime WebSocket Balance Tracker ===" -ForegroundColor Cyan
Write-Host "Address: mn_addr_preview175jkk5czqytflpppr8xd38yv0zhy7n0hla6qq2y8vt9zny6rqvqqgsrgkv" -ForegroundColor Gray
Write-Host "WebSocket: wss://indexer.preview.midnight.network/api/v4/graphql" -ForegroundColor Gray
Write-Host "Will track UTXO events for 20 seconds..." -ForegroundColor Yellow
Write-Host ""

try {
    $proc = Start-Process -FilePath $exePath -ArgumentList @(
        "--mnemonic", $MNEMONIC,
        "--realtime"
    ) -PassThru -NoNewWindow

    # Wait 20 seconds then kill
    Start-Sleep -Seconds 20

    if (!$proc.HasExited) {
        Write-Host ""
        Write-Host "=== 20 seconds elapsed, stopping ===" -ForegroundColor Cyan
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }

    if ($proc.ExitCode -ne $null -and $proc.ExitCode -ne 0) {
        Write-Host "Exit code: $($proc.ExitCode)" -ForegroundColor Red
    }
} catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
}
