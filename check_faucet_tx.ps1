#!/usr/bin/env pwsh
# Check faucet transaction on preprod

$TX_HASH = "0xe8b6bab117b98cd7d7dbe6ab86584d81ec3d9960218f001cd3696f957dea977e"
$RPC_URL = "https://rpc.preprod.midnight.network"
$INDEXER_URL = "https://indexer.preprod.midnight.network/api/v4/graphql"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Checking Faucet Transaction" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Method 1: Try to get transaction via RPC
Write-Host "[1] Checking via RPC..." -ForegroundColor Yellow
$rpcBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "chain_getBlockHash"
    params = @()
} | ConvertTo-Json

try {
    $response = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $rpcBody -TimeoutSec 30
    Write-Host "Latest block hash: $($response.result)" -ForegroundColor Green
} catch {
    Write-Host "RPC Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Method 2: Try to query via GraphQL
Write-Host ""
Write-Host "[2] Checking via GraphQL Indexer..." -ForegroundColor Yellow

$graphqlQuery = @"
{
    "query": "query { transaction(hash: \"" + $TX_HASH.Substring(2) + "\") { hash block { number } status } }"
}
"@

# Alternative: query by tx hash directly
$query2 = @"
{
    "query": "{ transactions(where: { hash: { _eq: \"" + $TX_HASH + "\" } }) { hash block { number } status } }"
}
"@

Write-Host "Trying to query transaction..."

try {
    $gqlResponse = Invoke-RestMethod -Uri $INDEXER_URL -Method Post -ContentType "application/json" -Body $query2 -TimeoutSec 30
    Write-Host "GraphQL Response: $($gqlResponse | ConvertTo-Json -Depth 5)"
} catch {
    Write-Host "GraphQL Error: $($_.Exception.Message)" -ForegroundColor Red
}

# Method 3: Check recent blocks
Write-Host ""
Write-Host "[3] Checking chain tip..." -ForegroundColor Yellow

$tipBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "chain_getHeader"
    params = @()
} | ConvertTo-Json

try {
    $tipResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $tipBody -TimeoutSec 30
    Write-Host "Chain Header: $($tipResponse | ConvertTo-Json -Depth 5)"
} catch {
    Write-Host "Tip Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
