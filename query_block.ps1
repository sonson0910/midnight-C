$headers = @{ "Content-Type" = "application/json" }

# Query recent blocks
$body = @{
    query = @"
    query {
      block(offset: { height: 1000 }) {
        hash
        height
        timestamp
        transactions {
          id
          unshieldedCreatedOutputs {
            address
            value
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30
$response | ConvertTo-Json -Depth 20
