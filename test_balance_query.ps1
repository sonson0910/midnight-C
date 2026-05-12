# Check balance using different RPC methods

# User's preprod address (Bech32m)
$UserAddress = "mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q"
# User's address hex (32 bytes)
$UserAddressHex = "66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99"

$RpcUrl = "https://rpc.preprod.midnight.network"

Write-Host "=== Test midnight_contractState - User Address ===" -ForegroundColor Cyan
$body1 = @{
    jsonrpc = "2.0"
    id = 1
    method = "midnight_contractState"
    params = @{
        contract_address = $UserAddressHex
    }
} | ConvertTo-Json -Depth 10

$result1 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body1
Write-Host "Result (user address): " -ForegroundColor Green
$result1 | ConvertTo-Json -Depth 10

Write-Host "`n=== Test midnight_contractState - Empty string ===" -ForegroundColor Cyan
$body2 = @{
    jsonrpc = "2.0"
    id = 2
    method = "midnight_contractState"
    params = @{
        contract_address = ""
    }
} | ConvertTo-Json -Depth 10

$result2 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body2
Write-Host "Result (empty): " -ForegroundColor Green
$result2 | ConvertTo-Json -Depth 10

Write-Host "`n=== Test midnight_contractState - All zeros ===" -ForegroundColor Cyan
$body3 = @{
    jsonrpc = "2.0"
    id = 3
    method = "midnight_contractState"
    params = @{
        contract_address = "0000000000000000000000000000000000000000000000000000000000000000"
    }
} | ConvertTo-Json -Depth 10

$result3 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body3
Write-Host "Result (zeros): " -ForegroundColor Green
$result3 | ConvertTo-Json -Depth 10

# Thử getBalance qua state_getStorage
Write-Host "`n=== Test state_getStorage ===" -ForegroundColor Cyan
$body4 = @{
    jsonrpc = "2.0"
    id = 4
    method = "state_getStorage"
    params = @{
        key = "0x66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99"
    }
} | ConvertTo-Json -Depth 10

$result4 = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body4
Write-Host "Result (state_getStorage): " -ForegroundColor Green
$result4 | ConvertTo-Json -Depth 10

# Thử kiểm tra xem có method nào liên quan đến balance không
Write-Host "`n=== Query midnight methods ===" -ForegroundColor Cyan
$midnightMethods = @(
    "midnight_contractState",
    "midnight_apiVersions", 
    "midnight_ledgerStateRoot",
    "midnight_ledgerVersion",
    "midnight_zswapStateRoot"
)

foreach ($method in $midnightMethods) {
    Write-Host "`n--- $method ---" -ForegroundColor Yellow
    $body = @{
        jsonrpc = "2.0"
        id = 99
        method = $method
        params = if ($method -eq "midnight_contractState") {
            @{ contract_address = $UserAddressHex }
        } elseif ($method -eq "midnight_apiVersions") {
            @{}
        } else {
            @{}
        }
    } | ConvertTo-Json -Depth 10
    
    try {
        $result = Invoke-RestMethod -Uri $RpcUrl -Method Post -ContentType "application/json" -Body $body
        Write-Host "Result: " -NoNewline
        $result | ConvertTo-Json -Depth 5
    } catch {
        Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    }
}
