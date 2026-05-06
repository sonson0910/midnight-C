$headers = @{ "Content-Type" = "application/json" }

# Query block at height 1 (genesis)
$body1 = @{
    query = @"
    query {
      block(offset: { height: 1 }) {
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

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body1 -TimeoutSec 30
Write-Host "=== Block 1 ===" 
$response | ConvertTo-Json -Depth 10

# Query block at height 500
$body2 = @{
    query = @"
    query {
      block(offset: { height: 500 }) {
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
Write-Host "`n=== Block 500 ===" 
$response2 | ConvertTo-Json -Depth 10
