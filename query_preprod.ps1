$headers = @{ "Content-Type" = "application/json" }

# Query transactions from preprod
$body = @{
    query = @"
    query {
      transactions(offset: 0) {
        id
        hash
        block {
          height
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preprod.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30
$response | ConvertTo-Json -Depth 10
