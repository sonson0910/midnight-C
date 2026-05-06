import httpx

r = httpx.post('https://indexer.preview.midnight.network/api/v4/graphql', json={
    'query': '''
    query { 
        transactions(limit: 1, where: { hash: { eq: "0x0ca52f7d965277a59a207f30254240d3ae2aca76a3991ca18786e416ae169ac3" } }) { 
            id 
            hash 
            block { height timestamp } 
        } 
    }
    '''
}, timeout=30)
print(r.json())
