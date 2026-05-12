# Check if faucet transaction was confirmed and look for UTXOs

$RpcUrl = "https://rpc.preprod.midnight.network"

# User's address hex
$UserAddressHex = "66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99"

Write-Host "=== Check user's UTXOs via midnight_contractState ===" -ForegroundColor Cyan

# Test with user's exact address (no 0x prefix, no quotes issue)
$body = @{
    jsonrpc = "2.0"
    id = 1
    method = "midnight_contractState"
    params = @{
        contract_address = $UserAddressHex
    }
}

$result = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body ($body | ConvertTo-Json -Depth 10)

Write-Host "Raw result: '$($result.result)'" -ForegroundColor Yellow
Write-Host "Result length: $(if($result.result) { $result.result.Length } else { 0 })" -ForegroundColor Yellow

# Try decoding result as hex
if ($result.result -and $result.result.Length -gt 0) {
    Write-Host "`nTrying to decode result as hex..." -ForegroundColor Cyan
    try {
        $hex = $result.result.Trim('"')
        Write-Host "Trimmed hex: '$hex'" -ForegroundColor Yellow
        if ($hex -and $hex.Length -gt 0) {
            $bytes = [Convert]::FromHexString($hex)
            Write-Host "Decoded length: $($bytes.Length)" -ForegroundColor Green
        }
    } catch {
        Write-Host "Decode error: $_" -ForegroundColor Red
    }
}

# Try with SCALE-encoded Vec<u8> in params
Write-Host "`n=== Try with SCALE-encoded Vec<u8> (0x80 + 32 bytes) ===" -ForegroundColor Cyan
$body2 = @{
    jsonrpc = "2.0"
    id = 2
    method = "midnight_contractState"
    params = @{
        contract_address = "80$UserAddressHex"
    }
}

$result2 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body ($body2 | ConvertTo-Json -Depth 10)

Write-Host "Raw result: '$($result2.result)'" -ForegroundColor Yellow
Write-Host "Result length: $(if($result2.result) { $result2.result.Length } else { 0 })" -ForegroundColor Yellow

# Try decoding result as base64
if ($result.result -and $result.result.Length -gt 0) {
    Write-Host "`nTrying to decode result as base64..." -ForegroundColor Cyan
    try {
        $b64 = $result.result.Trim('"')
        $bytes = [Convert]::FromBase64String($b64)
        Write-Host "Base64 decoded length: $($bytes.Length)" -ForegroundColor Green
        Write-Host "First 64 bytes hex: $([BitConverter]::ToString($bytes[0..63]) -replace '-')" -ForegroundColor Yellow
    } catch {
        Write-Host "Base64 decode error: $_" -ForegroundColor Red
    }
}

# Check chain head to see current block
Write-Host "`n=== Check current block ===" -ForegroundColor Cyan
$body3 = @{
    jsonrpc = "2.0"
    id = 3
    method = "chain_getHeader"
    params = @{}
}

$result3 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body ($body3 | ConvertTo-Json -Depth 10)
Write-Host "Current header: " -ForegroundColor Green -NoNewline
$result3 | ConvertTo-Json -Depth 5

# Try archive_v1_call to query contract state
Write-Host "`n=== Try archive_v1_call ===" -ForegroundColor Cyan
$body4 = @{
    jsonrpc = "2.0"
    id = 4
    method = "archive_v1_call"
    params = @{
        function = "midnight_contractState"
        call_parameters = "0x$UserAddressHex"
    }
}

try {
    $result4 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body ($body4 | ConvertTo-Json -Depth 10)
    Write-Host "archive_v1_call result: " -ForegroundColor Green -NoNewline
    $result4 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "archive_v1_call error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try archive_v1_storage
Write-Host "`n=== Try archive_v1_storage ===" -ForegroundColor Cyan
$body5 = @{
    jsonrpc = "2.0"
    id = 5
    method = "archive_v1_storage"
    params = @{
        key = "0x$UserAddressHex"
    }
}

try {
    $result5 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body ($body5 | ConvertTo-Json -Depth 10)
    Write-Host "archive_v1_storage result: " -ForegroundColor Green -NoNewline
    $result5 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "archive_v1_storage error: $($_.Exception.Message)" -ForegroundColor Red
}
