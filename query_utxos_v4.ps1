$headers = @{ "Content-Type" = "application/json" }
$body = @{
    query = @"
    query {
      unshieldedUtxos(where: { owner: { eq: "mn_addr_preview12wwwa5q2cau6z4gvnn55vn2ws5fscn79llyjseu9mg4ahz9f5asq4ghyqp" } }) {
        edges {
          node {
            owner
            tokenType
            value
            intentHash
            outputIndex
            registeredForDustGeneration
            ctime
            createdAtTransaction
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30
$response | ConvertTo-Json -Depth 15
