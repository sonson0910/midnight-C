import urllib.request
import json
import time
from urllib.request import Request

ADDR = "mn_addr_preview1vs80ttcyqrlpu74dvtaexz7tecaxx7gaa3l70gf445tht2d724hq5yfwah"
FAUCET_HASH = "0x0ca52f7d965277a59a207f30254240d3ae2aca76a3991ca18786e416ae169ac3"
INDEXER_URL = "https://indexer.preview.midnight.network/api/v4/graphql"

def gql(query, vars=None):
    body = {"query": query}
    if vars:
        body["variables"] = vars
    data = json.dumps(body).encode()
    req = Request(INDEXER_URL, data=data, headers={"Content-Type": "application/json"})
    r = json.loads(urllib.request.urlopen(req, timeout=15).read())
    if r.get("errors"):
        print("  ERR:", r["errors"][0]["message"][:80])
    return r

print("=== APPROACH 1: block(offset: { height: N }) ===")
r = gql("query Get($h: Int!) { block(offset: { height: $h }) { height hash parent { height hash } } }", {"h": 609178})
print("block(h=609178):", json.dumps(r.get("data", {})))
r = gql("query Get($h: Int!) { block(offset: { height: $h }) { height hash } }", {"h": 609179})
print("block(h=609179):", json.dumps(r.get("data", {})))

print("\n=== APPROACH 2: transactions(offset: { identifier }) - hex ===")
# identifier is hex-encoded, needs even number of hex digits
# Try valid hex values
for ident in ["00", "0000", "0000000000000000000000000000000000000000000000000000000000000000"]:
    r = gql("query Get($id: HexEncoded!) { transactions(offset: { identifier: $id }) { id hash block { height } } }", {"id": ident})
    txs_data = r.get("data")
    txs = txs_data.get("transactions", []) if txs_data else []
    print("  id=%s: %d txs" % (ident[:16], len(txs)))
    if txs:
        print("    first id=%s hash=%s" % (txs[0].get("id"), txs[0].get("hash", "")[:20]))

print("\n=== APPROACH 3: Get faucet tx, then next block ===")
r = gql("query Get($h: HexEncoded!) { transactions(offset: { hash: $h }) { id hash block { height hash } } }", {"h": FAUCET_HASH})
txs = r.get("data", {}).get("transactions", [])
if txs is None:
    txs = []
if txs:
    tx = txs[0]
    blk = tx.get("block", {})
    blk_height = blk.get("height")
    print("Faucet tx: id=%s, block_height=%s" % (tx.get("id"), blk_height))
    
    rpc_body = json.dumps({"jsonrpc": "2.0", "method": "chain_getBlockHash", "params": [blk_height + 1], "id": 1}).encode()
    rpc_req = Request("https://rpc.preview.midnight.network", data=rpc_body, headers={"Content-Type": "application/json"})
    rpc_r = json.loads(urllib.request.urlopen(rpc_req, timeout=10).read())
    next_blk_hash = rpc_r.get("result")
    print("Next block hash:", next_blk_hash[:20])
    
    r2 = gql("query Get($h: HexEncoded!) { transactions(offset: { hash: $h }) { id hash block { height } unshieldedCreatedOutputs { owner value } } }", {"h": next_blk_hash})
    txs_data2 = r2.get("data")
    txs2 = txs_data2.get("transactions", []) if txs_data2 else []
    print("From next block hash: %d txs" % len(txs2))
    for t in txs2[:5]:
        print("  TX id=%s hash=%s block=%s" % (t.get("id"), t.get("hash", "")[:20], t.get("block", {}).get("height")))
        for out in t.get("unshieldedCreatedOutputs", []):
            if ADDR in out.get("owner", ""):
                print("    CREATED our: %s" % out.get("value"))
else:
    print("No txs found for faucet hash")

print("\n=== APPROACH 4: Block range ===")
r = gql("{ block { height } }")
tip = r.get("data", {}).get("block", {}).get("height")
print("Latest block: %d" % tip)
print("Blocks from 609178: %d" % (tip - 609178))
print("Time at 1s/block: ~%d seconds = ~%.1f min" % (tip - 609178, (tip - 609178) / 60.0))
