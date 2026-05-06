$url = 'https://indexer.preview.midnight.network/api/v4/graphql'
$headers = @{'Content-Type' = 'application/json'}

$body = '{"query":"query { __type(name: \"UnshieldedTransaction\") { name kind fields { name type { name kind ofType { name kind } } } } }"}'
try {
    $resp = Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $body -Headers $headers -TimeoutSec 10
    if ($resp.data.__type) {
        Write-Host '=== UnshieldedTransaction fields ==='
        foreach ($f in $resp.data.__type.fields) {
            Write-Host "  $($f.name)"
        }
    }
} catch { Write-Host "Error1: $($_.Exception.Message)" }

Write-Host ''

$body2 = '{"query":"query { __type(name: \"UnshieldedTransactionsubscription\") { name fields { name args { name type { name kind } } } } }"}'
try {
    $resp2 = Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $body2 -Headers $headers -TimeoutSec 10
    if ($resp2.data.__type) {
        Write-Host '=== unshieldedTransactions subscription ==='
        foreach ($f in $resp2.data.__type.fields) {
            Write-Host "Subscription: $($f.name)"
            foreach ($a in $f.args) {
                Write-Host "  Arg: $($a.name) Type: $($a.type.name)"
            }
        }
    }
} catch { Write-Host "Error2: $($_.Exception.Message)" }
