$body = @{
    query = 'query { address(address: "mn_addr_preview1mtpshtezg450jk7rmpvgwfsa60cpwp8n89cnu8ax5xp6x5qhcspq62ut7h") { balance { total available locked } unshieldedUtxos { totalCount } } }'
} | ConvertTo-Json -Compress

try {
    $result = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method POST -ContentType "application/json" -Body $body -TimeoutSec 30
    $result | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $_"
}
