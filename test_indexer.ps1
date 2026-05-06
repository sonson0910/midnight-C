# First get the latest block
$body1 = @{
    query = "{ block { hash height } }"
} | ConvertTo-Json -Compress

$response1 = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" `
    -Method POST -ContentType "application/json" -Body $body1

Write-Host "=== Latest Block ===" -ForegroundColor Cyan
$response1 | ConvertTo-Json -Depth 5

# Now try transactions from genesis
$body2 = @{
    query = "{ transactions(first: 5) { hash block { height } unshieldedCreatedOutputs { owner value tokenType outputIndex } } }"
} | ConvertTo-Json -Compress

$response2 = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" `
    -Method POST -ContentType "application/json" -Body $body2

Write-Host "`n=== Transactions (first:5) ===" -ForegroundColor Cyan
$response2 | ConvertTo-Json -Depth 10
