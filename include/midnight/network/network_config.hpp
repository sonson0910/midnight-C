#ifndef MIDNIGHT_NETWORK_CONFIG_HPP
#define MIDNIGHT_NETWORK_CONFIG_HPP

#include <string>
#include <algorithm>
#include <cctype>

namespace midnight::network
{

    /**
     * @brief Midnight Network Configuration
     *
     * Defines endpoints and parameters for different Midnight networks.
     * Add new networks or custom endpoints as needed.
     */
    class NetworkConfig
    {
    public:
        enum class Network
        {
            DEVNET,   // Local development
            PREVIEW,  // Preview testnet (recommended for dev)
            TESTNET,  // Preprod testnet
            STAGENET, // Staging network (future)
            MAINNET   // Production mainnet
        };

        /**
         * @brief Get RPC endpoint for network
         */
        static std::string get_rpc_endpoint(Network network)
        {
            switch (network)
            {
            case Network::DEVNET:
                return "http://localhost:9944";

            case Network::PREVIEW:
                return "https://rpc.preview.midnight.network";

            case Network::TESTNET:
                return "https://rpc.preprod.midnight.network";

            case Network::STAGENET:
                return "https://rpc.staging.midnight.network";

            case Network::MAINNET:
                return "https://rpc.mainnet.midnight.network";

            default:
                return "";
            }
        }

        /**
         * @brief Get GraphQL endpoint for network (optional)
         */
        static std::string get_graphql_endpoint(Network network)
        {
            switch (network)
            {
            case Network::DEVNET:
                return "http://localhost:8088/api/v4/graphql";

            case Network::PREVIEW:
                return "https://indexer.preview.midnight.network/api/v4/graphql";

            case Network::TESTNET:
                return "https://indexer.preprod.midnight.network/api/v4/graphql";

            case Network::STAGENET:
                return "https://indexer.staging.midnight.network/api/v4/graphql";

            case Network::MAINNET:
                return "https://indexer.mainnet.midnight.network/api/v4/graphql";

            default:
                return "";
            }
        }

        /**
         * @brief Get faucet URL for testnet
         */
        static std::string get_faucet_url(Network network)
        {
            switch (network)
            {
            case Network::DEVNET:
                return ""; // No faucet for local devnet

            case Network::PREVIEW:
                return "https://faucet.preview.midnight.network/";

            case Network::TESTNET:
                return "https://faucet.preprod.midnight.network/";

            case Network::STAGENET:
                return "https://faucet.staging.midnight.network/";

            case Network::MAINNET:
                return ""; // No faucet for mainnet

            default:
                return "";
            }
        }

        /**
         * @brief Get network name as string
         */
        static std::string network_name(Network network)
        {
            switch (network)
            {
            case Network::DEVNET:
                return "devnet";
            case Network::PREVIEW:
                return "preview";
            case Network::TESTNET:
                return "testnet";
            case Network::STAGENET:
                return "stagenet";
            case Network::MAINNET:
                return "mainnet";
            default:
                return "unknown";
            }
        }

        /**
         * @brief Determine network from string
         */
        static Network from_string(const std::string &network_name)
        {
            std::string normalized = network_name;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char c) -> char
                           { return static_cast<char>(std::tolower(c)); });

            if (normalized == "devnet" || normalized == "midnight-devnet" ||
                normalized == "undeployed")
                return Network::DEVNET;
            if (normalized == "preview" || normalized == "midnight-preview")
                return Network::PREVIEW;
            if (normalized == "testnet" || normalized == "preprod" ||
                normalized == "midnight-testnet" || normalized == "midnight-preprod")
                return Network::TESTNET;
            if (normalized == "stagenet" || normalized == "midnight-stagenet")
                return Network::STAGENET;
            if (normalized == "mainnet" || normalized == "midnight-mainnet")
                return Network::MAINNET;
            return Network::PREVIEW; // Default to preview
        }
    };

} // namespace midnight::network

#endif // MIDNIGHT_NETWORK_CONFIG_HPP
