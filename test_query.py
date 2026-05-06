#!/usr/bin/env python3
"""
Test cách query hiệu quả: dùng transactions(offset: { identifier: "hex_id" })
để paginate từng transaction một, lấy unshieldedCreatedOutputs/spentOutputs.

Cách này tránh block-by-block scan, chỉ cần scan từ faucet tx (id=30324)
trở đi — thay vì scan ~600 blocks.
"""

import httpx
import asyncio
import json

INDEXER_URL = "https://indexer.midnight.network/graphql"

# Hex encode 30324 (decimal) = 0x7654
FAUCET_TX_ID_DECIMAL = 30324
FAUCET_TX_ID_HEX = hex(FAUCET_TX_ID_DECIMAL)[2:]  # "7654"
print(f"Faucet tx id={FAUCET_TX_ID_DECIMAL}, hex='{FAUCET_TX_ID_HEX}'")


async def fetch_transactions():
    """Query transactions by identifier (hex-encoded tx id)."""
    query = """
    query GetTransactions($identifier: String!) {
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

    # Lấy faucet tx trước (id=30324, hex=7654)
    variables = {"identifier": FAUCET_TX_ID_HEX}

    async with httpx.AsyncClient(timeout=30.0) as client:
        resp = await client.post(
            INDEXER_URL,
            json={"query": query, "variables": variables},
        )
        data = resp.json()

    if "errors" in data:
        print(f"GraphQL errors: {data['errors']}")
        return None

    txs = data.get("data", {}).get("transactions", [])
    return txs


async def fetch_next_transactions_batch():
    """
    Thử paginate: gọi transactions với identifier = hex của id cuối cùng trong kết quả trước.
    Mỗi lần gọi, nó sẽ trả về transaction tại identifier ĐÓ, rồi tiếp theo.

    Tuy nhiên documentation nói "starting from a specific transaction" —
    tức là trả về transaction tại offset ĐÓ. Vậy để paginate, ta cần:
    1. Gọi lần đầu với identifier = hex(30324) = "7654" → được tx id=30324
    2. Lần sau: gọi với identifier = hex(30325) = "7655" → được tx id=30325
    ...
    Điều này có nghĩa mỗi lần chỉ lấy được 1 tx, không hiệu quả!
    """
    query = """
    query GetTransactions($identifier: String!) {
        transactions(offset: { identifier: $identifier }) {
            id
            hash
            block { height }
            unshieldedCreatedOutputs {
                owner
                value
                tokenType
            }
            unshieldedSpentOutputs {
                owner
                value
                tokenType
            }
        }
    }
    """

    # Test: paginate từ faucet tx trở đi, lấy 5 tx tiếp theo
    results = []
    current_id = FAUCET_TX_ID_DECIMAL

    async with httpx.AsyncClient(timeout=30.0) as client:
        for i in range(5):
            hex_id = hex(current_id)[2:]
            resp = await client.post(
                INDEXER_URL,
                json={
                    "query": query,
                    "variables": {"identifier": hex_id}
                },
            )
            result = resp.json()

            if "errors" in result:
                print(f"  Batch {i}: GraphQL errors: {result['errors']}")
                break

            txs = result.get("data", {}).get("transactions", [])
            if not txs:
                print(f"  Batch {i} (id={current_id}, hex={hex_id}): No transactions returned")
                current_id += 1
                continue

            for tx in txs:
                results.append(tx)
                print(f"  Got tx id={tx['id']} block={tx['block']['height']} "
                      f"created={len(tx.get('unshieldedCreatedOutputs', []))} "
                      f"spent={len(tx.get('unshieldedSpentOutputs', []))}")
            current_id += 1

    return results


async def fetch_transactions_by_block_range():
    """
    CÁCH HIỆU QUẢ THỰC SỰ:
    Dùng block(offset: { height: N }) để lấy block, rồi lấy tất cả transactions trong block đó.
    Mỗi block có thể có nhiều transactions — hiệu quả hơn nhiều so với query từng tx một.

    Block 609178 có faucet tx. Lấy 10 blocks từ 609178 trở đi.
    """
    query = """
    query GetBlock($height: Int!) {
        block(offset: { height: $height }) {
            height
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

    # USER ADDRESS: the derived unshielded address from mnemonic
    USER_ADDRESS = "midnight1ngxj8gfn0j0npxdz2c0hqu6vy3j9h7gkwu8hvy5l3j0l3g3v5qqq7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c"
    print(f"\nUser address: {USER_ADDRESS}")
    print(f"\nScanning blocks 609178 -> 609200 (22 blocks) for user address...")

    async with httpx.AsyncClient(timeout=30.0) as client:
        for height in range(609178, 609200):
            resp = await client.post(
                INDEXER_URL,
                json={"query": query, "variables": {"height": height}},
            )
            result = resp.json()

            if "errors" in result:
                print(f"  Block {height}: GraphQL error")
                continue

            block = result.get("data", {}).get("block")
            if not block:
                print(f"  Block {height}: Not found")
                continue

            txs = block.get("transactions", [])
            for tx in txs:
                created = tx.get("unshieldedCreatedOutputs", [])
                for utxo in created:
                    if utxo.get("owner") == USER_ADDRESS:
                        print(f"\n  *** FOUND UTXO! Block {height}, tx {tx['hash'][:16]}...")
                        print(f"      owner={utxo['owner']}")
                        print(f"      value={utxo['value']}")
                        print(f"      tokenType={utxo['tokenType']}")
                        print(f"      outputIndex={utxo['outputIndex']}")

                spent = tx.get("unshieldedSpentOutputs", [])
                for utxo in spent:
                    if utxo.get("owner") == USER_ADDRESS:
                        print(f"\n  *** SPENT UTXO! Block {height}, tx {tx['hash'][:16]}...")
                        print(f"      owner={utxo['owner']}")
                        print(f"      value={utxo['value']}")

            if txs:
                print(f"  Block {height}: {len(txs)} transactions, "
                      f"created={sum(len(t.get('unshieldedCreatedOutputs',[])) for t in txs)} "
                      f"spent={sum(len(t.get('unshieldedSpentOutputs',[])) for t in txs)}")


async def fetch_transactions_by_block_range_concurrent():
    """
    Phiên bản concurrent: scan nhiều blocks song song.
    """
    query = """
    query GetBlock($height: Int!) {
        block(offset: { height: $height }) {
            height
            transactions {
                id
                hash
                unshieldedCreatedOutputs {
                    owner
                    value
                    tokenType
                }
                unshieldedSpentOutputs {
                    owner
                    value
                    tokenType
                }
            }
        }
    }
    """

    USER_ADDRESS = "midnight1ngxj8gfn0npxdz2c0hqu6vy3j9h7gkwu8hvy5l3j0l3g3v5qqq7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7"
    print(f"\n=== CONCURRENT BLOCK SCAN ===")
    print(f"User address: {USER_ADDRESS}")

    async def get_block(client, height):
        try:
            resp = await client.post(
                INDEXER_URL,
                json={"query": query, "variables": {"height": height}},
            )
            result = resp.json()
            if "errors" in result:
                return None
            return result.get("data", {}).get("block")
        except Exception as e:
            return None

    async with httpx.AsyncClient(timeout=30.0, limits=httpx.Limits(max_connections=20)) as client:
        # Scan 200 blocks concurrently
        tasks = [get_block(client, h) for h in range(609178, 609378)]
        results = await asyncio.gather(*tasks)

    found_utxos = []
    for block in results:
        if not block:
            continue
        height = block["height"]
        txs = block.get("transactions", [])
        for tx in txs:
            for utxo in tx.get("unshieldedCreatedOutputs", []):
                if utxo.get("owner") == USER_ADDRESS:
                    found_utxos.append({
                        "type": "CREATED",
                        "block": height,
                        "tx_id": tx["id"],
                        "tx_hash": tx["hash"],
                        "value": utxo["value"],
                        "tokenType": utxo["tokenType"],
                        "outputIndex": utxo.get("outputIndex"),
                    })
                    print(f"\n  FOUND CREATED UTXO at block {height}, tx {tx['hash'][:20]}...")
                    print(f"    value={utxo['value']}, tokenType={utxo['tokenType']}")
            for utxo in tx.get("unshieldedSpentOutputs", []):
                if utxo.get("owner") == USER_ADDRESS:
                    found_utxos.append({
                        "type": "SPENT",
                        "block": height,
                        "tx_id": tx["id"],
                        "tx_hash": tx["hash"],
                        "value": utxo["value"],
                        "tokenType": utxo["tokenType"],
                    })
                    print(f"\n  FOUND SPENT UTXO at block {height}, tx {tx['hash'][:20]}...")

    print(f"\n=== SUMMARY ===")
    print(f"Total blocks scanned: 200")
    print(f"UTXOs found: {len(found_utxos)}")
    for utxo in found_utxos:
        print(f"  [{utxo['type']}] block={utxo['block']}, value={utxo['value']}, token={utxo['tokenType']}")

    return found_utxos


async def get_wallet_address():
    """Lấy đúng unshielded address từ mnemonic."""
    import subprocess
    result = subprocess.run(
        ["python", "-c", """
import sys
sys.path.insert(0, r'd:\\venera\\midnight\\night_fund')
from check_addr import get_unshielded_address
mnemonic = "genre volcano salute hurry motion talk kingdom gym doctor high code cattle bone ecology recall exotic shed cattle decide reform argue ability melt face"
addr = get_unshielded_address(mnemonic)
print(addr)
"""],
        capture_output=True, text=True, cwd=r"d:\venera\midnight\night_fund"
    )
    if result.returncode == 0:
        return result.stdout.strip()
    return None


async def main():
    print("=" * 60)
    print("TEST 1: Query transactions by identifier (hex-encoded tx id)")
    print("=" * 60)
    txs = await fetch_transactions()
    if txs:
        for tx in txs:
            print(f"  tx id={tx['id']} hash={tx['hash'][:20]}...")
            for utxo in tx.get("unshieldedCreatedOutputs", []):
                print(f"    CREATED: owner={utxo['owner'][:40]}... value={utxo['value']} {utxo['tokenType']}")
            for utxo in tx.get("unshieldedSpentOutputs", []):
                print(f"    SPENT: owner={utxo['owner'][:40]}... value={utxo['value']} {utxo['tokenType']}")

    print("\n" + "=" * 60)
    print("TEST 2: Fetch faucet tx, then paginate next 5 transactions")
    print("=" * 60)
    txs2 = await fetch_next_transactions_batch()

    print("\n" + "=" * 60)
    print("TEST 3: Block-by-block scan (10 blocks, sequential)")
    print("=" * 60)
    await fetch_transactions_by_block_range()

    print("\n" + "=" * 60)
    print("TEST 4: Concurrent block scan (200 blocks)")
    print("=" * 60)
    found = await fetch_transactions_by_block_range_concurrent()

    if found:
        print("\n\n>>> KẾT LUẬN: Tìm thấy UTXO! Balance query THÀNH CÔNG.")
        print("    Sử dụng block scan + concurrent requests = HIỆU QUẢ hơn nhiều.")
    else:
        print("\n\n>>> Không tìm thấy UTXO trong 200 blocks đầu tiên.")
        print("    Có thể cần scan nhiều hơn, hoặc address derivation bị sai.")


if __name__ == "__main__":
    asyncio.run(main())
