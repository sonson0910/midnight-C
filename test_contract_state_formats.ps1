# Test midnight_contractState RPC với nhiều định dạng address

# Địa chỉ ví preprod của user
$Address = "mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q"

# RPC endpoint
$RpcUrl = "https://rpc.preprod.midnight.network"

# Format 1: 32-byte hex (không có 0x prefix) - định dạng untagged
$AddressHex32 = "66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99"

Write-Host "=== Test 1: 32-byte hex (khong 0x prefix) ===" -ForegroundColor Cyan
$body1 = @{
    jsonrpc = "2.0"
    id = 1
    method = "midnight_contractState"
    params = @{
        contract_address = $AddressHex32
    }
} | ConvertTo-Json -Depth 10

try {
    $result1 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body1
    Write-Host "Result: " -ForegroundColor Green -NoNewline
    $result1 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "`n=== Test 2: 32-byte hex (có 0x prefix) ===" -ForegroundColor Cyan
$body2 = @{
    jsonrpc = "2.0"
    id = 2
    method = "midnight_contractState"
    params = @{
        contract_address = "0x$AddressHex32"
    }
} | ConvertTo-Json -Depth 10

try {
    $result2 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body2
    Write-Host "Result: " -ForegroundColor Green -NoNewline
    $result2 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Format 3: SCALE-encoded Vec<u8> = compact(32) + 32 bytes = 0x9001 + 32 bytes
$ScalePrefix = "9001"  # 32 in compact encoding (32 << 2 = 128 = 0x80... wait, 32 in compact is different)
# Compact encoding for 32: (32 << 2) | 0 = 128 = 0x80
# But SCALE uses: 32 = 0x8001 (actually 32 in compact = 0x80, but 32 as u16 = 0x2000)
# Let me recalculate: compact encode 32 = 0x8001? No...
# For compact: if value < 64, encode as (value << 2) | mode00
# 32 << 2 = 128 = 0x80

$ScalePrefix = "80"  # 32 in compact mode00 = (32 << 2) | 0 = 0x80
$ScaleEncoded = "0x$ScalePrefix$AddressHex32"

Write-Host "`n=== Test 3: SCALE-encoded Vec<u8> (0x80 + 32 bytes) ===" -ForegroundColor Cyan
$body3 = @{
    jsonrpc = "2.0"
    id = 3
    method = "midnight_contractState"
    params = @{
        contract_address = $ScaleEncoded
    }
} | ConvertTo-Json -Depth 10

try {
    $result3 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body3
    Write-Host "Result: " -ForegroundColor Green -NoNewline
    $result3 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Format 4: Double SCALE - encode the address as Vec<u8> first, then pass to state_call
# For state_call, we need: method_name (String) + data (Bytes)
Write-Host "`n=== Test 4: Via state_call với MidnightRuntimeApi_get_contract_state ===" -ForegroundColor Cyan

# Method name: MidnightRuntimeApi_get_contract_state
$MethodNameHex = "4d69646e6967687452756e74696d654170695f6765745f636f6e74726163745f7374617465"  # hex of "MidnightRuntimeApi_get_contract_state"

# SCALE encode Vec<u8> = compact length (0x80 for 32) + 32 bytes
$DataHex = "80$AddressHex32"

$stateCallData = "0x$DataHex"

Write-Host "state_call data: $stateCallData" -ForegroundColor Yellow

$body4 = @{
    jsonrpc = "2.0"
    id = 4
    method = "state_call"
    params = @{
        method = "MidnightRuntimeApi_get_contract_state"
        data = $stateCallData
    }
} | ConvertTo-Json -Depth 10

try {
    $result4 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body4
    Write-Host "Result: " -ForegroundColor Green -NoNewline
    $result4 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Format 5: state_call với contract_address như JSON param (string hex)
Write-Host "`n=== Test 5: state_call với params object ===" -ForegroundColor Cyan
$body5 = @{
    jsonrpc = "2.0"
    id = 5
    method = "state_call"
    params = @{
        method = "MidnightRuntimeApi_get_contract_state"
        data = "0x$AddressHex32"  # Just the 32 bytes, no SCALE prefix
    }
} | ConvertTo-Json -Depth 10

try {
    $result5 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body5
    Write-Host "Result: " -ForegroundColor Green -NoNewline
    $result5 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}
