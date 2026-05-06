$headers = @{ "Content-Type" = "application/json" }
$body = @{
    query = @"
    query {
      unshieldedAccount(address: "mn_addr_preview12wwwa5q2cau6z4gvnn55vn2ws5fscn79llyjseu9mg4ahz9f5asq4ghyqp") {
        balance
        coinHistory(first: 10) {
          edges {
            node {
              transactionId
              value
              __typename
            }
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v3/graphql" -Method Post -Headers $headers -Body $body
$response | ConvertTo-Json -Depth 10
