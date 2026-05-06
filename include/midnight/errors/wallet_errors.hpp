#pragma once

#include <string>
#include <variant>
#include <vector>
#include <optional>
#include <map>
#include <cstdint>

namespace midnight::wallet::errors {

// ─── Error Categories ──────────────────────────────────────────

enum class ErrorCategory {
    Sync,
    Insufficient,
    Invalid,
    Network,
    Proof,
    Crypto,
    State
};

enum class ErrorCode {
    InsufficientFunds = 1001,
    InvalidCoinHashes = 1002,
    SyncWallet = 2001,
    NetworkRequest = 3001,
    NetworkTimeout = 3002,
    ProofGeneration = 4001,
    ProofVerification = 4002,
    InvalidAddress = 5001,
    InvalidMnemonic = 5002,
    SerializationFailed = 5003,
    EncryptionFailed = 5004,
    DecryptionFailed = 5005,
    Unknown = 9999
};

// ─── Specific Error Types ─────────────────────────────────────

struct InsufficientFundsError {
    uint64_t available = 0;
    uint64_t required = 0;
    std::string token_type;
    
    static ErrorCategory category() { return ErrorCategory::Insufficient; }
    static ErrorCode code() { return ErrorCode::InsufficientFunds; }
};

struct InvalidCoinHashesError {
    std::vector<std::string> invalid_hashes;
    
    static ErrorCategory category() { return ErrorCategory::Invalid; }
    static ErrorCode code() { return ErrorCode::InvalidCoinHashes; }
};

struct SyncWalletError {
    std::string message;
    uint64_t last_synced_height = 0;
    uint64_t current_height = 0;
    
    static ErrorCategory category() { return ErrorCategory::Sync; }
    static ErrorCode code() { return ErrorCode::SyncWallet; }
};

struct NetworkError {
    std::string endpoint;
    int http_code = 0;
    std::string message;
    bool is_timeout = false;
    
    static ErrorCategory category() { return ErrorCategory::Network; }
    static ErrorCode code() { return ErrorCode::NetworkRequest; }
};

struct ProofError {
    std::string circuit_name;
    std::string message;
    uint64_t generation_time_ms = 0;
    bool is_verification_failed = false;
    
    static ErrorCategory category() { return ErrorCategory::Proof; }
    static ErrorCode code() { return ErrorCode::ProofGeneration; }
};

struct InvalidAddressError {
    std::string address;
    std::string reason;
    
    static ErrorCategory category() { return ErrorCategory::Invalid; }
    static ErrorCode code() { return ErrorCode::InvalidAddress; }
};

struct InvalidMnemonicError {
    std::string mnemonic;
    std::string reason;
    
    static ErrorCategory category() { return ErrorCategory::Invalid; }
    static ErrorCode code() { return ErrorCode::InvalidMnemonic; }
};

struct SerializationError {
    std::string details;
    bool is_encryption = false;
    
    static ErrorCategory category() { return ErrorCategory::State; }
    static ErrorCode code() { return ErrorCode::SerializationFailed; }
};

struct EncryptionError {
    std::string operation;
    std::string reason;
    
    static ErrorCategory category() { return ErrorCategory::Crypto; }
    ErrorCode code() const {
        return operation == "encrypt" ? ErrorCode::EncryptionFailed : ErrorCode::DecryptionFailed;
    }
};

// ─── Union Error Type ─────────────────────────────────────────

using WalletError = std::variant<
    InsufficientFundsError,
    InvalidCoinHashesError,
    SyncWalletError,
    NetworkError,
    ProofError,
    InvalidAddressError,
    InvalidMnemonicError,
    SerializationError,
    EncryptionError
>;

// ─── Error Accessors ───────────────────────────────────────────

ErrorCategory get_category(const WalletError& error);
ErrorCode get_code(const WalletError& error);
std::string get_message(const WalletError& error);
bool is_retryable(const WalletError& error);

// ─── Serialization ─────────────────────────────────────────────

std::string to_json(const WalletError& error);
std::optional<WalletError> from_json(const std::string& json_str);

// ─── Error Builders ────────────────────────────────────────────

WalletError insufficient_funds(uint64_t available, uint64_t required, const std::string& token_type = "NIGHT");
WalletError invalid_coin_hashes(const std::vector<std::string>& hashes);
WalletError sync_error(const std::string& message, uint64_t last_synced = 0, uint64_t current = 0);
WalletError network_error(const std::string& endpoint, const std::string& message, int http_code = 0);
WalletError proof_error(const std::string& circuit, const std::string& message);
WalletError invalid_address(const std::string& address, const std::string& reason);
WalletError invalid_mnemonic(const std::string& mnemonic, const std::string& reason);
WalletError serialization_error(const std::string& details, bool is_encryption = false);
WalletError encryption_error(const std::string& operation, const std::string& reason);

// ─── Visitor Helper ───────────────────────────────────────────

template<typename Visitor>
auto visit_error(const WalletError& error, Visitor&& visitor) {
    return std::visit(std::forward<Visitor>(visitor), error);
}

} // namespace midnight::wallet::errors
