$graphql = 'https://indexer.preprod.midnight.network/api/v4/graphql'

Write-Host "=== Introspection query ===" -ForegroundColor Cyan

$query = @"
{
  __schema {
    queryType {
      fields {
        name
        description
        type {
          name
          kind
        }
      }
    }
  }
}
"@

try {
    $response = Invoke-RestMethod -Uri $graphql -Method Post -ContentType 'application/json' -Body (@{ query = $query } | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "Query type fields:" -ForegroundColor Green
    $response.data.__schema.queryType.fields | ForEach-Object {
        Write-Host "  $($_.name): $($_.type.name) ($($_.type.kind))"
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}
