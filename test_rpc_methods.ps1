$rpc = 'https://rpc.preprod.midnight.network'

Write-Host "=== RPC methods list ===" -ForegroundColor Cyan
$body = @{
    jsonrpc = '2.0'
    id = 1
    method = 'rpc_methods'
} | ConvertTo-Json -Compress

try {
    $response = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 15
    if ($response.result) {
        Write-Host "Methods:" -ForegroundColor Green
        $response.result.methods | ForEach-Object { Write-Host "  $_" }
    } else {
        Write-Host "Response: $($response | ConvertTo-Json)" -ForegroundColor Green
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Testing midnight_decodeAddress ===" -ForegroundColor Cyan
$body2 = @{
    jsonrpc = '2.0'
    id = 2
    method = 'midnight_decodeAddress'
    params = @{
        address = 'mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q'
    }
} | ConvertTo-Json -Compress

try {
    $r2 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body2 -TimeoutSec 15
    Write-Host "Result: $($r2 | ConvertTo-Json)" -ForegroundColor Green
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Testing chain_getHeader ===" -ForegroundColor Cyan
$body3 = @{
    jsonrpc = '2.0'
    id = 3
    method = 'chain_getHeader'
} | ConvertTo-Json -Compress

try {
    $r3 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body3 -TimeoutSec 15
    Write-Host "Result:" -ForegroundColor Green
    $r3 | ConvertTo-Json -Depth 5
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Testing state_queryStorageAt with unshielded public key ===" -ForegroundColor Cyan
# The unshielded address public key is 32 bytes
# Let's construct the storage key properly
# For MidnightLedger free balance, we need to compute storage key

# First get the hash of "MidnightLedger"
$moduleHash = "9fc1e81e3da0a9c6e05f8791e6e9a24f"  # twox128("MidnightLedger")

# Try with a simple storage query
$body4 = @{
    jsonrpc = '2.0'
    id = 4
    method = 'state_queryStorageAt'
    params = @(@('0x0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'))
} | ConvertTo-Json -Compress

try {
    $r4 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body4 -TimeoutSec 15
    Write-Host "Result:" -ForegroundColor Green
    $r4 | ConvertTo-Json -Depth 5
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}
