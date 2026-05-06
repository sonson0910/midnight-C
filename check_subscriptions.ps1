# Check GraphQL subscriptions in Midnight Indexer v4
$url = 'https://indexer.preview.midnight.network/api/v4/graphql'

$body = @{
    query = 'query { __schema { subscriptionType { name fields { name type { name kind ofType { name kind } } } } } }'
}

$headers = @{
    'Content-Type' = 'application/json'
    'Accept' = 'application/json'
}

try {
    $jsonBody = $body | ConvertTo-Json -Depth 10
    $resp = Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $jsonBody -Headers $headers -TimeoutSec 10
    
    if ($resp.data.__schema.subscriptionType) {
        Write-Host '=== GRAPHQL SUBSCRIPTION TYPES ===' -ForegroundColor Green
        $resp.data.__schema.subscriptionType.fields | ForEach-Object {
            Write-Host "  - $($_.name)"
        }
    } else {
        Write-Host 'No subscriptionType in schema'
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)"
}
