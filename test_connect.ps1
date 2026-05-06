try {
    $r = Invoke-WebRequest -Uri 'https://indexer.preview.midnight.network/api/v4/graphql' -Method POST -Body '{"query":"{ __typename }"}' -ContentType 'application/json' -TimeoutSec 10 -UseBasicParsing
    Write-Host "HTTP OK:" $r.StatusCode
    Write-Host $r.Content
} catch {
    Write-Host "ERR:" $_.Exception.Message
}

try {
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    $ct = [Threading.CancellationToken]::None
    $ws.ConnectAsync((Invoke-RestMethod 'https://indexer.preview.midnight.network/api/v4/graphql' -Method POST -Body '{"query":"{ __typename }"}' -ContentType 'application/json' -UseBasicParsing).BaseResponse.ResponseUri, $ct).Wait(10000)
    Write-Host "WebSocket connect result:" $ws.State
    $ws.CloseAsync('NormalClosure', "", $ct).Wait(5000)
} catch {
    Write-Host "WS ERR:" $_.Exception.Message
}
