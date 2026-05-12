$rpc = 'https://rpc.preprod.midnight.network'
$address = 'mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q'

Write-Host "=== Testing midnight_contractState ===" -ForegroundColor Cyan

$body = @{
    jsonrpc = '2.0'
    id = 1
    method = 'midnight_contractState'
    params = @{
        contract_address = $address
    }
}

$jsonBody = $body | ConvertTo-Json -Compress
Write-Host "Request: $jsonBody" -ForegroundColor Gray

try {
    $response = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $jsonBody -TimeoutSec 30
    Write-Host "Response:" -ForegroundColor Green
    $response | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Response: $($_.Exception.Response)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Testing state_call with MidnightRuntimeApi_get_contract_state ===" -ForegroundColor Cyan

# Try state_call with the runtime API method
$methodHex = "0x" + ([System.Text.Encoding]::ASCII.GetBytes("MidnightRuntimeApi_get_contract_state") | ForEach-Object { $_.ToString("x2") })
Write-Host "Method hex: $methodHex" -ForegroundColor Yellow

# We need to pass the address as SCALE-encoded Vec<u8>
# But first let's see if we can just call state_call with the method name
$body2 = @{
    jsonrpc = '2.0'
    id = 2
    method = 'state_call'
    params = @($methodHex, "0x")  # Empty data for now
}

$jsonBody2 = $body2 | ConvertTo-Json -Compress
Write-Host "Request: $jsonBody2" -ForegroundColor Gray

try {
    $response2 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $jsonBody2 -TimeoutSec 30
    Write-Host "Response:" -ForegroundColor Green
    $response2 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try archive_v1_call which might work better
Write-Host ""
Write-Host "=== Testing archive_v1_call ===" -ForegroundColor Cyan

$body3 = @{
    jsonrpc = '2.0'
    id = 3
    method = 'archive_v1_call'
    params = @{
        method = 'MidnightRuntimeApi_get_contract_state'
        data = '0x'
    }
}

$jsonBody3 = $body3 | ConvertTo-Json -Compress
Write-Host "Request: $jsonBody3" -ForegroundColor Gray

try {
    $response3 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $jsonBody3 -TimeoutSec 30
    Write-Host "Response:" -ForegroundColor Green
    $response3 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}
