$url = 'https://indexer.preview.midnight.network/api/v4/graphql'
$body = '{"query":"{ __type(name: \"UnshieldedTransaction\") { fields { name } } }"}'
$resp = Invoke-RestMethod -Uri $url -Method POST -ContentType 'application/json' -Body $body -TimeoutSec 10
Write-Host "UnshieldedTransaction fields:"
$resp.data.__type.fields | ForEach-Object { Write-Host "  $($_."name")" }
