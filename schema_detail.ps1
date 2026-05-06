$headers = @{ "Content-Type" = "application/json" }
$body = @{
    query = @"
    query {
      __schema {
        types {
          name
          fields {
            name
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30

# Show relevant types
$relevantTypes = @("Transaction", "Block", "DustGenerationStatus")
$response.data.__schema.types | Where-Object { $relevantTypes -contains $_.name } | ForEach-Object {
    Write-Host "=== Type: $($_.name) ==="
    $_.fields | ForEach-Object { Write-Host "  $($_.name)" }
}
