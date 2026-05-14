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
        IndexerSchemaError,
        LedgerBackendError,
        LedgerBuildError,
        ProofServerError,
        NodeRpcError,
        NodeRejectedTx,
        IndexerError,
        SubmissionRejected,
        ConfirmationTimeout,
        FinalityTimeout,
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
        case ErrorCode::IndexerSchemaError:
            return "indexer_schema_error";
        case ErrorCode::LedgerBackendError:
            return "ledger_backend_error";
        case ErrorCode::LedgerBuildError:
            return "ledger_build_error";
        case ErrorCode::ProofServerError:
            return "proof_server_error";
        case ErrorCode::NodeRpcError:
            return "node_rpc_error";
        case ErrorCode::NodeRejectedTx:
            return "node_rejected_tx";
        case ErrorCode::IndexerError:
            return "indexer_error";
        case ErrorCode::SubmissionRejected:
            return "submission_rejected";
        case ErrorCode::ConfirmationTimeout:
            return "confirmation_timeout";
        case ErrorCode::FinalityTimeout:
            return "finality_timeout";
        case ErrorCode::ArtifactError:
            return "artifact_error";
        case ErrorCode::UnsupportedOperation:
            return "unsupported_operation";
        }
        return "unknown";
    }

} // namespace midnight::production
