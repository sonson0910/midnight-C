$headers = @{ "Content-Type" = "application/json" }

# Get latest block info (no offset)
$body = @{
    query = @"
    query {
      block {
        hash
        height
        timestamp
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 60
Write-Host "=== Latest Block ===" 
$response.data.block | Format-List

# Now search blocks from 600000 onwards for UTXOs with user's address
$userAddress = "mn_addr_preview12wwwa5q2cau6z4gvnn55vn2ws5fscn79llyjseu9mg4ahz9f5asq4ghyqp"

# Query blocks 609000 - 610000 to see recent activity around the tx we found
$body2 = @{
    query = @"
    query {
      block(offset: { height: 609200 }) {
        hash
        height
        transactions {
          id
          unshieldedCreatedOutputs {
            owner
            value
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response2 = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body2 -TimeoutSec 60
$response2 | ConvertTo-Json -Depth 20
