$rpc = 'https://rpc.preprod.midnight.network'
$address = 'mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q'

# The unshielded address public key is 32 bytes
# SCALE encoding for Vec<u8>: compact(length) + bytes
# 32 in compact encoding: 0x80 (32 * 4 = 128, compact = 128 << 2 = 0x80)
# So SCALE-encoded 32-byte address: 0x80 + 32 bytes

$hexBytes = '66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99'
$scaleEncoded = '0x80' + $hexBytes  # 0x80 = compact(32)

Write-Host "=== Testing midnight_contractState with SCALE-encoded address ===" -ForegroundColor Cyan
Write-Host "Address: $address" -ForegroundColor Yellow
Write-Host "Hex bytes: $hexBytes" -ForegroundColor Yellow
Write-Host "SCALE encoded: $scaleEncoded" -ForegroundColor Yellow

$body = @{
    jsonrpc = '2.0'
    id = 1
    method = 'midnight_contractState'
    params = @{
        contract_address = $scaleEncoded
    }
}

try {
    $response = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body ($body | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "Response:" -ForegroundColor Green
    $response | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try with array params instead
Write-Host ""
Write-Host "=== Testing with array params ===" -ForegroundColor Cyan
$body2 = @{
    jsonrpc = '2.0'
    id = 2
    method = 'midnight_contractState'
    params = @($scaleEncoded)
}

try {
    $response2 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body ($body2 | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "Response:" -ForegroundColor Green
    $response2 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try midnight_unclaimedAmount
Write-Host ""
Write-Host "=== Testing midnight_unclaimedAmount ===" -ForegroundColor Cyan
$body3 = @{
    jsonrpc = '2.0'
    id = 3
    method = 'midnight_unclaimedAmount'
    params = @{
        contract_address = $scaleEncoded
    }
}

try {
    $response3 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body ($body3 | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "Response:" -ForegroundColor Green
    $response3 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}
