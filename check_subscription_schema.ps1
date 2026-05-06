# Check unshieldedTransactions subscription schema
$url = 'https://indexer.preview.midnight.network/api/v4/graphql'

$query = @"
query {
  __type(name: "UnshieldedTransactionsubscription") {
    name
    kind
    fields {
      name
      type {
        name
        kind
        ofType {
          name
          kind
          fields {
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
        Write-Host "=== unshieldedTransactions subscription type ===" -ForegroundColor Cyan
        $resp.data.__type.fields | ForEach-Object {
            Write-Host "Field: $($_.name)"
            if ($_.type.ofType.fields) {
                $_.type.ofType.fields | ForEach-Object {
                    Write-Host "  - $($_.name): $($_.type.name)"
                }
            }
        }
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)"
}

Write-Host ""
Write-Host "=== Checking __schema subscription root ===" -ForegroundColor Yellow

$schemaQuery = @"
query {
  __schema {
    subscriptionType {
      name
      fields {
        name
        args {
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
"@

$body2 = @{ query = $schemaQuery }
try {
    $jsonBody2 = $body2 | ConvertTo-Json -Depth 10
    $resp2 = Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $jsonBody2 -Headers $headers -TimeoutSec 10
    
    if ($resp2.data.__schema.subscriptionType) {
        Write-Host "Subscription root fields:" -ForegroundColor Cyan
        $resp2.data.__schema.subscriptionType.fields | ForEach-Object {
            $args = ($_.args | ForEach-Object { "$($_.name):$($_.type.name)" }) -join ", "
            if ($args) {
                Write-Host "  - $($_.name) ($args)"
            } else {
                Write-Host "  - $($_.name)"
            }
        }
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)"
}
