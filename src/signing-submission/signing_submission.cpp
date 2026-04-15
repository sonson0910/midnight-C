/**
 * Phase 5: Signing & Submission Implementation
 */

#include "midnight/signing-submission/signing_submission.hpp"
#include "midnight/network/network_client.hpp"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <stdexcept>

namespace midnight::phase5
{
    namespace
    {
        constexpr uint32_t kSubmitRpcTimeoutMs = 10000;

        std::string to_hex(const std::vector<uint8_t> &data)
        {
            static const char *hex = "0123456789abcdef";
            std::string out = "0x";
            out.reserve(2 + data.size() * 2);
            for (uint8_t byte : data)
            {
                out.push_back(hex[(byte >> 4) & 0x0F]);
                out.push_back(hex[byte & 0x0F]);
            }
            return out;
        }
    } // namespace

    // ============================================================================
    // KeyManager Implementation
    // ============================================================================

    std::optional<Keypair> KeyManager::generate_sr25519_key()
    {
        try
        {
            // In production: Use libsodium or similar for sr25519
            Keypair keypair;
            keypair.type = KeyType::SR25519;
            keypair.public_key = "0x" + std::string(64, 'p');
            keypair.private_key = "0x" + std::string(64, 's');
            return keypair;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error generating sr25519 key: " << e.what() << std::endl;
            return {};
        }
    }

    std::optional<Keypair> KeyManager::generate_ed25519_key()
    {
        try
        {
            Keypair keypair;
            keypair.type = KeyType::ED25519;
            keypair.public_key = "0x" + std::string(64, 'e');
            keypair.private_key = "0x" + std::string(64, 'd');
            return keypair;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error generating ed25519 key: " << e.what() << std::endl;
            return {};
        }
    }

    std::optional<Keypair> KeyManager::generate_ecdsa_key()
    {
        try
        {
            Keypair keypair;
            keypair.type = KeyType::ECDSA;
            keypair.public_key = "0x" + std::string(64, 'c');
            keypair.private_key = "0x" + std::string(64, 'k');
            return keypair;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error generating ECDSA key: " << e.what() << std::endl;
            return {};
        }
    }

    std::optional<Keypair> KeyManager::load_key(const std::string &file_path,
                                                const std::string &password)
    {
        try
        {
            // In production: Read encrypted key file, decrypt with password
            Keypair keypair;
            keypair.type = KeyType::SR25519;
            keypair.public_key = "0x" + std::string(64, 'p');
            keypair.private_key = "0x" + std::string(64, 's');
            return keypair;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error loading key: " << e.what() << std::endl;
            return {};
        }
    }

    bool KeyManager::save_key(const Keypair &keypair,
                              const std::string &file_path,
                              const std::string &password)
    {
        try
        {
            // In production: Encrypt key with password, save to file
            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error saving key: " << e.what() << std::endl;
            return false;
        }
    }

    std::string KeyManager::derive_address(const Keypair &keypair)
    {
        switch (keypair.type)
        {
        case KeyType::SR25519:
            return derive_address_sr25519(keypair.public_key);
        case KeyType::ED25519:
            return derive_address_ed25519(keypair.public_key);
        case KeyType::ECDSA:
            return "0x" + keypair.public_key.substr(2);
        }
        return "";
    }

    std::optional<Keypair> KeyManager::import_from_seed(const std::string &seed_phrase,
                                                        KeyType type)
    {
        try
        {
            // In production: Derive keypair from BIP39 seed
            Keypair keypair;
            keypair.type = type;
            keypair.seed = seed_phrase;

            if (type == KeyType::SR25519)
            {
                return generate_sr25519_key();
            }
            else
            {
                return generate_ed25519_key();
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error importing from seed: " << e.what() << std::endl;
            return {};
        }
    }

    std::string KeyManager::derive_address_sr25519(const std::string &public_key)
    {
        return "5" + public_key.substr(2, 40); // Midnight sr25519 address format
    }

    std::string KeyManager::derive_address_ed25519(const std::string &public_key)
    {
        return "e" + public_key.substr(2, 40); // ed25519 validator address
    }

    // ============================================================================
    // TransactionSigner Implementation
    // ============================================================================

    TransactionSigner::TransactionSigner(const Keypair &signer_keypair)
        : keypair_(signer_keypair)
    {
        signer_address_ = KeyManager::derive_address(keypair_);
    }

    std::string TransactionSigner::sign_transaction(const std::vector<uint8_t> &tx_data)
    {
        return sr25519_sign(tx_data);
    }

    bool TransactionSigner::verify_signature(const std::vector<uint8_t> &tx_data,
                                             const std::string &signature) const
    {
        return sr25519_verify(tx_data, signature);
    }

    std::string TransactionSigner::get_signer_address() const
    {
        return signer_address_;
    }

    bool TransactionSigner::is_signer_of(const std::string &signature) const
    {
        // In production: Extract signer from signature and compare
        return !signature.empty();
    }

    std::string TransactionSigner::sr25519_sign(const std::vector<uint8_t> &message)
    {
        // In production: Use sr25519 library
        // For now: Mock signature
        return "0x" + std::string(128, 's');
    }

    bool TransactionSigner::sr25519_verify(const std::vector<uint8_t> &message,
                                           const std::string &signature) const
    {
        // In production: Verify sr25519 signature
        if (signature.size() != 130)
        { // "0x" + 128 hex chars = 64 bytes
            return false;
        }
        return true;
    }

    // ============================================================================
    // FinallityVoteSigner Implementation
    // ============================================================================

    FinallityVoteSigner::FinallityVoteSigner(const Keypair &voter_keypair)
        : keypair_(voter_keypair)
    {
        voter_address_ = KeyManager::derive_address(keypair_);
    }

    std::string FinallityVoteSigner::sign_vote(uint32_t block_height,
                                               const std::string &block_hash)
    {
        // Construct vote message: height || hash
        std::vector<uint8_t> vote_message;
        vote_message.resize(40); // 4 bytes height + 36 bytes hash

        // Add height (big-endian)
        vote_message[0] = (block_height >> 24) & 0xFF;
        vote_message[1] = (block_height >> 16) & 0xFF;
        vote_message[2] = (block_height >> 8) & 0xFF;
        vote_message[3] = block_height & 0xFF;

        // Add hash
        std::copy(block_hash.begin(), block_hash.end(), vote_message.begin() + 4);

        return ed25519_sign(vote_message);
    }

    FinalityVote FinallityVoteSigner::create_vote(uint32_t block_height,
                                                  const std::string &block_hash)
    {
        FinalityVote vote;
        vote.target_block_height = block_height;
        vote.target_block_hash = block_hash;
        vote.voter_address = voter_address_;
        vote.signature = sign_vote(block_height, block_hash);
        vote.timestamp = std::time(nullptr);

        return vote;
    }

    bool FinallityVoteSigner::verify_vote(const FinalityVote &vote) const
    {
        // In production: Verify ed25519 signature
        if (vote.signature.size() != 130)
        { // "0x" + 128 hex chars = 64 bytes
            return false;
        }
        return true;
    }

    std::string FinallityVoteSigner::ed25519_sign(const std::vector<uint8_t> &message)
    {
        // In production: Use ed25519 library
        // For now: Mock signature
        return "0x" + std::string(128, 'f');
    }

    // ============================================================================
    // TransactionSubmitter Implementation
    // ============================================================================

    TransactionSubmitter::TransactionSubmitter(const std::string &node_rpc_url)
        : TransactionSubmitter(node_rpc_url, SubmissionTransportMode::MOCK)
    {
    }

    TransactionSubmitter::TransactionSubmitter(const std::string &node_rpc_url,
                                               SubmissionTransportMode mode)
        : rpc_url_(node_rpc_url)
    {
        transport_mode_ = mode;
    }

    void TransactionSubmitter::set_transport_mode(SubmissionTransportMode mode)
    {
        transport_mode_ = mode;
    }

    SubmissionTransportMode TransactionSubmitter::get_transport_mode() const
    {
        return transport_mode_;
    }

    SubmissionResult TransactionSubmitter::submit(const SignedTransaction &signed_tx)
    {
        try
        {
            auto response = rpc_submit_extrinsic(signed_tx);
            if (response.isMember("error"))
            {
                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                throw std::runtime_error("RPC error: " + Json::writeString(writer, response["error"]));
            }
            if (!response.isMember("result"))
            {
                throw std::runtime_error("RPC response missing result");
            }

            SubmissionResult result;
            result.transaction_hash = signed_tx.transaction_hash;
            result.status = SubmissionStatus::SUBMITTED;
            result.submission_timestamp = std::time(nullptr);

            // Cache result
            submission_cache_[signed_tx.transaction_hash] = result;

            return result;
        }
        catch (const std::exception &e)
        {
            SubmissionResult result;
            result.status = SubmissionStatus::FAILED;
            result.error_message = std::string("Submission failed: ") + e.what();
            return result;
        }
    }

    std::vector<SubmissionResult> TransactionSubmitter::submit_batch(
        const std::vector<SignedTransaction> &signed_txs)
    {

        std::vector<SubmissionResult> results;
        for (const auto &tx : signed_txs)
        {
            results.push_back(submit(tx));
        }
        return results;
    }

    SubmissionResult TransactionSubmitter::get_submission_status(const std::string &tx_hash)
    {
        auto it = submission_cache_.find(tx_hash);
        if (it != submission_cache_.end())
        {
            return it->second;
        }

        SubmissionResult result;
        result.transaction_hash = tx_hash;
        result.status = SubmissionStatus::CREATED; // Not found in cache
        return result;
    }

    void TransactionSubmitter::wait_for_inclusion(
        const std::string &tx_hash,
        std::function<void(const SubmissionResult &)> callback)
    {

        std::thread([this, tx_hash, callback]()
                    {
        for (int i = 0; i < 60; ++i) { // Wait up to 60 seconds
            auto status = get_submission_status(tx_hash);

            if (status.status == SubmissionStatus::INCLUDED_IN_BLOCK ||
                status.status == SubmissionStatus::FINALIZED) {
                callback(status);
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        SubmissionResult timeout_result;
        timeout_result.status = SubmissionStatus::FAILED;
        timeout_result.error_message = "Timeout waiting for inclusion";
        callback(timeout_result); })
            .detach();
    }

    Json::Value TransactionSubmitter::rpc_submit_extrinsic(const SignedTransaction &signed_tx)
    {
        if (transport_mode_ == SubmissionTransportMode::MOCK)
        {
            Json::Value response;
            response["result"] = signed_tx.transaction_hash;
            return response;
        }

        midnight::network::NetworkClient client(rpc_url_, kSubmitRpcTimeoutMs);
        nlohmann::json request = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "submitTransaction"},
            {"params", {
                           {"transaction_hash", signed_tx.transaction_hash},
                           {"signature", signed_tx.signature},
                           {"signer_address", signed_tx.signer_address},
                           {"nonce", signed_tx.nonce},
                           {"signed_data", to_hex(signed_tx.signed_data)}
                       }}
        };

        std::vector<std::string> rpc_paths = {"/", "/rpc", "/api"};
        std::string accumulated_errors;
        for (const auto &path : rpc_paths)
        {
            try
            {
                auto response = client.post_json(path, request);
                Json::Value parsed;
                Json::CharReaderBuilder builder;
                std::string errors;
                std::istringstream stream(response.dump());
                if (!Json::parseFromStream(builder, stream, &parsed, &errors))
                {
                    throw std::runtime_error("parse error: " + errors);
                }
                return parsed;
            }
            catch (const std::exception &e)
            {
                if (!accumulated_errors.empty())
                {
                    accumulated_errors += " | ";
                }
                accumulated_errors += path + ": " + e.what();
            }
        }

        throw std::runtime_error("Real RPC submission failed for all paths: " + accumulated_errors);
    }

    // ============================================================================
    // MempoolMonitor Implementation
    // ============================================================================

    MempoolMonitor::MempoolMonitor(const std::string &node_rpc_url)
        : rpc_url_(node_rpc_url)
    {
    }

    uint32_t MempoolMonitor::get_mempool_size()
    {
        try
        {
            auto mempool = rpc_get_mempool();

            if (mempool.isMember("extrinsics"))
            {
                return mempool["extrinsics"].size();
            }

            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error getting mempool size: " << e.what() << std::endl;
            return 0;
        }
    }

    std::optional<SignedTransaction> MempoolMonitor::get_mempool_transaction(
        const std::string &tx_hash)
    {

        try
        {
            auto mempool = rpc_get_mempool();

            if (mempool.isMember("extrinsics"))
            {
                for (const auto &tx : mempool["extrinsics"])
                {
                    if (tx.get("hash", "").asString() == tx_hash)
                    {
                        SignedTransaction signed_tx;
                        signed_tx.transaction_hash = tx_hash;
                        return signed_tx;
                    }
                }
            }

            return {};
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error getting mempool transaction: " << e.what() << std::endl;
            return {};
        }
    }

    double MempoolMonitor::get_transaction_priority(const std::string &tx_hash)
    {
        // In production: Calculate priority based on fee and size
        // Higher fee = higher priority

        auto tx = get_mempool_transaction(tx_hash);
        if (!tx)
        {
            return 0.0;
        }

        // Mock: return priority 0-1
        return 0.5;
    }

    void MempoolMonitor::monitor_mempool(
        std::function<void(const std::vector<std::string> &)> callback)
    {

        monitoring_ = true;

        std::thread([this, callback]()
                    {
        std::vector<std::string> last_txs;

        while (monitoring_) {
            auto mempool = rpc_get_mempool();
            std::vector<std::string> current_txs;

            if (mempool.isMember("extrinsics")) {
                for (const auto& tx : mempool["extrinsics"]) {
                    current_txs.push_back(tx.get("hash", "").asString());
                }
            }

            // Find new transactions
            std::vector<std::string> new_txs;
            for (const auto& tx : current_txs) {
                if (std::find(last_txs.begin(), last_txs.end(), tx) == last_txs.end()) {
                    new_txs.push_back(tx);
                }
            }

            if (!new_txs.empty()) {
                callback(new_txs);
            }

            last_txs = current_txs;
            std::this_thread::sleep_for(std::chrono::seconds(6)); // Check every block
        } })
            .detach();
    }

    uint32_t MempoolMonitor::estimate_inclusion_blocks(uint64_t fee_amount)
    {
        // Higher fee = faster inclusion
        // Mock: base is 6 blocks, reduced per fee level

        uint32_t base_blocks = 6;

        if (fee_amount > 100000)
        {
            return 1;
        }
        else if (fee_amount > 50000)
        {
            return 2;
        }
        else if (fee_amount > 10000)
        {
            return 3;
        }

        return base_blocks;
    }

    bool MempoolMonitor::wait_for_inclusion(const std::string &tx_hash, uint64_t timeout_ms)
    {
        auto start = std::chrono::high_resolution_clock::now();

        while (true)
        {
            auto tx = get_mempool_transaction(tx_hash);
            if (tx)
            {
                return true;
            }

            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);

            if (elapsed.count() > timeout_ms)
            {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    Json::Value MempoolMonitor::rpc_get_mempool()
    {
        // In production: Query mempool via RPC
        // For now: Return mock response

        Json::Value response;
        response["extrinsics"] = Json::Value(Json::arrayValue);
        return response;
    }

    // ============================================================================
    // BatchSigner Implementation
    // ============================================================================

    void BatchSigner::add_transaction(const std::vector<uint8_t> &tx_data)
    {
        transaction_batch_.push_back(tx_data);
    }

    std::vector<std::string> BatchSigner::sign_batch(const Keypair &keypair)
    {
        TransactionSigner signer(keypair);

        std::vector<std::string> signatures;
        for (const auto &tx : transaction_batch_)
        {
            signatures.push_back(signer.sign_transaction(tx));
        }

        return signatures;
    }

    size_t BatchSigner::batch_size() const
    {
        return transaction_batch_.size();
    }

    void BatchSigner::clear()
    {
        transaction_batch_.clear();
    }

    // ============================================================================
    // SignatureVerifier Implementation
    // ============================================================================

    bool SignatureVerifier::verify_sr25519(const std::vector<uint8_t> &message,
                                           const std::string &signature,
                                           const std::string &public_key)
    {
        // In production: Use sr25519 library
        if (signature.size() != 130 || public_key.empty())
        {
            return false;
        }
        return true;
    }

    bool SignatureVerifier::verify_ed25519(const std::vector<uint8_t> &message,
                                           const std::string &signature,
                                           const std::string &public_key)
    {
        // In production: Use ed25519 library
        if (signature.size() != 130 || public_key.empty())
        {
            return false;
        }
        return true;
    }

    std::optional<std::string> SignatureVerifier::recover_signer(
        const std::vector<uint8_t> &message,
        const std::string &signature)
    {

        // In production: Recover signer address from signature
        if (signature.empty())
        {
            return {};
        }

        // Mock: return derived address
        return "5" + signature.substr(2, 40);
    }

} // namespace midnight::phase5
