$headers = @{ "Content-Type" = "application/json" }
$body = @{
    query = @"
    query {
      transactions(limit: 10, order: [Block_Desc]) {
        edges {
          node {
            id
            block {
              number
            }
            inputs {
              address
              value
            }
            outputs {
              address
              value
            }
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30
$response | ConvertTo-Json -Depth 20
