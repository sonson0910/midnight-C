#!/usr/bin/env pwsh
# Check transaction in block 748878

$TX_HASH = "0xe8b6bab117b98cd7d7dbe6ab86584d81ec3d9960218f001cd3696f957dea977e"
$BLOCK_NUM = 748878
$RPC_URL = "https://rpc.preprod.midnight.network"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Checking Transaction Details" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Block: $BLOCK_NUM"
Write-Host "TX Hash: $TX_HASH"
Write-Host ""

# 1. Get block hash for block 748878
Write-Host "[1] Getting block $BLOCK_NUM hash..." -ForegroundColor Yellow
$blockBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "chain_getBlockHash"
    params = @($BLOCK_NUM)
} | ConvertTo-Json

try {
    $blockResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $blockBody -TimeoutSec 30
    $blockHash = $blockResponse.result
    Write-Host "Block Hash: $blockHash" -ForegroundColor Green
} catch {
    Write-Host "Error getting block hash: $($_.Exception.Message)" -ForegroundColor Red
}

if ($blockHash) {
    # 2. Get block body (extrinsics)
    Write-Host ""
    Write-Host "[2] Getting block body..." -ForegroundColor Yellow
    $bodyBody = @{
        jsonrpc = "2.0"
        id = 1
        method = "chain_getBlock"
        params = @($blockHash)
    } | ConvertTo-Json
    
    try {
        $blockData = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $bodyBody -TimeoutSec 30
        $blockJson = $blockData | ConvertTo-Json -Depth 10
        Write-Host "Block Data (first 3000 chars):"
        Write-Host $blockJson.Substring(0, [Math]::Min(3000, $blockJson.Length))
    } catch {
        Write-Host "Error getting block: $($_.Exception.Message)" -ForegroundColor Red
    }
}

# 3. Try to query storage for this address
Write-Host ""
Write-Host "[3] Checking account info..." -ForegroundColor Yellow

$accBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "query_infoAt"
    params = @{
        key = $TX_HASH
        at = $blockHash
    }
} | ConvertTo-Json -Depth 10

try {
    $accResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $accBody -TimeoutSec 30
    Write-Host "Query Response: $($accResponse | ConvertTo-Json -Depth 5)"
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

# 4. Try archive_v1_header
Write-Host ""
Write-Host "[4] Checking archive header..." -ForegroundColor Yellow

$archiveBody = @{
    jsonrpc = "2.0"
    id = 1
    method = "archive_v1_header"
    params = @($BLOCK_NUM)
} | ConvertTo-Json

try {
    $archiveResponse = Invoke-RestMethod -Uri $RPC_URL -Method Post -ContentType "application/json" -Body $archiveBody -TimeoutSec 30
    Write-Host "Archive Header: $($archiveResponse | ConvertTo-Json -Depth 5)"
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
