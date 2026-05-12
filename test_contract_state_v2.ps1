# Test midnight_contractState RPC - Test 1 da tra ve "" - that might mean success!
# Hãy thử decode kết quả và kiểm tra state_call với đúng params

$AddressHex32 = "66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99"
$RpcUrl = "https://rpc.preprod.midnight.network"

Write-Host "=== Test 1: Chính xác như Test 1 (không 0x, không quotes đặc biệt) ===" -ForegroundColor Cyan

# Chính xác giống test 1 - không có 0x prefix
$body1 = @{
    jsonrpc = "2.0"
    id = 1
    method = "midnight_contractState"
    params = @{
        contract_address = $AddressHex32
    }
}

# Debug: xem JSON được tạo ra
$json1 = $body1 | ConvertTo-Json -Depth 10
Write-Host "Request body:" -ForegroundColor Yellow
Write-Host $json1

$result1 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $json1
Write-Host "`nResult:" -ForegroundColor Green
$result1 | ConvertTo-Json -Depth 10

# Thử với base64 decode kết quả
if ($result1.result -and $result1.result.Length -gt 0) {
    Write-Host "`nTrying to decode result as hex..." -ForegroundColor Cyan
    # Result là hex string - thử decode
    try {
        $bytes = [Convert]::FromHexString($result1.result.TrimStart("0x"))
        Write-Host "Decoded bytes length: $($bytes.Length)" -ForegroundColor Yellow
        Write-Host "First 100 bytes as hex: $([BitConverter]::ToString($bytes[0..[Math]::Min(99,$bytes.Length-1)]))" -ForegroundColor Yellow
    } catch {
        Write-Host "Could not decode as hex: $_" -ForegroundColor Red
    }
}

Write-Host "`n=== Test state_call với params đúng (name + data) ===" -ForegroundColor Cyan
$body2 = @{
    jsonrpc = "2.0"
    id = 2
    method = "state_call"
    params = @{
        name = "MidnightRuntimeApi_get_contract_state"
        data = "0x$AddressHex32"
    }
}

$json2 = $body2 | ConvertTo-Json -Depth 10
Write-Host "Request body:" -ForegroundColor Yellow
Write-Host $json2

try {
    $result2 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $json2
    Write-Host "`nResult:" -ForegroundColor Green
    $result2 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "`n=== Test state_call với SCALE-encoded address (0x80 + 32 bytes) ===" -ForegroundColor Cyan
$ScaleData = "0x80$AddressHex32"
Write-Host "SCALE data: $ScaleData" -ForegroundColor Yellow

$body3 = @{
    jsonrpc = "2.0"
    id = 3
    method = "state_call"
    params = @{
        name = "MidnightRuntimeApi_get_contract_state"
        data = $ScaleData
    }
}

$json3 = $body3 | ConvertTo-Json -Depth 10
try {
    $result3 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $json3
    Write-Host "`nResult:" -ForegroundColor Green
    $result3 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Thử kiểm tra xem có RPC method nào hỗ trợ query balance không
Write-Host "`n=== Query available methods ===" -ForegroundColor Cyan
$body4 = @{
    jsonrpc = "2.0"
    id = 99
    method = "rpc_methods"
    params = @{}
}

$result4 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body ($body4 | ConvertTo-Json)
Write-Host "Available methods:" -ForegroundColor Green
$result4 | ConvertTo-Json -Depth 5
