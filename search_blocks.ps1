$headers = @{ "Content-Type" = "application/json" }
$userAddr = "mn_addr_preview12wwwa5q2cau6z4gvnn55vn2ws5fscn79llyjseu9mg4ahz9f5asq4ghyqp"

# Search blocks from 609000 to 611668 (latest) to find any UTXOs with user's address
# Block 609178 is where the tx is
$body = @{
    query = @"
    query {
      block(offset: { height: 609175 }) {
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
