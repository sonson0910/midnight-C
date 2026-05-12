$rpc = 'https://rpc.preprod.midnight.network'

# First decode the Bech32m address to get the raw bytes
# mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q
# The raw bytes should be 32 bytes of public key

# Let's try with the hex format directly - use a known contract address
# Or try to get it via state_getMetadata first

Write-Host "=== Getting chain metadata ===" -ForegroundColor Cyan
$body0 = @{
    jsonrpc = '2.0'
    id = 1
    method = 'state_getMetadata'
}

try {
    $meta = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body ($body0 | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "Metadata length: $($meta.result.Length)" -ForegroundColor Green
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try chain_getBlockHash to get latest block
Write-Host ""
Write-Host "=== Getting latest block hash ===" -ForegroundColor Cyan
$body1 = @{
    jsonrpc = '2.0'
    id = 2
    method = 'chain_getBlockHash'
    params = @()
}

try {
    $hash = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body ($body1 | ConvertTo-Json -Compress) -TimeoutSec 15
    Write-Host "Block hash: $($hash.result)" -ForegroundColor Green
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try archive_v1_call with hash
Write-Host ""
Write-Host "=== Testing archive_v1_call with block hash ===" -ForegroundColor Cyan
$hashHex = "0x0000000000000000000000000000000000000000000000000000000000000000"

$body2 = @{
    jsonrpc = '2.0'
    id = 3
    method = 'archive_v1_call'
    params = @{
        method = 'MidnightRuntimeApi_get_contract_state'
        data = '0x'
        hash = $hashHex
    }
}

try {
    $r2 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body ($body2 | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "archive_v1_call result:" -ForegroundColor Green
    $r2 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try archive_v1_storage to query state
Write-Host ""
Write-Host "=== Testing archive_v1_storage ===" -ForegroundColor Cyan
# Storage key format: twox_128(module) + twox_128(item) + key
# For MidnightLedger contract state:
# module = "Midnight", item = "Contracts" or similar

# Let's try with empty key to see what happens
$body3 = @{
    jsonrpc = '2.0'
    id = 4
    method = 'archive_v1_storage'
    params = @{
        keys = @('0x' + ('00' * 64))
        hash = $hashHex
    }
}

try {
    $r3 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body ($body3 | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "archive_v1_storage result:" -ForegroundColor Green
    $r3 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try rpc_methods to see what's available
Write-Host ""
Write-Host "=== All RPC methods ===" -ForegroundColor Cyan
$body4 = @{
    jsonrpc = '2.0'
    id = 5
    method = 'rpc_methods'
}

try {
    $methods = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body ($body4 | ConvertTo-Json -Compress) -TimeoutSec 15
    Write-Host "Available methods:" -ForegroundColor Green
    $methods.result | ForEach-Object { Write-Host "  $_" }
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}
