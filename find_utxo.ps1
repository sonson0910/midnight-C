$headers = @{ "Content-Type" = "application/json" }
$targetAddress = "mn_addr_preview12wwwa5q2cau6z4gvnn55vn2ws5fscn79llyjseu9mg4ahz9f5asq4ghyqp"

# Check latest block to find out where user's UTXOs are
$body = @{
    query = @"
    query {
      block(offset: { height: 700000 }) {
        hash
        height
        transactions {
          id
          unshieldedCreatedOutputs {
            owner
            value
          }
          unshieldedSpentOutputs {
            owner
            value
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 60
$response | ConvertTo-Json -Depth 20
