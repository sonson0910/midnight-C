$headers = @{ "Content-Type" = "application/json" }
$txHash = "0ca52f7d965277a59a207f30254240d3ae2aca76a3991ca18786e416ae169ac3"

# Check on preview network
$body = @{
    query = @"
    query {
      transactions(offset: { hash: "$txHash" }) {
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
        }
      }
    }
"@
} | ConvertTo-Json -Compress

Write-Host "=== Preview Network ===" 
$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 60
$response | ConvertTo-Json -Depth 20

# Check on preprod network
Write-Host "`n=== Preprod Network ===" 
$response2 = Invoke-RestMethod -Uri "https://indexer.preprod.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 60
$response2 | ConvertTo-Json -Depth 20
