$rpc = 'https://rpc.preprod.midnight.network'
$hexAddress = '0x66fcb0c7556a007e16174ee0a2bbff7baa085711f74175b9a70c800f1c5ccd99'

Write-Host "=== Testing midnight_contractState with hex address ===" -ForegroundColor Cyan
Write-Host "Address (hex): $hexAddress" -ForegroundColor Yellow

$body = @{
    jsonrpc = '2.0'
    id = 1
    method = 'midnight_contractState'
    params = @{
        contract_address = $hexAddress
    }
} | ConvertTo-Json -Compress

Write-Host "Request: $body" -ForegroundColor Gray

try {
    $response = Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 30
    Write-Host "Response:" -ForegroundColor Green
    $response | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Response stream: $($_.Exception.Response)" -ForegroundColor Red
}
