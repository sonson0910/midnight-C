$graphql = 'https://indexer.preprod.midnight.network/api/v4/graphql'

Write-Host "=== Get Block type fields ===" -ForegroundColor Cyan

$query = @"
{
  __type(name: "Block") {
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
"@

try {
    $r = Invoke-RestMethod -Uri $graphql -Method Post -ContentType 'application/json' -Body (@{ query = $query } | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "Block type fields:" -ForegroundColor Green
    $r.data.__type.fields | ForEach-Object {
        Write-Host "  $($_.name): $($_.type.name) ($($_.type.kind))"
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Get Transaction type fields ===" -ForegroundColor Cyan

$query2 = @"
{
  __type(name: "Transaction") {
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
"@

try {
    $r2 = Invoke-RestMethod -Uri $graphql -Method Post -ContentType 'application/json' -Body (@{ query = $query2 } | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "Transaction type fields:" -ForegroundColor Green
    $r2.data.__type.fields | ForEach-Object {
        Write-Host "  $($_.name): $($_.type.name) ($($_.type.kind))"
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Try block with hash ===" -ForegroundColor Cyan
$query3 = @"
{
  block(hash: "0x939906f65687c6250fce1eb21a59737f23b382e4a8335004d2b9dd9de33ecc2c") {
    hash
    id
  }
}
"@

try {
    $r3 = Invoke-RestMethod -Uri $graphql -Method Post -ContentType 'application/json' -Body (@{ query = $query3 } | ConvertTo-Json -Compress) -TimeoutSec 30
    Write-Host "Block result:" -ForegroundColor Green
    $r3 | ConvertTo-Json -Depth 10
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}
