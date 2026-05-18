# Hello World Compact Contract

This is the SDK's minimal real Compact contract example. It stores a
32-byte greeting hash instead of a string because Compact circuits should use
fixed-size field/byte values at the ledger boundary.

The C++ SDK does not shell out to the toolkit for production submission.
The intended production path is:

1. Compile the contract with the ledger-compatible `compactc`.
2. Generate deploy/circuit intents with `midnight-node-toolkit-js`.
3. Submit those intent files through the C++ SDK command:

```bash
MIDNIGHT_NETWORK=preview \
MIDNIGHT_LIVE_CUSTOM_COMPILED_CONTRACT_DIRS=/absolute/path/to/managed/hello \
MIDNIGHT_LIVE_CUSTOM_INTENT_FILES=/absolute/path/to/deploy.intent \
/Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh custom-contract-intents
```

For live smoke testing without a local Compact toolchain, use:

```bash
MIDNIGHT_NETWORK=preview \
/Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh hello-contract-flow
```

That deploys and calls Midnight's static `simple-merkle-tree` contract through
the same C++ ledger FFI/proof/submit/confirm pipeline.

