$headers = @{ "Content-Type" = "application/json" }

# Get latest block info
$body = @{
    query = @"
    query {
      block(offset: { height: 10000000 }) {
        hash
        height
        timestamp
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30
Write-Host "=== Latest Block ===" 
$response | ConvertTo-Json -Depth 10

# Also query for blocks with transactions
$body2 = @{
    query = @"
    query {
      block(offset: { height: 100 }) {
        hash
        height
        timestamp
        transactions {
          id
          unshieldedCreatedOutputs {
            owner
            value
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response2 = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body2 -TimeoutSec 30
Write-Host "`n=== Block 100 ===" 
$response2 | ConvertTo-Json -Depth 10
