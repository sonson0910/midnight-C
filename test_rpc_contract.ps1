$rpc = 'https://rpc.preprod.midnight.network'
$address = 'mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q'
$faucetBlock = 748878

Write-Host "=== Testing midnight_contractState with correct params ===" -ForegroundColor Cyan

# Try midnight_contractState with contract_address field
$body1 = @{
    jsonrpc = '2.0'
    id = 1
    method = 'midnight_contractState'
    params = @{
        contract_address = $address
    }
} | ConvertTo-Json -Compress

try {
    $r1 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body1 -TimeoutSec 15
    Write-Host "midnight_contractState result:" -ForegroundColor Green
    $r1 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try archive_v1_storage at faucet block
Write-Host ""
Write-Host "=== Testing archive_v1_storage at faucet block ($faucetBlock) ===" -ForegroundColor Cyan

# Get header at that block to get state root
$body2 = @{
    jsonrpc = '2.0'
    id = 2
    method = 'archive_v1_header'
    params = @{
        height = $faucetBlock
    }
} | ConvertTo-Json -Compress

try {
    $r2 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body2 -TimeoutSec 15
    Write-Host "Header at block ${faucetBlock}:" -ForegroundColor Green
    $r2 | ConvertTo-Json -Depth 5
} catch {
    Write-Host "archive_v1_header Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try chain_getBlock at faucet block
Write-Host ""
Write-Host "=== Getting faucet TX hash info ===" -ForegroundColor Cyan
$txHash = 'e8b6bab117b98cd7d7dbe6ab86584d81ec3d9960218f001cd3696f957dea977e'

# Try getting transaction details - chain_getBlock contains extrinsics
$body3 = @{
    jsonrpc = '2.0'
    id = 3
    method = 'chain_getBlockHash'
    params = @($faucetBlock)
} | ConvertTo-Json -Compress

try {
    $r3 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body3 -TimeoutSec 15
    $blockHash = $r3.result
    Write-Host "Block hash at ${faucetBlock}: $blockHash" -ForegroundColor Yellow
    
    # Now get the block body
    $body4 = @{
        jsonrpc = '2.0'
        id = 4
        method = 'archive_v1_body'
        params = @{
            hash = $blockHash
        }
    } | ConvertTo-Json -Compress
    
    $r4 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body4 -TimeoutSec 15
    Write-Host "Block body (extrinsics):" -ForegroundColor Green
    $r4 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Try chainHead_v1_header to get current header
Write-Host ""
Write-Host "=== Testing chainHead_v1_header ===" -ForegroundColor Cyan
$body5 = @{
    jsonrpc = '2.0'
    id = 5
    method = 'chainHead_v1_header'
    params = @{
        followSubscription = '0x1234567890'
    }
} | ConvertTo-Json -Compress

try {
    $r5 = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body5 -TimeoutSec 15
    Write-Host "chainHead_v1_header result:" -ForegroundColor Green
    $r5 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "chainHead_v1_header Error: $($_.Exception.Message)" -ForegroundColor Red
}
