# Check unshieldedTransactions returns
$url = 'https://indexer.preview.midnight.network/api/v4/graphql'

$query = @"
query {
  __type(name: "UnshieldedTransaction") {
    name
    kind
    fields(includeDeprecated: true) {
      name
      type {
        name
        kind
        ofType {
          name
          kind
        }
      }
    }
  }
}
"@

$body = @{ query = $query }
$headers = @{
    'Content-Type' = 'application/json'
    'Accept' = 'application/json'
}

try {
    $jsonBody = $body | ConvertTo-Json -Depth 20
    $resp = Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $jsonBody -Headers $headers -TimeoutSec 10
    
    if ($resp.data.__type) {
        Write-Host "=== UnshieldedTransaction fields ===" -ForegroundColor Cyan
        $resp.data.__type.fields | ForEach-Object {
            Write-Host "  $($_.name): $($_.type.name) ($($_.type.kind))"
        }
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)"
}

Write-Host ""

# Also check what unshieldedTransactions subscription filter args look like
$filterQuery = @"
query {
  __type(name: "UnshieldedTransactionsubscription") {
    name
    fields {
      name
      args {
        name
        type {
          name
          kind
          inputFields {
            name
            type {
              name
              kind
            }
          }
        }
      }
    }
  }
}
"@

$body2 = @{ query = $filterQuery }
try {
    $jsonBody2 = $body2 | ConvertTo-Json -Depth 20
    $resp2 = Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $jsonBody2 -Headers $headers -TimeoutSec 10
    
    if ($resp2.data.__type) {
        Write-Host "=== unshieldedTransactions subscription args ===" -ForegroundColor Yellow
        $resp2.data.__type.fields[0].args | ForEach-Object {
            Write-Host "Arg: $($_.name)"
            Write-Host "  Type: $($_.type.name) ($($_.type.kind))"
        }
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)"
}
