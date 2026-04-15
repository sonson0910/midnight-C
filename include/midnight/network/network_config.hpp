#ifndef MIDNIGHT_NETWORK_CONFIG_HPP
#define MIDNIGHT_NETWORK_CONFIG_HPP

#include <string>

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
            TESTNET,  // Preprod testnet
            STAGENET, // Staging network (future)
            MAINNET   // Production mainnet (future)
        };

        /**
         * @brief Get RPC endpoint for network
         */
        static std::string get_rpc_endpoint(Network network)
        {
            switch (network)
            {
            case Network::DEVNET:
                return "http://localhost:5678/api";

            case Network::TESTNET:
                return "https://preprod.midnight.network/api";

            case Network::STAGENET:
                return "https://staging.midnight.network/api";

            case Network::MAINNET:
                return "https://mainnet.midnight.network/api";

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
                return "http://localhost:9000/graphql";

            case Network::TESTNET:
                return "https://preprod-explorer.midnight.network/graphql";

            case Network::STAGENET:
                return "https://staging-explorer.midnight.network/graphql";

            case Network::MAINNET:
                return "https://explorer.midnight.network/graphql";

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
            if (network_name == "devnet")
                return Network::DEVNET;
            if (network_name == "testnet" || network_name == "preprod")
                return Network::TESTNET;
            if (network_name == "stagenet")
                return Network::STAGENET;
            if (network_name == "mainnet")
                return Network::MAINNET;
            return Network::TESTNET; // Default to testnet
        }
    };

} // namespace midnight::network

#endif // MIDNIGHT_NETWORK_CONFIG_HPP
