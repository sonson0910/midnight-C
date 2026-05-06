$headers = @{ "Content-Type" = "application/json" }
$body = @{
    query = @"
    query {
      __schema {
        types {
          name
          fields {
            name
            type {
              name
              kind
            }
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30

$relevantTypes = @("UnshieldedUtxo")
$response.data.__schema.types | Where-Object { $relevantTypes -contains $_.name } | ForEach-Object {
    Write-Host "=== Type: $($_.name) ==="
    $_.fields | ForEach-Object { Write-Host "  $($_.name): $($_.type.name) ($($_.type.kind))" }
}
