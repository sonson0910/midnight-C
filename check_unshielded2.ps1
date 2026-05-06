$url = 'https://indexer.preview.midnight.network/api/v4/graphql'
$headers = @{'Content-Type' = 'application/json'}

# Check UnshieldedTransaction fields
$body = @{
    query = 'query { __type(name: "UnshieldedTransaction") { name kind fields { name type { name kind ofType { name kind } } } } }'
}
try {
    $jsonBody = $body | ConvertTo-Json -Depth 20
    $resp = Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $jsonBody -Headers $headers -TimeoutSec 10
    if ($resp.data.__type) {
        Write-Host '=== UnshieldedTransaction fields ==='
        $resp.data.__type.fields | ForEach-Object {
            $t = $_.type.name
            if (-not $t) { $t = $_.type.ofType.name }
            Write-Host "  $($_.name): $t"
        }
    }
} catch { Write-Host "Error: $($_.Exception.Message)" }

Write-Host ''

# Check unshieldedTransactions args
$body2 = @{
    query = 'query { __type(name: "UnshieldedTransactionsubscription") { name fields { name args { name type { name kind inputFields { name type { name kind } } } } } } }'
}
try {
    $jsonBody2 = $body2 | ConvertTo-Json -Depth 20
    $resp2 = Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $jsonBody2 -Headers $headers -TimeoutSec 10
    if ($resp2.data.__type) {
        Write-Host '=== unshieldedTransactions subscription args ==='
        $resp2.data.__type.fields[0].args | ForEach-Object {
            Write-Host "Arg: $($_.name) Type: $($_.type.name)"
            if ($_.type.inputFields) {
                $_.type.inputFields | ForEach-Object {
                    Write-Host "  - $($_.name): $($_.type.name)"
                }
            }
        }
    }
} catch { Write-Host "Error2: $($_.Exception.Message)" }
