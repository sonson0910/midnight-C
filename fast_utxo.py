"""
Fast UTXO discovery using transactions() with id offset.
"""
import urllib.request
import json
from urllib.request import Request

ADDR = "mn_addr_preview1vs80ttcyqrlpu74dvtaexz7tecaxx7gaa3l70gf445tht2d724hq5yfwah"
ADDR_SHORT = ADDR[:50]
INDEXER_URL = "https://indexer.preview.midnight.network/api/v4/graphql"

def gql(query, vars=None):
    body = {"query": query}
    if vars:
        body["variables"] = vars
    data = json.dumps(body).encode()
    req = Request(INDEXER_URL, data=data, headers={"Content-Type": "application/json"})
    r = json.loads(urllib.request.urlopen(req, timeout=15).read())
    if r.get("errors"):
        print("  ERR:", r["errors"][0]["message"][:120])
    return r

# Test: does transactions() take offset with id?
r = gql("""query Get($id: Int) {
    transactions(offset: { id: $id }) {
        id hash block { height }
    }
}""", {"id": 30324})
print("offset:{id:30324}:", [(t["id"], t.get("block",{}).get("height")) for t in r.get("data",{}).get("transactions",[]) or []])

r = gql("""query Get($id: Int) {
    transactions(offset: { id: $id }) {
        id hash block { height }
    }
}""", {"id": 30325})
print("offset:{id:30325}:", [(t["id"], t.get("block",{}).get("height")) for t in r.get("data",{}).get("transactions",[]) or []])

r = gql("""query Get($id: Int) {
    transactions(offset: { id: $id }) {
        id hash block { height }
    }
}""", {"id": 0})
print("offset:{id:0}:", [(t["id"], t.get("block",{}).get("height")) for t in r.get("data",{}).get("transactions",[]) or []])

# Now get ALL from 30325 onwards - this should give us EVERYTHING
print("\n=== ALL transactions from id=30325 ===")
r = gql("""query Get($id: Int) {
    transactions(offset: { id: $id }) {
        id hash block { height }
        unshieldedCreatedOutputs { owner value outputIndex }
        unshieldedSpentOutputs { owner value }
    }
}""", {"id": 30325})
all_txs = r.get("data", {}).get("transactions", []) or []
print(f"Got {len(all_txs)} txs from 30325 onwards")
total_created = 0
total_spent = 0
for t in all_txs:
    outs = t.get("unshieldedCreatedOutputs", []) or []
    spents = t.get("unshieldedSpentOutputs", []) or []
    our_outs = [o for o in outs if ADDR_SHORT in o.get("owner","")]
    our_spents = [o for o in spents if ADDR_SHORT in o.get("owner","")]
    for o in our_outs:
        print(f"  **OUR CREATED** tx {t['id']} block={t.get('block',{}).get('height')} value={o.get('value')}")
        total_created += int(o.get('value', 0))
    for o in our_spents:
        print(f"  **OUR SPENT** tx {t['id']} block={t.get('block',{}).get('height')} value={o.get('value')}")
        total_spent += int(o.get('value', 0))

print(f"\nTotal CREATED: {total_created} = {total_created/1e8:.2f} NIGHT")
print(f"Total SPENT: {total_spent} = {total_spent/1e8:.2f} NIGHT")
print(f"Net balance: {total_created - total_spent} = {(total_created - total_spent)/1e8:.2f} NIGHT")
