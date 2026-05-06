#include "midnight/errors/wallet_errors.hpp"
#include <sstream>
#include <iomanip>

namespace midnight::wallet::errors {

// ─── Category ────────────────────────────────────────────────

ErrorCategory get_category(const WalletError& error) {
    struct CategoryVisitor {
        ErrorCategory operator()(const InsufficientFundsError&) { return ErrorCategory::Insufficient; }
        ErrorCategory operator()(const InvalidCoinHashesError&) { return ErrorCategory::Invalid; }
        ErrorCategory operator()(const SyncWalletError&) { return ErrorCategory::Sync; }
        ErrorCategory operator()(const NetworkError&) { return ErrorCategory::Network; }
        ErrorCategory operator()(const ProofError&) { return ErrorCategory::Proof; }
        ErrorCategory operator()(const InvalidAddressError&) { return ErrorCategory::Invalid; }
        ErrorCategory operator()(const InvalidMnemonicError&) { return ErrorCategory::Invalid; }
        ErrorCategory operator()(const SerializationError&) { return ErrorCategory::State; }
        ErrorCategory operator()(const EncryptionError&) { return ErrorCategory::Crypto; }
    };
    return std::visit(CategoryVisitor{}, error);
}

// ─── Code ───────────────────────────────────────────────────

ErrorCode get_code(const WalletError& error) {
    struct CodeVisitor {
        ErrorCode operator()(const InsufficientFundsError&) { return ErrorCode::InsufficientFunds; }
        ErrorCode operator()(const InvalidCoinHashesError&) { return ErrorCode::InvalidCoinHashes; }
        ErrorCode operator()(const SyncWalletError&) { return ErrorCode::SyncWallet; }
        ErrorCode operator()(const NetworkError& e) { 
            return e.is_timeout ? ErrorCode::NetworkTimeout : ErrorCode::NetworkRequest; 
        }
        ErrorCode operator()(const ProofError& e) { 
            return e.is_verification_failed ? ErrorCode::ProofVerification : ErrorCode::ProofGeneration; 
        }
        ErrorCode operator()(const InvalidAddressError&) { return ErrorCode::InvalidAddress; }
        ErrorCode operator()(const InvalidMnemonicError&) { return ErrorCode::InvalidMnemonic; }
        ErrorCode operator()(const SerializationError&) { return ErrorCode::SerializationFailed; }
        ErrorCode operator()(const EncryptionError& e) {
            return e.operation == "encrypt" ? ErrorCode::EncryptionFailed : ErrorCode::DecryptionFailed;
        }
    };
    return std::visit(CodeVisitor{}, error);
}

// ─── Message ─────────────────────────────────────────────────

std::string get_message(const WalletError& error) {
    struct MessageVisitor {
        std::string operator()(const InsufficientFundsError& e) {
            std::ostringstream oss;
            oss << "Insufficient " << e.token_type << " funds: available=" 
                << e.available << ", required=" << e.required;
            return oss.str();
        }
        std::string operator()(const InvalidCoinHashesError& e) {
            std::ostringstream oss;
            oss << "Invalid coin hashes: " << e.invalid_hashes.size() << " invalid";
            if (!e.invalid_hashes.empty()) {
                oss << " (first: " << e.invalid_hashes[0].substr(0, 16) << "...)";
            }
            return oss.str();
        }
        std::string operator()(const SyncWalletError& e) {
            std::ostringstream oss;
            oss << "Wallet sync failed: " << e.message;
            if (e.last_synced_height > 0 || e.current_height > 0) {
                oss << " (synced=" << e.last_synced_height << ", current=" << e.current_height << ")";
            }
            return oss.str();
        }
        std::string operator()(const NetworkError& e) {
            std::ostringstream oss;
            oss << "Network error";
            if (!e.endpoint.empty()) {
                oss << " for " << e.endpoint;
            }
            if (e.http_code != 0) {
                oss << " (HTTP " << e.http_code << ")";
            }
            if (!e.message.empty()) {
                oss << ": " << e.message;
            }
            return oss.str();
        }
        std::string operator()(const ProofError& e) {
            std::ostringstream oss;
            oss << "Proof error";
            if (!e.circuit_name.empty()) {
                oss << " for circuit '" << e.circuit_name << "'";
            }
            if (!e.message.empty()) {
                oss << ": " << e.message;
            }
            if (e.generation_time_ms > 0) {
                oss << " (took " << e.generation_time_ms << "ms)";
            }
            return oss.str();
        }
        std::string operator()(const InvalidAddressError& e) {
            std::ostringstream oss;
            oss << "Invalid address";
            if (!e.address.empty()) {
                oss << ": " << e.address.substr(0, 20) << "...";
            }
            if (!e.reason.empty()) {
                oss << " - " << e.reason;
            }
            return oss.str();
        }
        std::string operator()(const InvalidMnemonicError& e) {
            std::ostringstream oss;
            oss << "Invalid mnemonic";
            if (!e.reason.empty()) {
                oss << ": " << e.reason;
            }
            return oss.str();
        }
        std::string operator()(const SerializationError& e) {
            std::ostringstream oss;
            oss << "Serialization failed";
            if (!e.details.empty()) {
                oss << ": " << e.details;
            }
            return oss.str();
        }
        std::string operator()(const EncryptionError& e) {
            std::ostringstream oss;
            oss << "Encryption operation '" << e.operation << "' failed";
            if (!e.reason.empty()) {
                oss << ": " << e.reason;
            }
            return oss.str();
        }
    };
    return std::visit(MessageVisitor{}, error);
}

// ─── Retryable ───────────────────────────────────────────────

bool is_retryable(const WalletError& error) {
    struct RetryableVisitor {
        bool operator()(const InsufficientFundsError&) { return false; }
        bool operator()(const InvalidCoinHashesError&) { return false; }
        bool operator()(const SyncWalletError&) { return true; }
        bool operator()(const NetworkError& e) { 
            return e.is_timeout || e.http_code >= 500;
        }
        bool operator()(const ProofError& e) { 
            return !e.is_verification_failed;
        }
        bool operator()(const InvalidAddressError&) { return false; }
        bool operator()(const InvalidMnemonicError&) { return false; }
        bool operator()(const SerializationError&) { return false; }
        bool operator()(const EncryptionError&) { return false; }
    };
    return std::visit(RetryableVisitor{}, error);
}

// ─── JSON Serialization ──────────────────────────────────────

static std::string escape_json_string(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c; break;
        }
    }
    return oss.str();
}

std::string to_json(const WalletError& error) {
    std::ostringstream oss;
    oss << "{";
    
    struct JsonVisitor {
        std::ostringstream& oss;
        
        void operator()(const InsufficientFundsError& e) {
            oss << "\"type\":\"InsufficientFunds\","
                << "\"available\":" << e.available << ","
                << "\"required\":" << e.required << ","
                << "\"token_type\":\"" << escape_json_string(e.token_type) << "\"";
        }
        void operator()(const InvalidCoinHashesError& e) {
            oss << "\"type\":\"InvalidCoinHashes\","
                << "\"invalid_hashes\":[";
            for (size_t i = 0; i < e.invalid_hashes.size(); ++i) {
                if (i > 0) oss << ",";
                oss << "\"" << escape_json_string(e.invalid_hashes[i]) << "\"";
            }
            oss << "]";
        }
        void operator()(const SyncWalletError& e) {
            oss << "\"type\":\"SyncWallet\","
                << "\"message\":\"" << escape_json_string(e.message) << "\","
                << "\"last_synced_height\":" << e.last_synced_height << ","
                << "\"current_height\":" << e.current_height;
        }
        void operator()(const NetworkError& e) {
            oss << "\"type\":\"Network\","
                << "\"endpoint\":\"" << escape_json_string(e.endpoint) << "\","
                << "\"http_code\":" << e.http_code << ","
                << "\"message\":\"" << escape_json_string(e.message) << "\","
                << "\"is_timeout\":" << (e.is_timeout ? "true" : "false");
        }
        void operator()(const ProofError& e) {
            oss << "\"type\":\"Proof\","
                << "\"circuit_name\":\"" << escape_json_string(e.circuit_name) << "\","
                << "\"message\":\"" << escape_json_string(e.message) << "\","
                << "\"generation_time_ms\":" << e.generation_time_ms << ","
                << "\"is_verification_failed\":" << (e.is_verification_failed ? "true" : "false");
        }
        void operator()(const InvalidAddressError& e) {
            oss << "\"type\":\"InvalidAddress\","
                << "\"address\":\"" << escape_json_string(e.address) << "\","
                << "\"reason\":\"" << escape_json_string(e.reason) << "\"";
        }
        void operator()(const InvalidMnemonicError& e) {
            oss << "\"type\":\"InvalidMnemonic\","
                << "\"mnemonic\":\"" << escape_json_string(e.mnemonic) << "\","
                << "\"reason\":\"" << escape_json_string(e.reason) << "\"";
        }
        void operator()(const SerializationError& e) {
            oss << "\"type\":\"Serialization\","
                << "\"details\":\"" << escape_json_string(e.details) << "\","
                << "\"is_encryption\":" << (e.is_encryption ? "true" : "false");
        }
        void operator()(const EncryptionError& e) {
            oss << "\"type\":\"Encryption\","
                << "\"operation\":\"" << escape_json_string(e.operation) << "\","
                << "\"reason\":\"" << escape_json_string(e.reason) << "\"";
        }
    };
    
    std::visit(JsonVisitor{oss}, error);
    oss << "}";
    return oss.str();
}

std::optional<WalletError> from_json(const std::string& json_str) {
    try {
        std::string s = json_str;
        
        auto type_start = s.find("\"type\":\"");
        if (type_start == std::string::npos) return std::nullopt;
        auto type_end = s.find("\"", type_start + 8);
        std::string type = s.substr(type_start + 7, type_end - type_start - 7);
        
        if (type == "InsufficientFunds") {
            InsufficientFundsError e;
            
            auto avail_start = s.find("\"available\":");
            if (avail_start != std::string::npos) {
                auto comma = s.find(",", avail_start);
                e.available = std::stoull(s.substr(avail_start + 11, comma - avail_start - 11));
            }
            auto req_start = s.find("\"required\":");
            if (req_start != std::string::npos) {
                auto comma = s.find(",", req_start);
                e.required = std::stoull(s.substr(req_start + 11, comma - req_start - 11));
            }
            auto token_start = s.find("\"token_type\":\"");
            if (token_start != std::string::npos) {
                auto end = s.find("\"", token_start + 14);
                e.token_type = s.substr(token_start + 14, end - token_start - 14);
            }
            return e;
        }
        else if (type == "SyncWallet") {
            SyncWalletError e;
            auto msg_start = s.find("\"message\":\"");
            if (msg_start != std::string::npos) {
                auto end = s.find("\"", msg_start + 11);
                e.message = s.substr(msg_start + 11, end - msg_start - 11);
            }
            return e;
        }
        else if (type == "Network") {
            NetworkError e;
            auto ep_start = s.find("\"endpoint\":\"");
            if (ep_start != std::string::npos) {
                auto end = s.find("\"", ep_start + 13);
                e.endpoint = s.substr(ep_start + 13, end - ep_start - 13);
            }
            auto code_start = s.find("\"http_code\":");
            if (code_start != std::string::npos) {
                auto comma = s.find(",", code_start);
                e.http_code = std::stoi(s.substr(code_start + 12, comma - code_start - 12));
            }
            auto msg_start = s.find("\"message\":\"");
            if (msg_start != std::string::npos) {
                auto end = s.find("\"", msg_start + 11);
                e.message = s.substr(msg_start + 11, end - msg_start - 11);
            }
            return e;
        }
        else if (type == "Proof") {
            ProofError e;
            auto circuit_start = s.find("\"circuit_name\":\"");
            if (circuit_start != std::string::npos) {
                auto end = s.find("\"", circuit_start + 16);
                e.circuit_name = s.substr(circuit_start + 16, end - circuit_start - 16);
            }
            return e;
        }
        else if (type == "InvalidAddress") {
            InvalidAddressError e;
            auto addr_start = s.find("\"address\":\"");
            if (addr_start != std::string::npos) {
                auto end = s.find("\"", addr_start + 11);
                e.address = s.substr(addr_start + 11, end - addr_start - 11);
            }
            auto reason_start = s.find("\"reason\":\"");
            if (reason_start != std::string::npos) {
                auto end = s.find("\"", reason_start + 10);
                e.reason = s.substr(reason_start + 10, end - reason_start - 10);
            }
            return e;
        }
        else if (type == "InvalidMnemonic") {
            InvalidMnemonicError e;
            auto reason_start = s.find("\"reason\":\"");
            if (reason_start != std::string::npos) {
                auto end = s.find("\"", reason_start + 10);
                e.reason = s.substr(reason_start + 10, end - reason_start - 10);
            }
            return e;
        }
        else if (type == "Serialization") {
            SerializationError e;
            auto details_start = s.find("\"details\":\"");
            if (details_start != std::string::npos) {
                auto end = s.find("\"", details_start + 11);
                e.details = s.substr(details_start + 11, end - details_start - 11);
            }
            return e;
        }
        else if (type == "Encryption") {
            EncryptionError e;
            auto op_start = s.find("\"operation\":\"");
            if (op_start != std::string::npos) {
                auto end = s.find("\"", op_start + 13);
                e.operation = s.substr(op_start + 13, end - op_start - 13);
            }
            auto reason_start = s.find("\"reason\":\"");
            if (reason_start != std::string::npos) {
                auto end = s.find("\"", reason_start + 10);
                e.reason = s.substr(reason_start + 10, end - reason_start - 10);
            }
            return e;
        }
        
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

// ─── Error Builders ──────────────────────────────────────────

WalletError insufficient_funds(uint64_t available, uint64_t required, const std::string& token_type) {
    return InsufficientFundsError{available, required, token_type};
}

WalletError invalid_coin_hashes(const std::vector<std::string>& hashes) {
    return InvalidCoinHashesError{hashes};
}

WalletError sync_error(const std::string& message, uint64_t last_synced, uint64_t current) {
    return SyncWalletError{message, last_synced, current};
}

WalletError network_error(const std::string& endpoint, const std::string& message, int http_code) {
    return NetworkError{endpoint, http_code, message, false};
}

WalletError proof_error(const std::string& circuit, const std::string& message) {
    return ProofError{circuit, message, 0, false};
}

WalletError invalid_address(const std::string& address, const std::string& reason) {
    return InvalidAddressError{address, reason};
}

WalletError invalid_mnemonic(const std::string& mnemonic, const std::string& reason) {
    return InvalidMnemonicError{mnemonic, reason};
}

WalletError serialization_error(const std::string& details, bool is_encryption) {
    return SerializationError{details, is_encryption};
}

WalletError encryption_error(const std::string& operation, const std::string& reason) {
    return EncryptionError{operation, reason};
}

} // namespace midnight::wallet::errors
