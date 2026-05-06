$headers = @{ "Content-Type" = "application/json" }

# Query dust generation status
$body = @{
    query = @"
    query {
      dustGenerationStatus {
        cardanoRewardAddress
        dustAddress
        registered
        nightBalance
        generationRate
        maxCapacity
        currentCapacity
        utxoTxHash
        utxoOutputIndex
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30
Write-Host "=== Dust Generation Status ===" 
$response.data.dustGenerationStatus | ConvertTo-Json -Depth 10
