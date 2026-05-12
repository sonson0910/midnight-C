#!/usr/bin/env pwsh
# Find correct query for Midnight Indexer

$graphql_url = "https://indexer.preview.midnight.network/api/v4/graphql"

Write-Host "=== Finding Correct Indexer Query ===" -ForegroundColor Cyan
Write-Host ""

# Test 1: offset: { hash }
Write-Host "[1] Testing offset: { hash }..." -ForegroundColor Yellow
$q1 = @"
{"query":"{ transactions(offset: { hash: \"0000000000000000000000000000000000000000000000000000000000000000\" }) { id hash } }"}
"@
$body = [System.Text.Encoding]::UTF8.GetBytes($q1)
$resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -TimeoutSec 10 -UseBasicParsing
$data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
if ($data.errors) { Write-Host "  Error: $($data.errors[0].message.Substring(0, 80))" -ForegroundColor Red }
else { Write-Host "  Success!" -ForegroundColor Green }

# Test 2: offset: { id }
Write-Host "[2] Testing offset: { id }..." -ForegroundColor Yellow
$q2 = '{"query":"{ transactions(offset: { id: 0 }) { id hash } }"}'
$body = [System.Text.Encoding]::UTF8.GetBytes($q2)
$resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -TimeoutSec 10 -UseBasicParsing
$data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
if ($data.errors) { Write-Host "  Error: $($data.errors[0].message.Substring(0, 80))" -ForegroundColor Red }
else { Write-Host "  Success! $($data.data.transactions.Count) found" -ForegroundColor Green }

# Test 3: offset: { transactionId }
Write-Host "[3] Testing offset: { transactionId }..." -ForegroundColor Yellow
$q3 = '{"query":"{ transactions(offset: { transactionId: 0 }) { id hash } }"}'
$body = [System.Text.Encoding]::UTF8.GetBytes($q3)
$resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -body -TimeoutSec 10 -UseBasicParsing
$data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
if ($data.errors) { Write-Host "  Error: $($data.errors[0].message.Substring(0, 80))" -ForegroundColor Red }
else { Write-Host "  Success! $($data.data.transactions.Count) found" -ForegroundColor Green }

# Test 4: block query with transactions
Write-Host "[4] Testing block { transactions }..." -ForegroundColor Yellow
$q4 = '{"query":"{ block { height hash transactions { id hash } }"}'
$body = [System.Text.Encoding]::UTF8.GetBytes($q4)
$resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -TimeoutSec 15 -UseBasicParsing
$data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
if ($data.data.block) {
    Write-Host "  Block: $($data.data.block.height), TXs: $($data.data.block.transactions.Count)" -ForegroundColor Green
}

# Test 5: Get block heights
Write-Host "[5] Getting recent block heights..." -ForegroundColor Yellow
$q5 = '{"query":"{ block { height } }"}'
$body = [System.Text.Encoding]::UTF8.GetBytes($q5)
$resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -TimeoutSec 15 -UseBasicParsing
$data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
Write-Host "  Current height: $($data.data.block.height)" -ForegroundColor Green

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan
