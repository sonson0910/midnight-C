# Midnight SDK Golden Fixtures

These fixtures are shared test vectors for the C++ SDK and future SDKs in other
languages. They intentionally contain only public data: public addresses, public
transaction hashes, public block numbers, public contract addresses, and version
strings.

Do not add wallet seeds, mnemonics, private keys, proof keys, or `.secrets`
content here.

Recommended usage for every SDK:

1. Load `vectors/u128.json` and validate decimal u128 parsing.
2. Load `vectors/formats.json` and validate addresses, hashes, contract hex,
   token hex, and NIGHT token normalization.
3. Load `vectors/errors.json` and verify public error-code strings match the SDK
   error enum.
4. Load `vectors/artifact-layout.json` and verify artifact writers use the same
   directory and file conventions.
5. Treat `live/preview-smoke.json` as public live-smoke evidence, not as a
   default CI test. Live submit tests must remain gated by explicit environment
   variables.

