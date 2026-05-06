import httpx

# Try querying by block height (single)
r = httpx.post('https://indexer.preview.midnight.network/api/v4/graphql', json={
    'query': '''
    query {
        transactions(
            offset: { height: 609200 }
        ) {
            id
            hash
            block { height }
        }
    }
    '''
}, timeout=30)
print("Height query:", r.json())

