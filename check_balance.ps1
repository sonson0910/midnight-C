#!/usr/bin/env pwsh
# Balance checker using GraphQL
param(
    [Parameter(Mandatory=$true)]
    [string]$Address,
    
    [ValidateSet("preview", "preprod")]
    [string]$Network = "preview"
)

$graphql_url = switch ($Network) {
    "preview"  { "https://indexer.preview.midnight.network/api/v4/graphql" }
    "preprod" { "https://indexer.preprod.midnight.network/api/v4/graphql" }
}

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "  Midnight Balance Checker" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Address: $Address" -ForegroundColor Yellow
Write-Host "Network: $Network" -ForegroundColor Yellow
Write-Host "GraphQL: $graphql_url" -ForegroundColor Gray
Write-Host ""

# Note: Indexer requires WebSocket subscription for UTXO data
# HTTP POST only works for simple queries

$query = @"
{
  "query": "subscription UnshieldedBalanceCheck(`$addr: UnshieldedAddress!) { unshieldedTransactions(address: `$addr) { ... on UnshieldedTransaction { transaction { id } createdUtxos { value } spentUtxos { value } } } }",
  "variables": { "addr": "$Address" }
}
"@

Write-Host "[INFO] Starting WebSocket subscription for UTXO tracking..." -ForegroundColor Cyan
Write-Host "[INFO] The indexer does not support HTTP balance queries." -ForegroundColor Yellow
Write-Host "[INFO] Must use WebSocket subscription to track UTXOs." -ForegroundColor Yellow
Write-Host ""

# Try HTTP query first (will likely return error about subscription)
try {
    $headers = @{
        "Content-Type" = "application/json"
    }
    $body = @{
        "query" = "subscription UnshieldedBalanceCheck(`$addr: UnshieldedAddress!) { unshieldedTransactions(address: `$addr) { ... on UnshieldedTransaction { transaction { id } createdUtxos { value } } } }"
        "variables" = @{ "addr" = $Address }
    } | ConvertTo-Json -Depth 5
    
    Write-Host "[DEBUG] Sending HTTP POST..." -ForegroundColor Gray
    $response = Invoke-RestMethod -Uri $graphql_url -Method Post -Headers $headers -Body $body -TimeoutSec 30
    
    if ($response.errors) {
        Write-Host "[ERROR] GraphQL Errors:" -ForegroundColor Red
        foreach ($err in $response.errors) {
            Write-Host "  - $($err.message)" -ForegroundColor Red
        }
    } elseif ($response.data) {
        Write-Host "[INFO] Response received via HTTP (unexpected):" -ForegroundColor Green
        $response.data | ConvertTo-Json -Depth 5
    }
} catch {
    Write-Host "[ERROR] Connection failed: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "  Alternative: Use midnight-wallet-tool" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Set MIDNIGHT_MNEMONIC env var and run:" -ForegroundColor Gray
Write-Host "  midnight-wallet-tool --watch 10" -ForegroundColor Gray
Write-Host ""
Write-Host "  Or use wallet-generator with WebSocket:" -ForegroundColor Gray
Write-Host "  wallet-generator --official-sdk --network $Network --show-private-key" -ForegroundColor Gray
