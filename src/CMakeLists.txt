# Core library source files

set(MIDNIGHT_CORE_SOURCES
    core/config.cpp
    core/logger.cpp
    session/session_manager.cpp
)

set(MIDNIGHT_PROTOCOL_SOURCES)

# Protocol implementations
if(ENABLE_MQTT)
    list(APPEND MIDNIGHT_PROTOCOL_SOURCES
        protocols/mqtt/mqtt_client.cpp
        protocols/mqtt/mqtt_connection.cpp
    )
endif()

if(ENABLE_COAP)
    list(APPEND MIDNIGHT_PROTOCOL_SOURCES
        protocols/coap/coap_client.cpp
        protocols/coap/coap_message.cpp
    )
endif()

if(ENABLE_HTTP)
    list(APPEND MIDNIGHT_PROTOCOL_SOURCES
        protocols/http/http_client.cpp
        protocols/http/http_request.cpp
    )
endif()

if(ENABLE_WEBSOCKET)
    list(APPEND MIDNIGHT_PROTOCOL_SOURCES
        protocols/websocket/ws_client.cpp
        protocols/websocket/ws_connection.cpp
    )
endif()

set(MIDNIGHT_BLOCKCHAIN_SOURCES)

# Network support
set(MIDNIGHT_NETWORK_SOURCES)
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_NETWORK_SOURCES
        network/network_client.cpp
        network/midnight_node_rpc.cpp
        network/transaction_confirmation.cpp
    )
endif()

# Crypto support (Ed25519, CBOR, etc.)
set(MIDNIGHT_CRYPTO_SOURCES)
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_CRYPTO_SOURCES
        crypto/ed25519_signer.cpp
    )
endif()

# Zero-Knowledge Proof support (ZK proofs, circuits, witnesses)
set(MIDNIGHT_ZK_SOURCES)
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_ZK_SOURCES
        zk/proof_types.cpp
        zk/proof_server_client.cpp
        zk/private_state.cpp
        zk/proof_server_resilient_client.cpp
        zk/ledger_state_sync.cpp
    )
endif()

# Blockchain support
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_BLOCKCHAIN_SOURCES
        blockchain/chain_manager.cpp
        blockchain/transaction.cpp
        blockchain/block.cpp
        blockchain/wallet.cpp
        blockchain/midnight_adapter.cpp
        utxo-transactions/utxo_transactions.cpp
        compact-contracts/compact_contracts.cpp
        zk-proofs/zk_proofs.cpp
        monitoring-finality/monitoring_finality.cpp
        signing-submission/signing_submission.cpp
    )
endif()

# Create library
add_library(midnight-core
    ${MIDNIGHT_CORE_SOURCES}
    ${MIDNIGHT_PROTOCOL_SOURCES}
    ${MIDNIGHT_NETWORK_SOURCES}
    ${MIDNIGHT_CRYPTO_SOURCES}
    ${MIDNIGHT_ZK_SOURCES}
    ${MIDNIGHT_BLOCKCHAIN_SOURCES}
)

target_include_directories(midnight-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${HTTPLIB_SOURCE_DIR}
)

# Link dependencies
find_package(OpenSSL QUIET)
find_package(Sodium QUIET)

target_link_libraries(midnight-core
    PUBLIC
        nlohmann_json::nlohmann_json
        fmt::fmt
        $<$<TARGET_EXISTS:jsoncpp_lib>:jsoncpp_lib>
        $<$<TARGET_EXISTS:JsonCpp::JsonCpp>:JsonCpp::JsonCpp>
    PRIVATE
        $<$<BOOL:${OPENSSL_FOUND}>:OpenSSL::SSL>
        $<$<BOOL:${OPENSSL_FOUND}>:OpenSSL::Crypto>
        $<$<BOOL:${Sodium_FOUND}>:Sodium::Sodium>
)

# Set warning levels
if(MSVC)
    target_compile_options(midnight-core PRIVATE /W4)
else()
    target_compile_options(midnight-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Set C++ standard
target_compile_features(midnight-core PUBLIC cxx_std_20)
