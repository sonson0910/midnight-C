#!/usr/bin/env pwsh
# Decode the faucet transaction from block 748878

$RPC_URL = "https://rpc.preprod.midnight.network"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Decoding Faucet Extrinsics" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Extrinsic 2 is likely the faucet call - it's the long one
$FAUCET_CALL = "0x280501000b7080621c9e01"
$FAUCET_CALL2 = "0xd0050d0000a6f27ebdd7f1fd2b03134619e805e77533898699a9f5664c8bb1982c0d4950f36db24700e80d6f199e01000002000000"

Write-Host "[1] Decoding faucet call..." -ForegroundColor Yellow
Write-Host "Call data: $FAUCET_CALL"
Write-Host "Length: $($FAUCET_CALL.Length) chars"
Write-Host ""

# Try state_call to decode the call
$decodeBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "state_call"
    params = @{
        name = "Metadata_metadata"
        bytes = $FAUCET_CALL.Substring(2)
    }
} | ConvertTo-Json -Depth 10

try {
    $decodeResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $decodeBody -TimeoutSec 30
    Write-Host "Metadata decode: $($decodeResponse | ConvertTo-Json -Depth 10)"
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "[2] Trying to parse call structure..." -ForegroundColor Yellow

# Parse the hex manually
$hex = $FAUCET_CALL.Substring(2)
Write-Host "Hex: $hex"
Write-Host "First byte: $($hex.Substring(0,2)) - module index"
Write-Host "Second byte: $($hex.Substring(2,2)) - call index"

# Try to decode as Transaction API call
Write-Host ""
Write-Host "[3] Checking transaction API..." -ForegroundColor Yellow

$txBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "state_call"
    params = @{
        name = "TransactionAPI_verify_transaction"
        bytes = "0xd0050d0000a6f27ebdd7f1fd2b03134619e805e77533898699a9f5664c8bb1982c0d4950f36db24700e80d6f199e01000002000000"
    }
} | ConvertTo-Json -Depth 10

try {
    $txResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $txBody -TimeoutSec 30
    Write-Host "Transaction API: $($txResponse | ConvertTo-Json -Depth 10)"
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "[4] Raw decode of second extrinsic..." -ForegroundColor Yellow
$hex2 = "0xd0050d0000a6f27ebdd7f1fd2b03134619e805e77533898699a9f5664c8bb1982c0d4950f36db24700e80d6f199e01000002000000"
Write-Host "Length: $($hex2.Length) chars"
Write-Host ""

# Parse compact integer for amount
# 0xd0050d00 = prefix + compact encoded amount
# Let's extract potential address and amount

Write-Host "First 4 bytes (module/call): $($hex2.Substring(0,8))"
Write-Host "Next bytes might be: recipient address + amount"

# Try midnight runtime API
Write-Host ""
Write-Host "[5] Trying MidnightRuntimeApi..." -ForegroundColor Yellow

$rtBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "state_call"
    params = @{
        name = "MidnightRuntimeApi_get_contract_state"
        bytes = "0x" + "66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99"
    }
} | ConvertTo-Json -Depth 10

try {
    $rtResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $rtBody -TimeoutSec 30
    Write-Host "Runtime API Response: $($rtResponse | ConvertTo-Json -Depth 10)"
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
