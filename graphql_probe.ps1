#!/usr/bin/env pwsh
# GraphQL probe for Midnight Indexer

$graphql_url = "https://indexer.preview.midnight.network/api/v4/graphql"

Write-Host "=== Midnight Indexer Schema Probe ===" -ForegroundColor Cyan
Write-Host "Endpoint: $graphql_url" -ForegroundColor Gray
Write-Host ""

# 1. Query Block
Write-Host "[1] Query latest block..." -ForegroundColor Yellow
try {
    $query = '{"query":"{ block { height hash } }"}'
    $body = [System.Text.Encoding]::UTF8.GetBytes($query)
    $resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -TimeoutSec 15 -UseBasicParsing
    $data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
    if ($data.data) {
        Write-Host "  Block height: $($data.data.block.height)" -ForegroundColor Green
        Write-Host "  Block hash: $($data.data.block.hash)" -ForegroundColor Green
    }
} catch {
    Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Red
}

# 2. Try transactions query with null offset
Write-Host ""
Write-Host "[2] Try transactions query with offset: null..." -ForegroundColor Yellow
try {
    $query = '{"query":"{ transactions(offset: null) { id hash } }"}'
    $body = [System.Text.Encoding]::UTF8.GetBytes($query)
    $resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -TimeoutSec 15 -UseBasicParsing
    $data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
    if ($data.data -and $data.data.transactions) {
        Write-Host "  Found $($data.data.transactions.Count) transactions" -ForegroundColor Green
    }
    if ($data.errors) {
        foreach ($err in $data.errors) {
            Write-Host "  Error: $($err.message)" -ForegroundColor Red
        }
    }
} catch {
    Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Red
}

# 3. Try regularTransaction by ID
Write-Host ""
Write-Host "[3] Query regularTransaction by ID..." -ForegroundColor Yellow
try {
    $query = '{"query":"{ regularTransaction(id: 1) { id hash } }"}'
    $body = [System.Text.Encoding]::UTF8.GetBytes($query)
    $resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -TimeoutSec 15 -UseBasicParsing
    $data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
    if ($data.data -and $data.data.regularTransaction) {
        Write-Host "  TX ID: $($data.data.regularTransaction.id)" -ForegroundColor Green
        Write-Host "  TX Hash: $($data.data.regularTransaction.hash)" -ForegroundColor Green
    }
    if ($data.errors) {
        foreach ($err in $data.errors) {
            Write-Host "  Error: $($err.message)" -ForegroundColor Red
        }
    }
} catch {
    Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Red
}

# 4. Try systemTransaction
Write-Host ""
Write-Host "[4] Query systemTransaction..." -ForegroundColor Yellow
try {
    $query = '{"query":"{ systemTransaction(id: 1) { id hash } }"}'
    $body = [System.Text.Encoding]::UTF8.GetBytes($query)
    $resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -TimeoutSec 15 -UseBasicParsing
    $data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
    if ($data.data -and $data.data.systemTransaction) {
        Write-Host "  System TX ID: $($data.data.systemTransaction.id)" -ForegroundColor Green
    }
    if ($data.errors) {
        foreach ($err in $data.errors) {
            Write-Host "  Error: $($err.message)" -ForegroundColor Red
        }
    }
} catch {
    Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Red
}

# 5. Try unshieldedTransactions subscription via HTTP (won't work but let's see the error)
Write-Host ""
Write-Host "[5] Try unshieldedTransactions query..." -ForegroundColor Yellow
try {
    $query = '{"query":"{ unshieldedTransactions(address: \"mn_addr_preview1gd6495fqs6q4euafwcgy53tgz0jqee355h2fk9qqcp3pf00azf6qjgyqhc\") { id } }"}'
    $body = [System.Text.Encoding]::UTF8.GetBytes($query)
    $resp = Invoke-WebRequest -Uri $graphql_url -Method Post -ContentType "application/json" -Body $body -TimeoutSec 15 -UseBasicParsing
    $data = [System.Text.Encoding]::UTF8.GetString($resp.Content) | ConvertFrom-Json
    if ($data.data) {
        Write-Host "  Response: $($data.data | ConvertTo-Json -Depth 2)" -ForegroundColor Green
    }
    if ($data.errors) {
        foreach ($err in $data.errors) {
            Write-Host "  Error: $($err.message)" -ForegroundColor Red
        }
    }
} catch {
    Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan
