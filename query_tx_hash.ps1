$headers = @{ "Content-Type" = "application/json" }
$txHash = "0x0ca52f7d965277a59a207f30254240d3ae2aca76a3991ca18786e416ae169ac3"

# Remove 0x prefix if present
$cleanHash = $txHash -replace "^0x", ""

$body = @{
    query = @"
    query {
      transactions(offset: { hash: "$cleanHash" }) {
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
