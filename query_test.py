#!/usr/bin/env python3
"""
Cách hiệu quả nhất để query UTXO balance:
1. Dùng block(offset: { height: N }) để lấy tất cả transactions trong block
2. Lọc unshieldedCreatedOutputs/spentOutputs cho address của mình
3. Scan từng block từ faucet tx (609178) trở đi

Script này test trên MAIN network với IndexerClient.
"""
import urllib.request
import json
import time
from urllib.request import Request

INDEXER_URL = "https://indexer.midnight.network/graphql"
# Preview network
PREVIEW_INDEXER_URL = "https://indexer.preview.midnight.network/api/v4/graphql"

FAUCET_HASH = "0x0ca52f7d965277a59a207f30254240d3ae2aca76a3991ca18786e416ae169ac3"
MNEMONIC = "genre volcano salute hurry motion talk kingdom gym doctor high code cattle bone ecology recall exotic shed cattle decide reform argue ability melt face"


def gql(url, query, vars=None):
    body = {"query": query}
    if vars:
        body["variables"] = vars
    data = json.dumps(body).encode()
    req = Request(url, data=data, headers={"Content-Type": "application/json"})
    r = json.loads(urllib.request.urlopen(req, timeout=15).read())
    if r.get("errors"):
        print(f"  ERR: {r['errors'][0]['message'][:120]}")
    return r


def derive_unshielded_address(mnemonic: str) -> str:
    """Derive unshielded address từ mnemonic sử dụng Python."""
    try:
        from mnemonic import Mnemonic
        import secp256k1

        mp = Mnemonic(language="english")
        seed = mp.to_seed(mnemonic)
        priv_key = seed[:32]

        if not secp256k1.has_ribbd():
            secp256k1.ecdh = secp256k1.ecdh_raw

        pub_key = secp256k1.PublicKey(priv_key, raw=True)
        shared = secp256k1.ecdh(pub_key.pubkey_serialize(compressed=False), priv_key, raw=True)
        hashed = hashlib.sha256(shared).digest()

        import hashlib
        hashed = hashlib.sha256(shared).digest()
        from bech32 import bech32_encode, convertbits
        from typing import List

        def bech32_char(val: int) -> str:
            return "qpzry9x8gf2tvdw0s3jn54khce6mua7l" [val & 31]

        def bech32_hrp_expand(hrp: str) -> List[int]:
            return [ord(x) >> 5 for x in hrp] + [0] + [ord(x) & 31 for x in hrp]

        def bech32_create_data(hrp: str, data: List[int]) -> List[int]:
            values = bech32_hrp_expand(hrp) + data
            polymod = bech32_polymod(values + [0, 0, 0, 0, 0, 0])
            return values[:6] + [(polymod >> 5 * (5 - i)) & 31 for i in range(6)]

        def bech32_polymod(values: List[int]) -> int:
            GENERATORS = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3]
            chk = 1
            for value in values:
                top = chk >> 35
                chk = ((chk & 0x07ffffffff) << 5) ^ value
                for i in range(5):
                    chk ^= GENERATORS[i] if ((top >> i) & 1) else 0
            return chk

        def bech32_encode_addr(hrp: str, data: List[int]) -> str:
            combined = convertbits(data, 8, 5)
            ret_bech32 = bech32_hrp_expand(hrp) + combined
            polymod = bech32_polymod(ret_bech32 + [0, 0, 0, 0, 0, 0])
            values = convertbits(data, 8, 5) + [(polymod >> 5 * (5 - i)) & 31 for i in range(6)]
            return hrp + '1' + ''.join(bech32_char(v) for v in values)

        def convertbits(data: bytes, frombits: int, tobits: int, pad: bool = True) -> List[int]:
            acc = 0
            bits = 0
            ret = []
            maxv = (1 << tobits) - 1
            maxacc = (1 << (frombits + tobits - 1)) - 1
            for value in data:
                if value < 0 or (value >> frombits):
                    return None
                acc = ((acc << frombits) | value) & maxacc
                bits += frombits
                while bits >= tobits:
                    bits -= tobits
                    ret.append((acc >> bits) & maxv)
            if pad:
                if bits > 0:
                    ret.append((acc << (tobits - bits)) & maxv)
            elif bits >= frombits or ((acc << (tobits - bits)) & maxv):
                return None
            return ret

        def compute_address(pubkey_bytes: bytes) -> str:
            h = hashlib.sha256(pubkey_bytes).digest()
            r = hashlib.new('ripemd160', h).digest()
            return bech32_encode_addr('midnight', [0] + list(r[:20]))

        # Simple: just hash the private key to get pubkey
        # Use the last 32 bytes of seed as privkey
        privkey_bytes = seed[0:32]

        # For unshielded address: use privkey directly as input to hash
        h = hashlib.sha256(privkey_bytes).digest()
        h2 = hashlib.sha256(h).digest()
        pubkey = h2  # 32 bytes

        addr = compute_address(pubkey)
        return addr
    except Exception as e:
        print(f"Derivation error: {e}")
        return None


def get_address_from_midnight_lib():
    """Try using midnight lib if available."""
    try:
        import subprocess
        result = subprocess.run(
            ["npx", "ts-node", "-e", """
const { Derive } = require('@midnight-ntwrk/midnight-wallet');
const mnemonic = 'genre volcano salute hurry motion talk kingdom gym doctor high code cattle bone ecology recall exotic shed cattle decide reform argue ability melt face';
console.log(JSON.stringify({ mnemonic }));
"""],
            capture_output=True, text=True, timeout=10
        )
        print(f"npx result: {result.stdout[:200]} {result.stderr[:200]}")
    except Exception as e:
        print(f"npx error: {e}")


def test_transactions_by_identifier():
    """Test: query transactions bằng identifier (hex-encoded tx id)."""
    print("\n=== TEST: transactions(offset: { identifier: hex_id }) ===")

    # faucet tx id=30324 decimal, hex = 0x7654
    # identifier cần hex string, không phải số
    tx_id_decimal = 30324
    tx_id_hex = format(tx_id_decimal, 'x')  # "7654"
    print(f"Querying tx id={tx_id_decimal}, hex='{tx_id_hex}'")

    query = """
    query GetTx($identifier: String!) {
        transactions(offset: { identifier: $identifier }) {
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
    r = gql(INDEXER_URL, query, {"identifier": tx_id_hex})
    txs = r.get("data", {}).get("transactions", [])
    print(f"Results: {len(txs)} transactions")
    for tx in txs:
        print(f"  tx id={tx['id']} block={tx['block']['height']}")
        for utxo in tx.get("unshieldedCreatedOutputs", []):
            print(f"    CREATED: value={utxo['value']} token={utxo['tokenType']} owner={utxo['owner'][:30]}...")


def test_transactions_by_hash():
    """Test: query faucet tx bằng hash."""
    print("\n=== TEST: transactions(offset: { hash: faucet_hash }) ===")
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
    r = gql(INDEXER_URL, query, {"hash": FAUCET_HASH})
    txs = r.get("data", {}).get("transactions", [])
    print(f"Results: {len(txs)} transactions")
    for tx in txs:
        print(f"  tx id={tx['id']} block={tx['block']['height']}")
        for utxo in tx.get("unshieldedCreatedOutputs", []):
            print(f"    CREATED: value={utxo['value']} token={utxo['tokenType']} owner={utxo['owner'][:40]}...")
        for utxo in tx.get("unshieldedSpentOutputs", []):
            print(f"    SPENT: value={utxo['value']} token={utxo['tokenType']} owner={utxo['owner'][:40]}...")


def test_block_query():
    """Test: query từng block để tìm UTXO."""
    print("\n=== TEST: block(offset: { height: N }) - sequential ===")
    query = """
    query GetBlock($height: Int!) {
        block(offset: { height: $height }) {
            height
            hash
            transactions {
                id
                hash
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
    }
    """

    # Test 20 blocks từ 609178 trở đi
    start = time.time()
    for height in range(609178, 609198):
        r = gql(INDEXER_URL, query, {"height": height})
        block = r.get("data", {}).get("block")
        if not block:
            continue
        txs = block.get("transactions", [])
        if txs:
            print(f"  Block {height}: {len(txs)} txs")
            for tx in txs:
                for utxo in tx.get("unshieldedCreatedOutputs", []):
                    print(f"    CREATED: {utxo['value']} {utxo['tokenType']}")

    elapsed = time.time() - start
    print(f"  20 blocks took {elapsed:.1f}s ({elapsed/20:.2f}s per block)")


def test_derive_address():
    """Derive unshielded address từ mnemonic."""
    print("\n=== Deriving unshielded address ===")
    try:
        from mnemonic import Mnemonic
        import hashlib
        mp = Mnemonic(language="english")
        seed = mp.to_seed(MNEMONIC)
        print(f"Seed length: {len(seed)}")

        # Unshielded: sử dụng privkey từ seed để derive pubkey
        # Sau đó hash để tạo address
        priv = seed[0:32]

        # Cách Midnight làm: hash privkey 2 lần
        h1 = hashlib.sha256(priv).digest()
        h2 = hashlib.sha256(h1).digest()
        print(f"Derived pubkey (32 bytes): {h2.hex()}")

        # Address = bech32(midnight, 0 || ripemd160(sha256(pubkey)))
        r160 = hashlib.new('ripemd160', h2).digest()
        print(f"ripemd160: {r160.hex()}")

        # Encode as bech32m
        # midnight address format: midnight1 + bech32 encoded data
        # data = 0x00 (version) + 20 bytes of r160
        addr_bytes = bytes([0x00]) + r160
        print(f"Address bytes: {addr_bytes.hex()}")
        print(f"Address (bech32m): midnight1{addr_bytes.hex()}")

    except ImportError as e:
        print(f"mnemonic library not available: {e}")


def main():
    print("=" * 60)
    print("MAIN NETWORK QUERY TESTS")
    print("=" * 60)

    # Test 1: Query by hash
    test_transactions_by_hash()

    # Test 2: Query by identifier
    test_transactions_by_identifier()

    # Test 3: Block query
    test_block_query()

    # Test 4: Derive address
    test_derive_address()


if __name__ == "__main__":
    main()
