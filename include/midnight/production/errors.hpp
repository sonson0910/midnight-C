#pragma once

#include <string>

namespace midnight::production
{
    enum class ErrorCode
    {
        None,
        InvalidAddress,
        InvalidContractAddress,
        InvalidTokenType,
        InvalidAmount,
        InvalidHash,
        ToolkitError,
        LedgerBuildError,
        ProofServerError,
        NodeRpcError,
        IndexerError,
        SubmissionRejected,
        ConfirmationTimeout,
        ArtifactError,
        UnsupportedOperation
    };

    struct SdkError
    {
        ErrorCode code = ErrorCode::None;
        std::string message;
        std::string detail;

        explicit operator bool() const
        {
            return code != ErrorCode::None || !message.empty() || !detail.empty();
        }
    };

    inline const char *to_string(ErrorCode code)
    {
        switch (code)
        {
        case ErrorCode::None:
            return "none";
        case ErrorCode::InvalidAddress:
            return "invalid_address";
        case ErrorCode::InvalidContractAddress:
            return "invalid_contract_address";
        case ErrorCode::InvalidTokenType:
            return "invalid_token_type";
        case ErrorCode::InvalidAmount:
            return "invalid_amount";
        case ErrorCode::InvalidHash:
            return "invalid_hash";
        case ErrorCode::ToolkitError:
            return "toolkit_error";
        case ErrorCode::LedgerBuildError:
            return "ledger_build_error";
        case ErrorCode::ProofServerError:
            return "proof_server_error";
        case ErrorCode::NodeRpcError:
            return "node_rpc_error";
        case ErrorCode::IndexerError:
            return "indexer_error";
        case ErrorCode::SubmissionRejected:
            return "submission_rejected";
        case ErrorCode::ConfirmationTimeout:
            return "confirmation_timeout";
        case ErrorCode::ArtifactError:
            return "artifact_error";
        case ErrorCode::UnsupportedOperation:
            return "unsupported_operation";
        }
        return "unknown";
    }

} // namespace midnight::production
