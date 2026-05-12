#!/usr/bin/env pwsh
# Get full block data and archive query

$RPC_URL = "https://rpc.preprod.midnight.network"
$INDEXER_URL = "https://indexer.preprod.midnight.network/api/v4/graphql"
$BLOCK_HASH = "0x939906f65687c6250fce1eb21a59737f23b382e4a8335004d2b9dd9de33ecc2c"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Full Block Analysis" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 1. Get full block with extrinsics
Write-Host "[1] Getting full block data..." -ForegroundColor Yellow
$blockBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "chain_getBlock"
    params = @($BLOCK_HASH)
} | ConvertTo-Json

try {
    $blockResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $blockBody -TimeoutSec 30
    $extrinsics = $blockResponse.result.block.extrinsics
    Write-Host "Number of extrinsics: $($extrinsics.Count)" -ForegroundColor Green
    for ($i = 0; $i -lt $extrinsics.Count; $i++) {
        Write-Host ""
        Write-Host "Extrinsic [$i]: $($extrinsics[$i])" -ForegroundColor Cyan
        Write-Host "  Length: $($extrinsics[$i].Length) chars"
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# 2. Try archive_v1_body
Write-Host ""
Write-Host "[2] Trying archive_v1_body..." -ForegroundColor Yellow

$archiveBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "archive_v1_body"
    params = @($BLOCK_HASH)
} | ConvertTo-Json

try {
    $archiveResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $archiveBody -TimeoutSec 30
    Write-Host "Archive Body: $($archiveResponse | ConvertTo-Json -Depth 10)"
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# 3. Try archive_v1_call
Write-Host ""
Write-Host "[3] Trying archive_v1_call..." -ForegroundColor Yellow

$callParams = @{
    block = $BLOCK_HASH
    method = "MidnightFaucet_claim"
    args = @{
        recipient = "0x66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99"
    }
}

$callBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "archive_v1_call"
    params = $callParams
} | ConvertTo-Json -Depth 10

try {
    $callResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $callBody -TimeoutSec 30
    Write-Host "Archive Call: $($callResponse | ConvertTo-Json -Depth 10)"
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# 4. Try GraphQL query for transactions
Write-Host ""
Write-Host "[4] GraphQL query for transactions..." -ForegroundColor Yellow

$txHash = "0xe8b6bab117b98cd7d7dbe6ab86584d81ec3d9960218f001cd3696f957dea977e"

$graphqlQuery = @{
    query = "query { transactions(where: { hash: { _like: `"%$($txHash.Substring(2))%`" } }) { hash block { number } status inputs { address amount } outputs { address amount } } }"
} | ConvertTo-Json

try {
    $gqlResponse = Invoke-RestMethod -Uri $INDEXER_URL -Method Post -ContentType "application/json" -Body $graphqlQuery -TimeoutSec 30
    Write-Host "GraphQL Response: $($gqlResponse | ConvertTo-Json -Depth 10)"
} catch {
    Write-Host "GraphQL Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
