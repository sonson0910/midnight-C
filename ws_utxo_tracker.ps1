#!/usr/bin/env pwsh
# Fast UTXO tracker using native PowerShell WebSocket
# Subscribes to unshieldedTransactions and prints incoming UTXOs instantly

param(
    [Parameter(Mandatory=$false)]
    [string]$Address = "",
    
    [Parameter(Mandatory=$false)]
    [int]$Timeout = 30
)

$ws_url = "wss://indexer.preprod.midnight.network/api/v4/graphql"

if ([string]::IsNullOrWhiteSpace($Address)) {
    Write-Host "Usage: .\ws_utxo_tracker.ps1 -Address <unshielded_address> [-Timeout 30]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Example:" -ForegroundColor Gray
    Write-Host '  .\ws_utxo_tracker.ps1 -Address "mn_addr_preprod1vm7tp364dgq8u9shfms29w4qs4c37aqhtwd8pjqq78zuekvs5vf04q"' -ForegroundColor Gray
    exit 1
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Midnight UTXO Real-Time Tracker" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Address: $Address" -ForegroundColor Yellow
Write-Host "WS URL:  $ws_url" -ForegroundColor Gray
Write-Host "Timeout: $Timeout seconds" -ForegroundColor Gray
Write-Host ""

$totalReceived = 0
$utxoCount = 0
$stopwatch = [Diagnostics.Stopwatch]::StartNew()

try {
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    $ct = [Threading.CancellationToken]::None
    
    Write-Host "[1] Connecting to Indexer WebSocket..." -ForegroundColor Yellow
    
    # Add subprotocol header manually via options
    $ws.Options.AddSubProtocol("graphql-transport-ws")
    
    $connectTask = $ws.ConnectAsync($ws_url, $ct)
    if (-not $connectTask.Wait($Timeout * 1000)) {
        Write-Host "  Connection TIMEOUT!" -ForegroundColor Red
        exit 1
    }
    
    if ($ws.State -ne 'Open') {
        Write-Host "  Connection FAILED, state: $($ws.State)" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "  Connected! WebSocket is OPEN" -ForegroundColor Green
    Write-Host ""
    
    # Send connection_init (graphql-transport-ws protocol)
    Write-Host "[2] Sending connection_init..." -ForegroundColor Yellow
    $init = '{"type":"connection_init","payload":{}}'
    $initBytes = [System.Text.Encoding]::UTF8.GetBytes($init)
    $ws.SendAsync([ArraySegment[byte]]$initBytes, [Net.WebSockets.WebSocketMessageType]::Text, $true, $ct).Wait(5000)
    
    # Wait for connection_ack
    $buf = [byte[]]::new(8192)
    $end = (Get-Date).AddSeconds(10)
    $ackReceived = $false
    
    while ((Get-Date) -lt $end -and $ws.State -eq 'Open' -and -not $ackReceived) {
        $result = $ws.ReceiveAsync([ArraySegment[byte]]$buf, $ct)
        if ($result.Wait([TimeSpan]::FromSeconds(2))) {
            $response = [System.Text.Encoding]::UTF8.GetString($buf, 0, $result.Result.Count)
            Write-Host "  Server: $response" -ForegroundColor Gray
            if ($response -match "connection_ack") {
                Write-Host "  connection_ack received!" -ForegroundColor Green
                $ackReceived = $true
            } elseif ($response -match '"type":"ping"') {
                # Respond to ping with pong
                $pong = '{"type":"pong"}'
                $pongBytes = [System.Text.Encoding]::UTF8.GetBytes($pong)
                $ws.SendAsync([ArraySegment[byte]]$pongBytes, [Net.WebSockets.WebSocketMessageType]::Text, $true, $ct).Wait(3000)
                Write-Host "  -> Pong sent" -ForegroundColor Gray
            }
        }
    }
    
    if (-not $ackReceived) {
        Write-Host "  No connection_ack received!" -ForegroundColor Red
        $ws.CloseAsync([Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "", $ct).Wait(5000)
        exit 1
    }
    
    Write-Host ""
    Write-Host "[3] Subscribing to unshieldedTransactions..." -ForegroundColor Yellow
    Write-Host "  Watching for UTXOs to: $($Address.Substring(0, [Math]::Min(20, $Address.Length)))..." -ForegroundColor Gray
    
    # graphql-transport-ws subscribe message
    # The address in the query must match exactly the Bech32m encoded address
    $subscribeQuery = @"
subscription onUnshielded(`$address: String!) {
  unshieldedTransactions(address: `$address) {
    __typename
    ... on UnshieldedTransaction {
      transaction { id hash }
      createdUtxos { value tokenType outputIndex }
      spentUtxos { value tokenType }
    }
  }
}
"@
    
    $subscribeJson = @{
        id = "utxo-sub-1"
        type = "subscribe"
        payload = @{
            query = $subscribeQuery
            variables = @{
                address = $Address
            }
        }
    } | ConvertTo-Json -Compress -Depth 5
    
    $subBytes = [System.Text.Encoding]::UTF8.GetBytes($subscribeJson)
    $ws.SendAsync([ArraySegment[byte]]$subBytes, [Net.WebSockets.WebSocketMessageType]::Text, $true, $ct).Wait(5000)
    Write-Host "  Subscription sent!" -ForegroundColor Green
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  LISTENING FOR UTXOs..." -ForegroundColor Cyan
    Write-Host "  Press Ctrl+C to stop" -ForegroundColor Gray
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    $listenEnd = (Get-Date).AddSeconds($Timeout)
    
    while ((Get-Date) -lt $listenEnd -and $ws.State -eq 'Open') {
        $recvBuf = [byte[]]::new(16384)
        try {
            $recvResult = $ws.ReceiveAsync([ArraySegment[byte]]$recvBuf, $ct)
            if ($recvResult.Wait([TimeSpan]::FromSeconds(5))) {
                $msg = [System.Text.Encoding]::UTF8.GetString($recvBuf, 0, $recvResult.Result.Count)
                
                if ($msg -match "ping") {
                    $pong = '{"type":"pong"}'
                    $pongBytes = [System.Text.Encoding]::UTF8.GetBytes($pong)
                    $ws.SendAsync([ArraySegment[byte]]$pongBytes, [Net.WebSockets.WebSocketMessageType]::Text, $true, $ct).Wait(2000)
                    continue
                }
                
                if ($msg -match '"type":"next"') {
                    # Parse the subscription data
                    try {
                        $data = $msg -replace '^.*"payload"\s*:\s*', '' -replace '\}\s*$', ''
                        $dataJson = $data | ConvertFrom-Json -EA SilentlyContinue
                        
                        if ($dataJson.data.unshieldedTransactions.createdUtxos.Count -gt 0) {
                            Write-Host "[UTXO EVENT!] $($stopwatch.ElapsedMilliseconds)ms elapsed" -ForegroundColor Green
                            foreach ($utxo in $dataJson.data.unshieldedTransactions.createdUtxos) {
                                $value = [long]$utxo.value / 1000000.0
                                Write-Host "  + $value NIGHT ($($utxo.value) lovelace) [$($utxo.tokenType)]" -ForegroundColor Green
                                Write-Host "    Output Index: $($utxo.outputIndex)" -ForegroundColor Gray
                                $totalReceived += [long]$utxo.value
                                $utxoCount++
                            }
                            $txHash = $dataJson.data.unshieldedTransactions.transaction.hash
                            Write-Host "    TX Hash: $($txHash.Substring(0, [Math]::Min(32, $txHash.Length)))..." -ForegroundColor Gray
                            Write-Host ""
                        }
                        
                        if ($dataJson.data.unshieldedTransactions.spentUtxos.Count -gt 0) {
                            Write-Host "[SPEND EVENT!]" -ForegroundColor Yellow
                            foreach ($utxo in $dataJson.data.unshieldedTransactions.spentUtxos) {
                                $value = [long]$utxo.value / 1000000.0
                                Write-Host "  - $value NIGHT ($($utxo.value) lovelace) [$($utxo.tokenType)]" -ForegroundColor Yellow
                            }
                            Write-Host ""
                        }
                    } catch {
                        # Fallback: just print raw
                        if ($msg.Length -lt 1000) {
                            Write-Host "  [RAW] $msg" -ForegroundColor DarkGray
                        }
                    }
                }
                
                if ($msg -match '"type":"error"') {
                    Write-Host "  [ERROR] $msg" -ForegroundColor Red
                }
            }
        } catch [System.OperationCanceledException] {
            break
        } catch {
            # Timeout or other - continue
        }
    }
    
    $elapsed = $stopwatch.Elapsed
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  SESSION SUMMARY" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  Duration:  $($elapsed.TotalSeconds.ToString('F1'))s" -ForegroundColor White
    Write-Host "  UTXOs:     $utxoCount" -ForegroundColor White
    Write-Host "  Received:  $(($totalReceived/1000000.0).ToString('F6')) NIGHT" -ForegroundColor Green
    Write-Host ""
    
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    if ($ws -and $ws.State -eq 'Open') {
        try {
            $ws.CloseAsync([Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "", $ct).Wait(3000)
        } catch { }
    }
    exit 1
} finally {
    if ($ws -and $ws.State -eq 'Open') {
        try {
            $ws.CloseAsync([Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "done", $ct).Wait(3000)
        } catch { }
    }
}

Write-Host "Done." -ForegroundColor Gray
