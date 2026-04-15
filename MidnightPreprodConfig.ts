/**
 * Midnight Preprod Network Configuration
 * For testing real transactions on Midnight Preprod
 */

import path from 'path';

export interface Config {
    logDir: string;
    indexer: string;
    indexerWS: string;
    node: string;
    proofServer: string;
    networkId: string;
}

export class PreprodConfig implements Config {
    // Logs directory with timestamp
    logDir = path.resolve(__dirname, '..', 'logs', `${new Date().toISOString()}.log`);

    // Midnight Preprod Indexer (GraphQL)
    indexer = 'https://indexer.preprod.midnight.network/api/v3/graphql';

    // Midnight Preprod Indexer WebSocket
    indexerWS = 'wss://indexer.preprod.midnight.network/api/v3/graphql/ws';

    // Midnight Preprod RPC Node (WebSocket)
    node = 'wss://rpc.preprod.midnight.network';

    // Proof Server (local or remote)
    proofServer = 'http://127.0.0.1:6300';

    // Network identifier
    networkId = 'preprod';

    // Additional Preprod Configuration
    chainId = 'midnight-preprod-1';

    // Faucet for Preprod (if available)
    faucet = 'https://faucet.preprod.midnight.network';

    // Block explorer
    explorer = 'https://explorer.preprod.midnight.network';

    // Gas configuration for Preprod
    gasMultiplier = 1.2;
    maxGasLimit = 1000000;
    minGasLimit = 21000;
}

/**
 * Configuration Comparison
 */
export const ConfigComparison = {
    preview: {
        indexer: 'https://indexer.preview.midnight.network/api/v3/graphql',
        node: 'wss://rpc.preview.midnight.network',
        proofServer: 'http://127.0.0.1:6300',
    },
    preprod: {
        indexer: 'https://indexer.preprod.midnight.network/api/v3/graphql',
        node: 'wss://rpc.preprod.midnight.network',
        proofServer: 'http://127.0.0.1:6300',
    },
};

/**
 * Usage Example:
 *
 * import { PreprodConfig } from './MidnightPreprodConfig';
 *
 * const config = new PreprodConfig();
 *
 * // Connect to Preprod
 * const rpc = new MidnightRpcClient(config.node);
 * const indexer = new GraphQLClient(config.indexer);
 *
 * // Test transaction on Preprod
 * const tx = await rpc.submitTransaction({
 *     from: walletAddress,
 *     to: recipientAddress,
 *     value: amount,
 * });
 *
 * console.log('Transaction submitted:', tx.hash);
 */
