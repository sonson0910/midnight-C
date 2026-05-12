# Test midnight_contractState và state_call với đúng params

$AddressHex32 = "66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99"
$RpcUrl = "https://rpc.preprod.midnight.network"

Write-Host "=== Test midnight_contractState với bytes field ===" -ForegroundColor Cyan
$body1 = @{
    jsonrpc = "2.0"
    id = 1
    method = "midnight_contractState"
    params = @{
        bytes = $AddressHex32
    }
} | ConvertTo-Json -Depth 10

Write-Host "Request: $body1" -ForegroundColor Yellow
$result1 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body1
Write-Host "Result: " -ForegroundColor Green
$result1 | ConvertTo-Json -Depth 10

Write-Host "`n=== Test state_call với bytes field ===" -ForegroundColor Cyan
$body2 = @{
    jsonrpc = "2.0"
    id = 2
    method = "state_call"
    params = @{
        name = "MidnightRuntimeApi_get_contract_state"
        bytes = "0x$AddressHex32"
    }
} | ConvertTo-Json -Depth 10

Write-Host "Request: $body2" -ForegroundColor Yellow
$result2 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body2
Write-Host "Result: " -ForegroundColor Green
$result2 | ConvertTo-Json -Depth 10

# Thử decode empty result từ midnight_contractState
Write-Host "`n=== Parse empty result ===" -ForegroundColor Cyan
Write-Host "Result type: $($result1.result.GetType().Name)" -ForegroundColor Yellow
Write-Host "Result value: '$($result1.result)'" -ForegroundColor Yellow
Write-Host "Result length: $(if($result1.result) { $result1.result.Length } else { 'null' })" -ForegroundColor Yellow

# Thử với địa chỉ khác - ví dụ một contract đã deploy
Write-Host "`n=== Test với contract address mẫu từ test data ===" -ForegroundColor Cyan
# Contract address từ test file: dd76bcd08403b3a9af64485b1f3960437e7c5872c269d6d06d32b2f298d71577

$SampleContract = "dd76bcd08403b3a9af64485b1f3960437e7c5872c269d6d06d32b2f298d71577"

$body3 = @{
    jsonrpc = "2.0"
    id = 3
    method = "midnight_contractState"
    params = @{
        contract_address = $SampleContract
    }
} | ConvertTo-Json -Depth 10

Write-Host "Testing with sample contract: $SampleContract" -ForegroundColor Yellow
$result3 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body3
Write-Host "Result: " -ForegroundColor Green
$result3 | ConvertTo-Json -Depth 10

# Thử với SCALE encoded contract_address (0x80 + 32 bytes)
Write-Host "`n=== Test midnight_contractState với SCALE params (0x80 prefix) ===" -ForegroundColor Cyan
$body4 = @{
    jsonrpc = "2.0"
    id = 4
    method = "midnight_contractState"
    params = @{
        contract_address = "80$SampleContract"
    }
} | ConvertTo-Json -Depth 10

$result4 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body4
Write-Host "Result: " -ForegroundColor Green
$result4 | ConvertTo-Json -Depth 10

# Thử với 0x prefix + SCALE encoded
Write-Host "`n=== Test midnight_contractState với 0x80 prefix ===" -ForegroundColor Cyan
$body5 = @{
    jsonrpc = "2.0"
    id = 5
    method = "midnight_contractState"
    params = @{
        contract_address = "0x80$SampleContract"
    }
} | ConvertTo-Json -Depth 10

$result5 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body5
Write-Host "Result: " -ForegroundColor Green
$result5 | ConvertTo-Json -Depth 10
