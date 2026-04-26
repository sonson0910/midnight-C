/**
 * Phase 5: Signing & Submission
 * Handles sr25519/ed25519 signing and transaction submission
 *
 * Key components:
 * - sr25519 for AURA block authorship
 * - ed25519 for GRANDPA finality votes
 * - Transaction signing and submission
 * - Mempool monitoring and status tracking
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <json/json.h>
#include <cstdint>
#include <functional>
#include <thread>
#include <atomic>

namespace midnight::signing_submission
{

    /**
     * Cryptographic Key Types
     */
    enum class KeyType
    {
        SR25519, // AURA block authorship
        ED25519, // GRANDPA finality
        ECDSA,   // Cardano bridge
    };

    /**
     * Keypair Structure
     */
    struct Keypair
    {
        KeyType type;
        std::string public_key;  // Hex-encoded public key
        std::string private_key; // Hex-encoded secret key (encrypted in prod)
        std::string seed;        // BIP39 seed phrase (optional)
    };

    /**
     * Signed Transaction
     */
    struct SignedTransaction
    {
        std::string transaction_hash;
        std::vector<uint8_t> signed_data; // Signed TX bytes
        std::string signature;            // Hex-encoded sr25519 signature
        std::string signer_address;       // Who signed this
        uint64_t signature_timestamp;     // When signed
        uint32_t nonce;                   // TX nonce
    };

    /**
     * Finality Vote (GRANDPA)
     */
    struct FinalityVote
    {
        uint32_t target_block_height;
        std::string target_block_hash;
        std::string voter_address; // ed25519 validator
        std::string signature;     // ed25519 signature
        uint64_t timestamp;
    };

    /**
     * Submission Status
     */
    enum class SubmissionStatus
    {
        CREATED,
        SIGNED,
        SUBMITTED,
        IN_MEMPOOL,
        INCLUDED_IN_BLOCK,
        FINALIZED,
        FAILED,
    };

    enum class SubmissionTransportMode
    {
        MOCK,
        REAL_RPC,
    };

    /**
     * Transaction Submission Result
     */
    struct SubmissionResult
    {
        SubmissionStatus status = SubmissionStatus::CREATED;
        std::string transaction_hash;
        uint32_t block_height = 0;
        std::string error_message;
        uint64_t submission_timestamp = 0;
        uint64_t inclusion_timestamp = 0; // When included in block
        uint64_t finality_timestamp = 0;  // When GRANDPA finalized
    };

    /**
     * Key Manager
     * Generates, stores, and manages cryptographic keys
     */
    class KeyManager
    {
    public:
        /**
         * Generate new sr25519 keypair (AURA authority)
         */
        static std::optional<Keypair> generate_sr25519_key();

        /**
         * Generate new ed25519 keypair (GRANDPA validator)
         */
        static std::optional<Keypair> generate_ed25519_key();

        /**
         * Generate new ECDSA keypair (Cardano bridge)
         */
        static std::optional<Keypair> generate_ecdsa_key();

        /**
         * Load keypair from file
         * @param file_path: Path to encrypted key file
         * @param password: Password to decrypt
         */
        static std::optional<Keypair> load_key(const std::string &file_path,
                                               const std::string &password);

        /**
         * Save keypair to file (encrypted)
         */
        static bool save_key(const Keypair &keypair,
                             const std::string &file_path,
                             const std::string &password);

        /**
         * Derive address from public key
         */
        static std::string derive_address(const Keypair &keypair);

        /**
         * Import from BIP39 seed phrase
         */
        static std::optional<Keypair> import_from_seed(const std::string &seed_phrase,
                                                       KeyType type);

    private:
        static std::string derive_address_sr25519(const std::string &public_key);
        static std::string derive_address_ed25519(const std::string &public_key);
    };

    /**
     * Transaction Signer
     * Signs transactions with sr25519
     */
    class TransactionSigner
    {
    public:
        /**
         * Constructor
         * @param signer_keypair: Keypair to sign with
         */
        explicit TransactionSigner(const Keypair &signer_keypair);

        /**
         * Sign transaction
         * @param tx_data: Transaction bytes to sign
         * @return Signature or empty
         */
        std::string sign_transaction(const std::vector<uint8_t> &tx_data);

        /**
         * Verify signature
         * @param tx_data: Original data
         * @param signature: Signature to verify
         * @return true if valid
         */
        bool verify_signature(const std::vector<uint8_t> &tx_data,
                              const std::string &signature) const;

        /**
         * Get signer address
         */
        std::string get_signer_address() const;

        /**
         * Check if this is the signer for a signature
         */
        bool is_signer_of(const std::string &signature) const;

    private:
        Keypair keypair_;
        std::string signer_address_;

        /**
         * SR25519 sign operation
         */
        std::string sr25519_sign(const std::vector<uint8_t> &message);

        /**
         * SR25519 verify operation
         */
        bool sr25519_verify(const std::vector<uint8_t> &message,
                            const std::string &signature) const;
    };

    /**
     * Finality Vote Signer
     * Signs GRANDPA finality votes with ed25519
     */
    class FinallityVoteSigner
    {
    public:
        /**
         * Constructor
         * @param voter_keypair: ed25519 keypair for validator
         */
        explicit FinallityVoteSigner(const Keypair &voter_keypair);

        /**
         * Sign finality vote
         */
        std::string sign_vote(uint32_t block_height, const std::string &block_hash);

        /**
         * Create finality vote
         */
        FinalityVote create_vote(uint32_t block_height, const std::string &block_hash);

        /**
         * Verify finality vote
         */
        bool verify_vote(const FinalityVote &vote) const;

    private:
        Keypair keypair_;
        std::string voter_address_;

        /**
         * ED25519 sign operation
         */
        std::string ed25519_sign(const std::vector<uint8_t> &message);
    };

    /**
     * Transaction Submitter
     * Submits signed transactions to the network
     */
    class TransactionSubmitter
    {
    public:
        /**
         * Constructor
         * @param node_rpc_url: Node RPC endpoint
         */
        explicit TransactionSubmitter(const std::string &node_rpc_url);
        TransactionSubmitter(const std::string &node_rpc_url, SubmissionTransportMode mode);
        ~TransactionSubmitter();

        void set_transport_mode(SubmissionTransportMode mode);
        SubmissionTransportMode get_transport_mode() const;

        /**
         * Submit signed transaction
         * @param signed_tx: Signed transaction
         * @return Submission result
         */
        SubmissionResult submit(const SignedTransaction &signed_tx);

        /**
         * Submit batch of transactions
         */
        std::vector<SubmissionResult> submit_batch(
            const std::vector<SignedTransaction> &signed_txs);

        /**
         * Get transaction status
         */
        SubmissionResult get_submission_status(const std::string &tx_hash);

        /**
         * Wait for transaction inclusion (non-blocking with callback)
         */
        void wait_for_inclusion(const std::string &tx_hash,
                                std::function<void(const SubmissionResult &)> callback);

    private:
        std::string rpc_url_;
        SubmissionTransportMode transport_mode_ = SubmissionTransportMode::MOCK;
        std::map<std::string, SubmissionResult> submission_cache_;

        /**
         * RPC call to submit transaction
         */
        Json::Value rpc_submit_extrinsic(const SignedTransaction &signed_tx);

        std::thread wait_thread_; ///< Managed thread for wait_for_inclusion
    };

    /**
     * Mempool Monitor
     * Monitors transaction mempool
     */
    class MempoolMonitor
    {
    public:
        /**
         * Constructor
         * @param node_rpc_url: Node RPC endpoint
         */
        explicit MempoolMonitor(const std::string &node_rpc_url);
        ~MempoolMonitor();

        /**
         * Get current mempool size
         */
        uint32_t get_mempool_size();

        /**
         * Get transaction in mempool by hash
         */
        std::optional<SignedTransaction> get_mempool_transaction(const std::string &tx_hash);

        /**
         * Get transaction priority (based on fee)
         */
        double get_transaction_priority(const std::string &tx_hash);

        /**
         * Monitor mempool changes
         */
        void monitor_mempool(std::function<void(const std::vector<std::string> &)> callback);

        /**
         * Estimate inclusion time
         * @param fee_amount: Transaction fee
         * @return Estimated blocks to inclusion
         */
        uint32_t estimate_inclusion_blocks(uint64_t fee_amount);

        /**
         * Wait for transaction inclusion in mempool
         * @param tx_hash: Transaction hash
         * @param timeout_ms: Max wait time
         * @return true if included
         */
        bool wait_for_inclusion(const std::string &tx_hash, uint64_t timeout_ms);

    private:
        std::string rpc_url_;
        std::atomic<bool> monitoring_{false};
        std::thread monitor_thread_; ///< Managed monitoring thread

        /**
         * RPC query for mempool
         */
        Json::Value rpc_get_mempool();
    };

    /**
     * Batch Signer
     * Efficiently signs multiple transactions
     */
    class BatchSigner
    {
    public:
        /**
         * Add transaction to batch
         */
        void add_transaction(const std::vector<uint8_t> &tx_data);

        /**
         * Sign all transactions in batch
         * @param keypair: Key to sign with
         * @return Signatures
         */
        std::vector<std::string> sign_batch(const Keypair &keypair);

        /**
         * Get batch size
         */
        size_t batch_size() const;

        /**
         * Clear batch
         */
        void clear();

    private:
        std::vector<std::vector<uint8_t>> transaction_batch_;
    };

    /**
     * Signature Verifier
     * Verifies signatures from transactions
     */
    class SignatureVerifier
    {
    public:
        /**
         * Verify sr25519 signature
         */
        static bool verify_sr25519(const std::vector<uint8_t> &message,
                                   const std::string &signature,
                                   const std::string &public_key);

        /**
         * Verify ed25519 signature
         */
        static bool verify_ed25519(const std::vector<uint8_t> &message,
                                   const std::string &signature,
                                   const std::string &public_key);

        /**
         * Recover signer address from signature
         */
        static std::optional<std::string> recover_signer(
            const std::vector<uint8_t> &message,
            const std::string &signature);
    };

} // namespace midnight::signing_submission
