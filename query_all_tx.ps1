$headers = @{ "Content-Type" = "application/json" }

# Query transactions with owner's UTXOs
$body = @{
    query = @"
    query {
      transactions(offset: 0) {
        id
        hash
        block {
          height
        }
        unshieldedCreatedOutputs {
          owner
          value
          tokenType
        }
        unshieldedSpentOutputs {
          owner
          value
          tokenType
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 60
$response | ConvertTo-Json -Depth 20
