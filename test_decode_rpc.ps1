#!/usr/bin/env pwsh
# Decode the preprod address and test RPC

$ADDRESS = "mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q"
$RPC_URL = "https://rpc.preprod.midnight.network"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Decoding Address and Testing RPC" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Run decode_address.exe
Write-Host "[Step 1] Decoding address using C++..." -ForegroundColor Yellow
$decodeExe = "D:\venera\midnight\night_fund\.cmake-build\manual\bin\decode_address.exe"
$decodeOutput = & $decodeExe $ADDRESS 2>&1
Write-Host $decodeOutput
Write-Host ""

# Extract hex value - match "Hex (32 bytes): 0x" followed by hex chars until newline
$lines = $decodeOutput -split "`n"
foreach ($line in $lines) {
    if ($line -match "Hex.*0x([a-f0-9]+)") {
        $hexBytes = $matches[1]
        Write-Host "Extracted hex (no 0x): $hexBytes" -ForegroundColor Green
        Write-Host "Hex length: $($hexBytes.Length) characters" -ForegroundColor Green
        break
    }
}

if ($hexBytes) {
    # Step 2: Test RPC with the decoded hex
    Write-Host ""
    Write-Host "[Step 2] Testing RPC with decoded hex..." -ForegroundColor Yellow
    
    $body = @{
        jsonrpc = "2.0"
        id = 1
        method = "midnight_contractState"
        params = @{
            contract_address = $hexBytes
        }
    } | ConvertTo-Json -Depth 10
    
    try {
        $response = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $body -TimeoutSec 30
        $responseJson = $response | ConvertTo-Json -Depth 10
        Write-Host "Response: $responseJson" -ForegroundColor Cyan
    } catch {
        Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    }
} else {
    Write-Host "Could not extract hex from decode output" -ForegroundColor Red
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
