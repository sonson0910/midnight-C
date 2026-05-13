#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <vector>
#include <cctype>
#include <cstring>

namespace midnight::blockchain
{

    namespace
    {
        // Import shared Bech32m utilities from common_utils.hpp
        using midnight::util::bech32m::kBech32mConst;
        using midnight::util::bech32m::kBech32Charset;
        using midnight::util::bech32m::polymod;
        using midnight::util::bech32m::hrp_expand;
        using midnight::util::bech32m::convert_bits;

        // Alias to match the old call-sites
        auto bech32_polymod = polymod;
        auto bech32_hrp_expand = hrp_expand;

        std::string bech32m_encode(const std::string &hrp, const std::array<uint8_t, 32> &payload)
        {
            return midnight::util::bech32m::encode(hrp, payload);
        }

        // Import shared hex_nibble from common_utils.hpp
        using midnight::util::hex_nibble;

        bool hex_to_fixed_bytes(const std::string &hex_input, uint8_t *out, size_t out_size)
        {
            std::string hex = hex_input;
            if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0)
            {
                hex = hex.substr(2);
            }

            if (hex.size() != (out_size * 2))
            {
                return false;
            }

            for (size_t i = 0; i < out_size; ++i)
            {
                const int hi = hex_nibble(hex[i * 2]);
                const int lo = hex_nibble(hex[i * 2 + 1]);
                if (hi < 0 || lo < 0)
                {
                    return false;
                }

                out[i] = static_cast<uint8_t>((hi << 4) | lo);
            }

            return true;
        }

        bool hex_to_bytes32(const std::string &hex_input, std::array<uint8_t, 32> &out)
        {
            return hex_to_fixed_bytes(hex_input, out.data(), out.size());
        }

        bool has_supported_hrp(const std::string &hrp)
        {
            return hrp.rfind("mn_addr_", 0) == 0 ||
                   hrp.rfind("mn_shield-addr_", 0) == 0 ||
                   hrp.rfind("mn_dust_", 0) == 0;
        }

        bool bech32m_validate_address(const std::string &address)
        {
            if (address.empty())
            {
                return false;
            }

            bool has_lower = false;
            bool has_upper = false;
            std::string lower;
            lower.reserve(address.size());

            for (unsigned char c : address)
            {
                if (c < 33 || c > 126)
                {
                    return false;
                }

                if (std::isalpha(c))
                {
                    if (std::islower(c))
                    {
                        has_lower = true;
                    }
                    else
                    {
                        has_upper = true;
                    }
                }

                lower.push_back(static_cast<char>(std::tolower(c)));
            }

            if (has_lower && has_upper)
            {
                return false;
            }

            const auto sep = lower.rfind('1');
            if (sep == std::string::npos || sep == 0 || sep + 7 > lower.size())
            {
                return false;
            }

            const std::string hrp = lower.substr(0, sep);
            if (!has_supported_hrp(hrp))
            {
                return false;
            }

            std::vector<uint8_t> data;
            data.reserve(lower.size() - sep - 1);
            for (size_t i = sep + 1; i < lower.size(); ++i)
            {
                const char ch = lower[i];
                const char *pos = std::strchr(kBech32Charset, ch);
                if (!pos)
                {
                    return false;
                }
                data.push_back(static_cast<uint8_t>(pos - kBech32Charset));
            }

            auto values = bech32_hrp_expand(hrp);
            values.insert(values.end(), data.begin(), data.end());
            return bech32_polymod(values) == kBech32mConst;
        }

    } // namespace

    MidnightBlockchain::MidnightBlockchain() : connected_(false) {}

    MidnightBlockchain::~MidnightBlockchain()
    {
        if (connected_)
        {
            disconnect();
        }
    }

    void MidnightBlockchain::initialize(const std::string &network, const ProtocolParams &protocol_params)
    {
        network_ = network;
        protocol_params_ = protocol_params;

        std::ostringstream msg;
        msg << "Midnight blockchain manager initialized for network: " << network
            << " with min_fee_a: " << protocol_params.min_fee_a;
        midnight::g_logger->info(msg.str());
    }

    bool MidnightBlockchain::connect(const std::string &node_url)
    {
        node_url_ = node_url;

        std::ostringstream msg;
        msg << "Connecting to Midnight node at: " << node_url;
        midnight::g_logger->info(msg.str());

        try
        {
            // Create RPC client for node communication
            rpc_ = std::make_unique<midnight::network::MidnightNodeRPC>(node_url, 5000);

            // Verify node is ready
            if (!rpc_->is_ready())
            {
                midnight::g_logger->warn("Node is not ready yet");
                connected_ = false;
                return false;
            }

            // Fetch protocol parameters from node if not initialized
            if (protocol_params_.min_fee_a == 0)
            {
                protocol_params_ = rpc_->get_protocol_params();
                msg.str("");
                msg << "Fetched protocol params: min_fee_a=" << protocol_params_.min_fee_a;
                midnight::g_logger->info(msg.str());
            }

            connected_ = true;
            midnight::g_logger->info("Successfully connected to Midnight node");
            return true;
        }
        catch (const std::exception &e)
        {
            msg.str("");
            msg << "Failed to connect to node: " << e.what();
            midnight::g_logger->error(msg.str());
            connected_ = false;
            return false;
        }
    }

    std::string MidnightBlockchain::create_address(const std::string &public_key, uint8_t network_id)
    {
        if (public_key.empty())
        {
            return "";
        }

        std::array<uint8_t, 32> payload{};
        if (!hex_to_bytes32(public_key, payload))
        {
            midnight::g_logger->warn("Invalid public key hex when creating Midnight address");
            return "";
        }

        const std::string hrp = (network_id == 0) ? "mn_addr_preprod" : "mn_addr_mainnet";
        return bech32m_encode(hrp, payload);
    }

    bool MidnightBlockchain::validate_address(const std::string &address)
    {
        return bech32m_validate_address(address);
    }

    Result MidnightBlockchain::submit_transaction(const std::string &signed_tx)
    {
        Result result{false, "", ""};

        if (!connected_ || !rpc_)
        {
            result.error_message = "Not connected to Midnight network";
            return result;
        }

        if (signed_tx.empty())
        {
            result.error_message = "Transaction is empty";
            return result;
        }

        midnight::g_logger->info("Submitting transaction to Midnight network");
        try
        {
            // Submit via RPC node
            std::string tx_id = rpc_->submit_transaction(signed_tx);

            result.success = true;
            result.result = tx_id;

            std::ostringstream msg;
            msg << "Transaction submitted: " << tx_id.substr(0, 16) << "...";
            midnight::g_logger->info(msg.str());
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.error_message = std::string("RPC submission failed: ") + e.what();

            std::ostringstream msg;
            msg << "Failed to submit transaction: " << e.what();
            midnight::g_logger->error(msg.str());
        }

        return result;
    }

    std::vector<UTXO> MidnightBlockchain::query_utxos(const std::string &address)
    {
        std::vector<UTXO> utxos;

        if (!connected_ || !rpc_)
        {
            midnight::g_logger->warn("Not connected to query UTXOs");
            return utxos;
        }

        std::ostringstream msg;
        msg << "Querying UTXOs for address: " << address.substr(0, 20) << "...";
        midnight::g_logger->debug(msg.str());

        try
        {
            // Query from RPC node
            utxos = rpc_->get_utxos(address);

            msg.str("");
            msg << "Found " << utxos.size() << " UTXOs";
            midnight::g_logger->debug(msg.str());
        }
        catch (const std::exception &e)
        {
            msg.str("");
            msg << "Failed to query UTXOs: " << e.what();
            midnight::g_logger->error(msg.str());
        }

        return utxos;
    }

    ProtocolParams MidnightBlockchain::get_protocol_params() const
    {
        return protocol_params_;
    }

    Result MidnightBlockchain::get_chain_tip() const
    {
        Result result{false, "", ""};

        if (!connected_ || !rpc_)
        {
            result.error_message = "Not connected to Midnight network";
            return result;
        }

        try
        {
            auto tip = rpc_->get_chain_tip();
            result.success = true;
            result.result = "{\"height\":" + std::to_string(tip.first) +
                            ",\"hash\":\"" + tip.second + "\"}";
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.error_message = std::string("Failed to query chain tip: ") + e.what();
        }

        return result;
    }

    void MidnightBlockchain::disconnect()
    {
        connected_ = false;
        rpc_ = nullptr; // Clean up RPC client
        midnight::g_logger->info("Disconnected from Midnight network");
    }

    uint64_t MidnightBlockchain::calculate_min_fee(size_t tx_size)
    {
        // Fee formula: fee = minFeeA * size + minFeeB
        return protocol_params_.min_fee_a * tx_size + protocol_params_.min_fee_b;
    }

} // namespace midnight::blockchain
