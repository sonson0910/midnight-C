# Read mnemonic from environment variable (never hardcode secrets)
$MNEMONIC = $env:MIDNIGHT_MNEMONIC
if ([string]::IsNullOrEmpty($MNEMONIC)) {
    Write-Error "MIDNIGHT_MNEMONIC environment variable is not set"
    exit 1
}

$exe = "d:\venera\midnight\night_fund\.cmake-build\manual\bin\e2e_transfer_night.exe"

Write-Host "`n=== STEP 1: CHECK NIGHT BALANCE ===`n" -ForegroundColor Cyan
& $exe --mnemonic $MNEMONIC --balance

Write-Host "`n`n=== STEP 2: REQUEST FAUCET ===`n" -ForegroundColor Cyan
& $exe --mnemonic $MNEMONIC --faucet

Write-Host "`n`n=== STEP 3: WAIT 20s FOR CONFIRMATION ===`n" -ForegroundColor Yellow
Start-Sleep -Seconds 20

Write-Host "`n`n=== STEP 4: CHECK BALANCE AFTER FAUCET ===`n" -ForegroundColor Cyan
& $exe --mnemonic $MNEMONIC --balance
