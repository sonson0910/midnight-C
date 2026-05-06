$headers = @{ "Content-Type" = "application/json" }
$body = @{
    query = @"
    query {
      __schema {
        queryType {
          fields {
            name
          }
        }
      }
    }
"@
} | ConvertTo-Json -Compress

$response = Invoke-RestMethod -Uri "https://indexer.preview.midnight.network/api/v4/graphql" -Method Post -Headers $headers -Body $body -TimeoutSec 30
$response.data.__schema.queryType.fields | ForEach-Object { Write-Host $_.name }
