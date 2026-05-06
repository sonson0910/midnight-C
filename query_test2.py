#!/usr/bin/env python3
"""
Tìm cách query hiệu quả nhất cho Midnight UTXO balance.
Phát hiện quan trọng:
1. Main network indexer không accessible từ máy này
2. Faucet tx ở preview network (indexer.preview.midnight.network)
3. transactions(offset: { identifier: hex }) = hex of decimal id
4. block(offset: { height: N }) = cách duy nhất scan blocks
"""
import urllib.request
import json
import time

PREVIEW_INDEXER_URL = "https://indexer.preview.midnight.network/api/v4/graphql"
FAUCET_HASH = "0x0ca52f7d965277a59a207f30254240d3ae2aca76a3991ca18786e416ae169ac3"


def gql(query, vars=None):
    body = {"query": query}
    if vars:
        body["variables"] = vars
    data = json.dumps(body).encode()
    req = urllib.request.Request(PREVIEW_INDEXER_URL, data=data,
                                  headers={"Content-Type": "application/json"})
    r = json.loads(urllib.request.urlopen(req, timeout=30).read())
    if r.get("errors"):
        print(f"  ERR: {r['errors'][0]['message'][:120]}")
    return r


def test1_query_faucet_by_hash():
    """TEST 1: Query faucet tx bằng hash."""
    print("\n=== TEST 1: Query faucet tx by hash ===")
    query = """
    query GetTx($hash: String!) {
        transactions(offset: { hash: $hash }) {
            id
            hash
            block { height }
            unshieldedCreatedOutputs {
                owner
                value
                tokenType
                outputIndex
            }
            unshieldedSpentOutputs {
                owner
                value
                tokenType
                outputIndex
            }
        }
    }
    """
    r = gql(query, {"hash": FAUCET_HASH})
    txs = r.get("data", {}).get("transactions", [])
    print(f"  Results: {len(txs)} transactions")
    for tx in txs:
        print(f"  tx id={tx['id']} block={tx['block']['height']}")
        for utxo in tx.get("unshieldedCreatedOutputs", []):
            print(f"    CREATED: value={utxo['value']} token={utxo['tokenType']} owner={utxo['owner'][:30]}...")
        for utxo in tx.get("unshieldedSpentOutputs", []):
            print(f"    SPENT: value={utxo['value']} token={utxo['tokenType']} owner={utxo['owner'][:30]}...")


def test2_query_by_identifier():
    """TEST 2: Query tx bằng identifier (hex of decimal id)."""
    print("\n=== TEST 2: Query by identifier (hex of decimal id) ===")

    # faucet tx id=30324 decimal, hex="7654"
    # Thử với prefix "0x" và không prefix
    for ident in ["7654", "0x7654"]:
        query = """
        query GetTx($ident: String!) {
            transactions(offset: { identifier: $ident }) {
                id
                hash
                block { height }
                unshieldedCreatedOutputs {
                    owner value tokenType outputIndex
                }
            }
        }
        """
        r = gql(query, {"ident": ident})
        txs = r.get("data", {}).get("transactions", [])
        print(f"  identifier='{ident}': {len(txs)} txs")
        if txs:
            for tx in txs[:3]:
                print(f"    tx id={tx['id']} block={tx['block']['height']} hash={tx['hash'][:20]}...")


def test3_block_sequential():
    """TEST 3: Block query sequential (20 blocks)."""
    print("\n=== TEST 3: Block query sequential (20 blocks from 609178) ===")
    query = """
    query GetBlock($h: Int!) {
        block(offset: { height: $h }) {
            height
            transactions {
                id
                hash
                unshieldedCreatedOutputs { owner value tokenType outputIndex }
                unshieldedSpentOutputs { owner value tokenType outputIndex }
            }
        }
    }
    """
    start = time.time()
    for h in range(609178, 609198):
        r = gql(query, {"h": h})
        block = r.get("data", {}).get("block")
        if block:
            txs = block.get("transactions", [])
            if txs:
                for tx in txs[:2]:
                    created = len(tx.get("unshieldedCreatedOutputs", []))
                    spent = len(tx.get("unshieldedSpentOutputs", []))
                    if created or spent:
                        print(f"  Block {h}: tx {tx['id']} created={created} spent={spent}")
    elapsed = time.time() - start
    print(f"  20 blocks took {elapsed:.1f}s ({elapsed/20:.3f}s per block)")


def test4_block_concurrent():
    """TEST 4: Block query concurrent (200 blocks)."""
    print("\n=== TEST 4: Block query CONCURRENT (200 blocks) ===")
    import concurrent.futures

    query = """
    query GetBlock($h: Int!) {
        block(offset: { height: $h }) {
            height
            transactions {
                id
                unshieldedCreatedOutputs { owner value tokenType outputIndex }
                unshieldedSpentOutputs { owner value tokenType outputIndex }
            }
        }
    }
    """

    def fetch_block(h):
        try:
            body = json.dumps({"query": query, "variables": {"h": h}}).encode()
            req = urllib.request.Request(PREVIEW_INDEXER_URL, data=body,
                                        headers={"Content-Type": "application/json"})
            r = json.loads(urllib.request.urlopen(req, timeout=30).read())
            if r.get("errors"):
                return None
            return r.get("data", {}).get("block")
        except Exception as e:
            return None

    start = time.time()
    heights = list(range(609178, 609378))  # 200 blocks

    with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
        results = list(executor.map(fetch_block, heights))

    elapsed = time.time() - start
    blocks_found = sum(1 for b in results if b is not None)
    txs_found = sum(len(b.get("transactions", [])) for b in results if b)
    utxos_created = sum(
        sum(len(tx.get("unshieldedCreatedOutputs", [])) for tx in b.get("transactions", []))
        for b in results if b
    )
    utxos_spent = sum(
        sum(len(tx.get("unshieldedSpentOutputs", [])) for tx in b.get("transactions", []))
        for b in results if b
    )

    print(f"  {len(heights)} blocks in {elapsed:.1f}s ({elapsed/len(heights):.3f}s per block)")
    print(f"  Blocks with data: {blocks_found}")
    print(f"  Total txs: {txs_found}")
    print(f"  Total unshieldedCreatedOutputs: {utxos_created}")
    print(f"  Total unshieldedSpentOutputs: {utxos_spent}")

    # Estimate full scan time
    r = gql("{ block { height } }")
    tip = r.get("data", {}).get("block", {}).get("height", 609178)
    blocks_remaining = tip - 609178
    estimated = blocks_remaining * elapsed / len(heights)
    print(f"\n  Current tip: {tip}")
    print(f"  Blocks from 609178 to tip: {blocks_remaining}")
    print(f"  Estimated full scan time: {estimated:.0f}s = {estimated/60:.1f} min")


def test5_derive_address():
    """TEST 5: Derive unshielded address để filter UTXOs."""
    print("\n=== TEST 5: Derive unshielded address ===")
    try:
        from mnemonic import Mnemonic
        import hashlib

        mn = "genre volcano salute hurry motion talk kingdom gym doctor high code cattle bone ecology recall exotic shed cattle decide reform argue ability melt face"
        mp = Mnemonic("english")
        seed = mp.to_seed(mn)

        # Unshielded key derivation: BIP44 path m/44'/2400'/0'/0/0
        # Midnight uses SLIP-0044 coin type 2400
        # For unshielded, use the NIGHT key (role 0)
        # Derive: privkey = seed[0:32]
        priv = seed[0:32]

        # Hash privkey twice for pubkey
        h1 = hashlib.sha256(priv).digest()
        h2 = hashlib.sha256(h1).digest()
        pubkey = h2

        # ripemd160 of sha256 of pubkey
        r160 = hashlib.new('ripemd160', h2).digest()

        # bech32m encode: midnight1 + data
        # data = version(0x00) + r160
        addr_bytes = bytes([0x00]) + r160

        # bech32 encoding
        CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"

        def bech32_polymod(values):
            generators = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3]
            chk = 1
            for v in values:
                top = chk >> 35
                chk = ((chk & 0x07ffffffff) << 5) ^ v
                for i in range(5):
                    chk ^= generators[i] if ((top >> i) & 1) else 0
            return chk

        def bech32_hrp_expand(hrp):
            return [ord(x) >> 5 for x in hrp] + [0] + [ord(x) & 31 for x in hrp]

        def convertbits(data, frombits, tobits, pad=True):
            acc = 0
            bits = 0
            ret = []
            maxv = (1 << tobits) - 1
            maxacc = (1 << (frombits + tobits - 1)) - 1
            for value in data:
                acc = ((acc << frombits) | value) & maxacc
                bits += frombits
                while bits >= tobits:
                    bits -= tobits
                    ret.append((acc >> bits) & maxv)
            if pad:
                if bits > 0:
                    ret.append((acc << (tobits - bits)) & maxv)
            return ret

        def bech32_encode(hrp, data):
            combined = convertbits(data, 8, 5)
            values = bech32_hrp_expand(hrp) + combined
            polymod = bech32_polymod(values + [0]*6)
            values += [(polymod >> 5 * (5 - i)) & 31 for i in range(6)]
            return hrp + '1' + ''.join(CHARSET[v] for v in values)

        addr = bech32_encode("midnight", list(addr_bytes))
        print(f"  Unshielded address: {addr}")
        print(f"  Pubkey (32 bytes): {pubkey.hex()}")
        print(f"  r160: {r160.hex()}")

    except ImportError as e:
        print(f"  mnemonic library not installed: {e}")


def main():
    print("=" * 60)
    print("MIDNIGHT UTXO QUERY OPTIMIZATION TESTS")
    print("Using PREVIEW network:", PREVIEW_INDEXER_URL)
    print("=" * 60)

    test1_query_faucet_by_hash()
    test2_query_by_identifier()
    test3_block_sequential()
    test4_block_concurrent()
    test5_derive_address()

    print("\n" + "=" * 60)
    print("CONCLUSIONS:")
    print("  1. Faucet tx at block 609178 (preview network)")
    print("  2. transactions(offset: identifier=hex) = hex of decimal id")
    print("  3. block(offset: { height: N }) = best for block scanning")
    print("  4. Concurrent requests = ~5x faster than sequential")
    print("=" * 60)


if __name__ == "__main__":
    main()
