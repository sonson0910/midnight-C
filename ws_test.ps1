#!/usr/bin/env pwsh
# WebSocket test for Midnight Indexer

param(
    [string]$Address = ""
)

$ws_url = "wss://indexer.preprod.midnight.network/api/v4/graphql"
$timeout = 15

Write-Host "=== Midnight Indexer WebSocket Test ===" -ForegroundColor Cyan
Write-Host "Address: $Address" -ForegroundColor Yellow
Write-Host "WS URL: $ws_url" -ForegroundColor Gray
Write-Host ""

try {
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    $ct = [Threading.CancellationToken]::None
    
    Write-Host "[1] Connecting to WebSocket..." -ForegroundColor Yellow
    $connectTask = $ws.ConnectAsync($ws_url, $ct)
    
    if (-not $connectTask.Wait($timeout * 1000)) {
        Write-Host "  Connection timeout!" -ForegroundColor Red
        return
    }
    
    if ($ws.State -ne 'Open') {
        Write-Host "  Connection failed, state: $($ws.State)" -ForegroundColor Red
        return
    }
    
    Write-Host "  Connected! State: $($ws.State)" -ForegroundColor Green
    
    # Send connection_init
    Write-Host ""
    Write-Host "[2] Sending connection_init..." -ForegroundColor Yellow
    $init = '{"type":"connection_init"}'
    $initBytes = [System.Text.Encoding]::UTF8.GetBytes($init)
    $ws.SendAsync([ArraySegment[byte]]$initBytes, 'Text', $true, $ct).Wait(5000)
    
    # Wait for connection_ack
    $buf = [byte[]]::new(4096)
    $end = (Get-Date).AddSeconds(5)
    $result = $ws.ReceiveAsync([ArraySegment[byte]]$buf, $ct)
    
    if ($result.Wait($end - (Get-Date))) {
        $response = [System.Text.Encoding]::UTF8.GetString($buf, 0, $result.Result.Count)
        Write-Host "  Received: $response" -ForegroundColor Green
        
        if ($response -match "connection_ack") {
            Write-Host "  Connection acknowledged!" -ForegroundColor Green
            
            # Send subscription
            Write-Host ""
            Write-Host "[3] Sending unshieldedTransactions subscription..." -ForegroundColor Yellow
            
            $sub = @"
{"id":"1","type":"subscribe","payload":{"query":"subscription { unshieldedTransactions(address: `"$Address`") { ... on UnshieldedTransaction { transaction { id hash } createdUtxos { value owner } } } }"}
"@
            
            $subBytes = [System.Text.Encoding]::UTF8.GetBytes($sub)
            $ws.SendAsync([ArraySegment[byte]]$subBytes, 'Text', $true, $ct).Wait(5000)
            
            Write-Host "  Subscription sent. Waiting for data..." -ForegroundColor Yellow
            
            # Wait for data
            $end2 = (Get-Date).AddSeconds(10)
            while ((Get-Date) -lt $end2 -and $ws.State -eq 'Open') {
                $buf2 = [byte[]]::new(8192)
                $result2 = $ws.ReceiveAsync([ArraySegment[byte]]$buf2, $ct)
                
                if ($result2.Wait($end2 - (Get-Date))) {
                    $data = [System.Text.Encoding]::UTF8.GetString($buf2, 0, $result2.Result.Count)
                    Write-Host "  Data received: $($data.Substring(0, [Math]::Min(500, $data.Length)))" -ForegroundColor Green
                    
                    # Check if it's a UnshieldedTransaction
                    if ($data -match "UnshieldedTransaction") {
                        Write-Host "  Found UTXO data!" -ForegroundColor Green
                    }
                }
            }
        }
    }
    
    Write-Host ""
    Write-Host "[4] Closing connection..." -ForegroundColor Yellow
    $ws.CloseAsync('NormalClosure', "", $ct).Wait(5000)
    Write-Host "  Closed." -ForegroundColor Green
    
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan
