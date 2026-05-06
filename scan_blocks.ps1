$headers = @{ "Content-Type" = "application/json" }

# Query multiple blocks to find UTXOs with significant values
# The user's tx shows 97000000000 = 97 NIGHT being spent
# So maybe the user's UTXO was spent

$userAddr = "mn_addr_preview12wwwa5q2cau6z4gvnn55vn2ws5fscn79llyjseu9mg4ahz9f5asq4ghyqp"

# Query blocks around where the user's UTXO might be
for ($h = 609170; $h -le 609185; $h++) {
    $body = @{
        query = @"
        query {
          block(offset: { height: $h }) {
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
    
    $response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30
    
    if ($response.data.block.transactions.Count -gt 0) {
        foreach ($tx in $response.data.block.transactions) {
            if ($tx.unshieldedCreatedOutputs) {
                foreach ($out in $tx.unshieldedCreatedOutputs) {
                    if ($out.owner -eq $userAddr) {
                        Write-Host "FOUND! Block $h, Tx $($tx.id), Value: $($out.value)"
                    }
                }
            }
        }
    }
}
