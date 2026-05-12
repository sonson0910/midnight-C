#!/usr/bin/env pwsh
# Fast balance checker using faucet TX hash and RPC
# No full chain scan needed

param(
    [Parameter(Mandatory=$false)]
    [string]$Address = "mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q",
    
    [Parameter(Mandatory=$false)]
    [string]$FaucetTxHash = "e8b6bab117b98cd7d7dbe6ab86584d81ec3d9960218f001cd3696f957dea977e"
)

$graphql = "https://indexer.preprod.midnight.network/api/v4/graphql"
$rpc = "https://rpc.preprod.midnight.network"

Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "  Midnight Fast Balance Checker (PreProd)" -ForegroundColor Cyan
Write-Host "=============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Address: $Address" -ForegroundColor Yellow
Write-Host ""

# Step 1: Get current block height
Write-Host "[1] Getting current block height..." -ForegroundColor Gray
$r = Invoke-RestMethod -Uri $graphql -Method Post -ContentType "application/json" -Body '{"query":"{ block { height } }"}' -TimeoutSec 15
$currentHeight = $r.data.block.height
Write-Host "  Current height: $currentHeight" -ForegroundColor Green

# Step 2: Try unshieldedTransactions if available
Write-Host ""
Write-Host "[2] Trying direct UTXO query..." -ForegroundColor Gray
$query = "query { unshieldedTransactions(address: `"$Address`", limit: 10) { totalCount items { transaction { id hash } createdUtxos { value tokenType outputIndex } } } }"
try {
    $r = Invoke-RestMethod -Uri $graphql -Method Post -ContentType "application/json" -Body ($query | ConvertTo-Json -Compress) -TimeoutSec 30 -ErrorAction SilentlyContinue
    if ($r.data.unshieldedTransactions) {
        Write-Host "  Direct query worked!" -ForegroundColor Green
        $count = $r.data.unshieldedTransactions.totalCount
        Write-Host "  Total UTXOs: $count"
    }
} catch {
    Write-Host "  Direct UTXO query not available on preprod" -ForegroundColor Red
}

# Step 3: If faucet TX hash provided, decode it
if ($FaucetTxHash) {
    Write-Host ""
    Write-Host "[3] Decoding faucet transaction..." -ForegroundColor Gray
    Write-Host "  TX: $FaucetTxHash" -ForegroundColor Cyan
    
    # Query transaction
    $txQuery = "query { transactions(offset: { hash: `"$FaucetTxHash`" }) { id hash } }"
    $r = Invoke-RestMethod -Uri $graphql -Method Post -ContentType "application/json" -Body ($txQuery | ConvertTo-Json -Compress) -TimeoutSec 30
    if ($r.data.transactions.Count -gt 0) {
        $txId = $r.data.transactions[0].id
        $txHash = $r.data.transactions[0].hash
        Write-Host "  TX ID: $txId" -ForegroundColor Green
        Write-Host "  TX Hash: $txHash" -ForegroundColor Green
    }
    
    # Get the block containing this TX
    Write-Host ""
    Write-Host "[4] Finding block for TX..." -ForegroundColor Gray
    
    # We know the faucet TX is in block 748878
    # Let's get the block hash and check
    $blockQuery = "query { block(offset: { height: 748878 }) { height hash transactions { id hash } } }"
    $r = Invoke-RestMethod -Uri $graphql -Method Post -ContentType "application/json" -Body ($blockQuery | ConvertTo-Json -Compress) -TimeoutSec 30
    if ($r.data.block.transactions) {
        $found = $false
        foreach ($tx in $r.data.block.transactions) {
            if ($tx.hash -eq $FaucetTxHash) {
                Write-Host "  Found in block $($r.data.block.height)" -ForegroundColor Green
                Write-Host "  Block hash: $($r.data.block.hash)" -ForegroundColor Green
                $found = $true
                break
            }
        }
        if (-not $found) {
            Write-Host "  TX not found in expected block" -ForegroundColor Red
        }
    }
}

# Step 4: Use RPC to get raw block and decode
Write-Host ""
Write-Host "[5] Getting raw block via RPC..." -ForegroundColor Gray
$blockHash = "0x939906f65687c6250fce1eb21a59737f23b382e4a8335004d2b9dd9de33ecc2c"
$body = '{"jsonrpc":"2.0","method":"chain_getBlock","params":["' + $blockHash + '"],"id":1}'
$r = Invoke-RestMethod -Uri $rpc -Method Post -ContentType "application/json" -Body $body -TimeoutSec 30

if ($r.result) {
    $extrinsics = $r.result.block.extrinsics
    Write-Host "  Block extrinsics count: $($extrinsics.Count)" -ForegroundColor Cyan
    
    # Extrinsic index 1 should be the faucet transfer (index 0 is usually epoch boundary)
    # Let's decode the faucet extrinsic
    Write-Host ""
    Write-Host "[6] Analyzing faucet extrinsic (index 1)..." -ForegroundColor Gray
    
    if ($extrinsics.Count -gt 1) {
        $faucetExt = $extrinsics[1]
        Write-Host "  Raw: $($faucetExt.Substring(0, [Math]::Min(80, $faucetExt.Length)))..." -ForegroundColor DarkGray
        
        # SCALE decode attempt
        # Format: call_index(2) + compact_len + dest_raw_addr + value
        # Call index for transfer is typically 0x05 (standard transfer)
        # Check if this is a transfer call
        $callIndex = $faucetExt.Substring(0, 4)
        Write-Host "  Call index: $callIndex" -ForegroundColor Cyan
        
        # The second extrinsic looks like a transfer
        # 0xd0050d0000a6f27ebdd7f1fd2b03134619e805e77533898699a9f5664c8bb1982c0d4950f36db24700e80d6f199e01000002000000
        # This decodes to:
        # d0 = compact encoding for 208 (length)
        # 05 = call index (transfer/note)
        # 0d0000 = compact 6 (dest raw length)
        # a6f27ebdd7f1fd2b03134619e805e77533898699a9f5664c8bb = dest (20 bytes)
        # 1982c0d4950f36db24700e80d6f199e01000002000000 = value + proof
    }
}

Write-Host ""
Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "  NOTE: Full UTXO decoding requires C++ SCALE codec" -ForegroundColor Yellow
Write-Host "  Run midnight-wallet-tool for complete balance" -ForegroundColor Yellow
Write-Host "=============================================" -ForegroundColor Cyan
