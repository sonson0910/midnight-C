"""
Fast UTXO query for our wallet.
Faucet tx: block 609178, id=30324, hash=0x0ca52f7d965277a59a207f30254240d3ae2aca76a3991ca18786e416ae169ac3
We need to find unshielding (our address gets tokens) and any follow-up txs.
"""
import urllib.request
import json
from urllib.request import Request

ADDR = "mn_addr_preview1vs80ttcyqrlpu74dvtaexz7tecaxx7gaa3l70gf445tht2d724hq5yfwah"
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

# Step 1: Get faucet tx details
print("=== Step 1: Faucet tx details ===")
r = gql("""query Get($h: HexEncoded!) {
    transactions(offset: { hash: $h }) {
        id hash block { height }
        unshieldedCreatedOutputs { owner value outputIndex }
        unshieldedSpentOutputs { owner value }
    }
}""", {"h": "0x0ca52f7d965277a59a207f30254240d3ae2aca76a3991ca18786e416ae169ac3"})
txs = r.get("data", {}).get("transactions", []) or []
print("Faucet txs:", len(txs))
for tx in txs:
    print(f"  id={tx['id']} hash={tx.get('hash','')[:20]} block={tx.get('block',{}).get('height')}")
    for out in tx.get("unshieldedCreatedOutputs", []):
        print(f"    CREATED: owner={out.get('owner','')[:40]} value={out.get('value')} idx={out.get('outputIndex')}")
    for out in tx.get("unshieldedSpentOutputs", []):
        print(f"    SPENT: owner={out.get('owner','')[:40]}")

# Step 2: Get blocks after 609178 using block-by-block scan to find the unshielding block
print("\n=== Step 2: Find unshielding tx (block where our address appears as output) ===")
FAUCET_HASH = "0x0ca52f7d965277a59a207f30254240d3ae2aca76a3991ca18786e416ae169ac3"

# The faucet tx is at block 609178. Now query blocks AFTER that to find unshielding.
# We know from previous tests that blocks with txs are sparse.
# Let's query a range of blocks quickly.
import time

for start in [609179, 609200, 609220, 609240, 609260, 609280, 609300, 609320, 609340, 609360,
               609380, 609400, 609420, 609440, 609460, 609480, 609500, 609520, 609540, 609560,
               609580, 609600, 609620, 609640, 609660, 609680, 609700, 609720, 609740, 609760,
               609780, 609800, 609820, 609840, 609860, 609880, 609900, 609920, 609940, 609960,
               609980, 610000, 610020, 610040, 610060, 610080, 610100, 610120, 610140, 610160,
               610174]:
    r = gql("""query Get($h: Int!) {
        block(offset: { height: $h }) {
            height hash
            transactions {
                id hash
                unshieldedCreatedOutputs { owner value outputIndex }
                unshieldedSpentOutputs { owner value }
            }
        }
    }""", {"h": start})
    blk = r.get("data", {}).get("block")
    if blk:
        txs_in_block = blk.get("transactions", []) or []
        for tx in txs_in_block:
            for out in tx.get("unshieldedCreatedOutputs", []) or []:
                if ADDR in out.get("owner", ""):
                    print(f"  FOUND unshielding: block={blk['height']} tx_id={tx['id']} value={out.get('value')} output_idx={out.get('outputIndex')}")
            for out in tx.get("unshieldedSpentOutputs", []) or []:
                if ADDR in out.get("owner", ""):
                    print(f"  FOUND spend: block={blk['height']} tx_id={tx['id']} value={out.get('value')}")
        if txs_in_block:
            print(f"  block {blk['height']}: {len(txs_in_block)} txs")
    time.sleep(0.05)

print("\n=== Done ===")
