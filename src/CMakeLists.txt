# Core library source files

set(MIDNIGHT_CORE_SOURCES
    core/config.cpp
    core/logger.cpp
    session/session_manager.cpp
    wallet/shielded_address.cpp
)

set(MIDNIGHT_PROTOCOL_SOURCES)

# gRPC Protocol Buffers
if(ENABLE_GRPC)
    list(APPEND MIDNIGHT_PROTOCOL_SOURCES
        protocols/grpc/grpc_client.cpp
        protocols/grpc/grpc_server.cpp
    )

    # Generate protobuf files from proto
    find_package(Protobuf REQUIRED)

    set(GRPC_PROTO "${CMAKE_SOURCE_DIR}/proto/midnight.proto")
    set(GRPC_PB_SRC "${CMAKE_BINARY_DIR}/midnight.pb.cc")
    set(GRPC_PB_HDR "${CMAKE_BINARY_DIR}/midnight.pb.h")
    set(GRPC_GRPC_SRC "${CMAKE_BINARY_DIR}/midnight.grpc.pb.cc")
    set(GRPC_GRPC_HDR "${CMAKE_BINARY_DIR}/midnight.grpc.pb.h")

    add_custom_command(
        OUTPUT ${GRPC_PB_SRC} ${GRPC_PB_HDR} ${GRPC_GRPC_SRC} ${GRPC_GRPC_HDR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${CMAKE_SOURCE_DIR}/proto
            --cpp_out=${CMAKE_BINARY_DIR}
            --grpc_out=${CMAKE_BINARY_DIR}
            --plugin=protoc-gen-grpc=${gRPC_GRPCPP_PLUGIN}
            ${GRPC_PROTO}
        DEPENDS ${GRPC_PROTO} ${Protobuf_PROTOC_EXECUTABLE} ${gRPC_GRPCPP_PLUGIN}
        COMMENT "Generating gRPC protobuf files from midnight.proto"
        VERBATIM
    )

    # Add generated sources to protocol sources
    list(APPEND MIDNIGHT_PROTOCOL_SOURCES
        ${GRPC_PB_SRC}
        ${GRPC_GRPC_SRC}
    )

    # Include directories for generated protobuf
    target_include_directories(midnight-core PRIVATE ${CMAKE_BINARY_DIR})
endif()

# Protocol implementations
if(ENABLE_MQTT)
    list(APPEND MIDNIGHT_PROTOCOL_SOURCES
        protocols/mqtt/mqtt_protocol.cpp
        protocols/mqtt/mqtt_connection.cpp
        protocols/mqtt/mqtt_client.cpp
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
    )
endif()

if(ENABLE_WEBSOCKET)
    list(APPEND MIDNIGHT_PROTOCOL_SOURCES
        protocols/websocket/ws_client.cpp
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
        network/indexer_client.cpp
        network/indexer_subscription.cpp
        network/graphql_subscription.cpp
        network/substrate_rpc.cpp
    )
endif()

# Crypto support (Ed25519, ECDSA, SR25519, BLS, BIP340, etc.)
set(MIDNIGHT_CRYPTO_SOURCES)
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_CRYPTO_SOURCES
        crypto/ed25519_signer.cpp
        crypto/bip340_signer.cpp
        crypto/sr25519_signer.cpp
        crypto/ecdsa_signer.cpp
        crypto/bls_signer.cpp
        crypto/key_encryption.cpp
        crypto/state_encryption.cpp
        crypto/merkle_tree.cpp
    )
endif()

# SCALE codec (Substrate binary serialization)
set(MIDNIGHT_CODEC_SOURCES)
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_CODEC_SOURCES
        codec/scale_codec.cpp
    )
endif()

# HD Wallet (BIP39 + SLIP-10 Ed25519) + Bech32m address encoding
set(MIDNIGHT_WALLET_SOURCES)
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_WALLET_SOURCES
        wallet/hd_wallet.cpp
        wallet/bech32m.cpp
        wallet/wallet_facade.cpp
        wallet/wallet_services.cpp
        wallet/transaction_balancer.cpp
        errors/wallet_errors.cpp
    )
endif()

# Transaction builder (Substrate extrinsics + Midnight transaction)
set(MIDNIGHT_TX_SOURCES)
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_TX_SOURCES
        tx/extrinsic_builder.cpp
    )
endif()

# Zero-Knowledge Proof support (ZK proofs, circuits, witnesses)
set(MIDNIGHT_ZK_SOURCES)
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_ZK_SOURCES
        zk/proof_types.cpp
        zk/proof_server_client.cpp
        zk/private_state.cpp
    )
endif()

# Blockchain support
if(ENABLE_BLOCKCHAIN)
    list(APPEND MIDNIGHT_BLOCKCHAIN_SOURCES
        blockchain/chain_manager.cpp
        blockchain/block.cpp
        blockchain/wallet.cpp
        blockchain/wallet_manager.cpp
        blockchain/midnight_adapter.cpp
        utxo-transactions/utxo_transactions.cpp
        ledger/ledger_backend.cpp
        monitoring-finality/monitoring_finality.cpp
        signing-submission/signing_submission.cpp
        production/midnight_client.cpp
        production/artifact_manager.cpp
        production/state_backend.cpp
        contracts/contract_interactor.cpp
        contract/contract_manager.cpp
    )
endif()

# Create library
add_library(midnight-core
    ${MIDNIGHT_CORE_SOURCES}
    ${MIDNIGHT_PROTOCOL_SOURCES}
    ${MIDNIGHT_NETWORK_SOURCES}
    ${MIDNIGHT_CRYPTO_SOURCES}
    ${MIDNIGHT_CODEC_SOURCES}
    ${MIDNIGHT_WALLET_SOURCES}
    ${MIDNIGHT_TX_SOURCES}
    ${MIDNIGHT_ZK_SOURCES}
    ${MIDNIGHT_BLOCKCHAIN_SOURCES}
)

if(WIN32 AND BUILD_SHARED_LIBS)
    # Ensure an import library is generated for examples linking against the DLL.
    set_target_properties(midnight-core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_compile_definitions(midnight-core
        PRIVATE MIDNIGHT_BUILDING_DLL
        INTERFACE MIDNIGHT_USING_DLL
    )
endif()

target_include_directories(midnight-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${HTTPLIB_SOURCE_DIR}
)

if(DEFINED jsoncpp_SOURCE_DIR)
    target_include_directories(midnight-core PRIVATE "${jsoncpp_SOURCE_DIR}/include")
endif()

# Link dependencies
find_package(OpenSSL QUIET)
find_package(Sodium QUIET)

# Handle jsoncpp linking
# The jsoncpp subdirectory creates jsoncpp_static target (static library)
set(JSONCPP_LIBRARY jsoncpp_static)
if(NOT TARGET ${JSONCPP_LIBRARY})
    # Fallback: try alternative target names
    if(TARGET jsoncpp_lib)
        set(JSONCPP_LIBRARY jsoncpp_lib)
    elseif(TARGET JsonCpp::JsonCpp)
        set(JSONCPP_LIBRARY JsonCpp::JsonCpp)
    else()
        # Fallback: try to find the library
        find_library(JSONCPP_LIBRARY NAMES jsoncpp jsoncpp_lib)
        if(JSONCPP_LIBRARY)
            message(STATUS "Found jsoncpp library: ${JSONCPP_LIBRARY}")
        endif()
    endif()
endif()

# Fallback: use pkg-config for libsodium on systems without CMake find module
if(NOT Sodium_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SODIUM_PKG QUIET libsodium)
        if(SODIUM_PKG_FOUND)
            if(NOT TARGET Sodium::Sodium)
                add_library(Sodium::Sodium INTERFACE IMPORTED)
                set_target_properties(Sodium::Sodium PROPERTIES
                    INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_PKG_INCLUDE_DIRS}"
                    INTERFACE_LINK_LIBRARIES "${SODIUM_PKG_LINK_LIBRARIES}"
                )
            endif()
            set(Sodium_FOUND TRUE)
            message(STATUS "Found libsodium via pkg-config: ${SODIUM_PKG_LINK_LIBRARIES}")
        endif()
    endif()
endif()

# Fallback: direct library search on standard paths
if(NOT Sodium_FOUND)
    find_path(SODIUM_INCLUDE_DIR NAMES sodium.h)
    find_library(SODIUM_LIBRARY NAMES sodium)
    if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
        if(NOT TARGET Sodium::Sodium)
            add_library(Sodium::Sodium UNKNOWN IMPORTED)
            set_target_properties(Sodium::Sodium PROPERTIES
                IMPORTED_LOCATION "${SODIUM_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_DIR}"
            )
        endif()
        set(Sodium_FOUND TRUE)
        message(STATUS "Found libsodium: ${SODIUM_LIBRARY}")
    endif()
endif()

# Fallback to conda-provided crypto libraries when package config files are unavailable.
if(NOT OPENSSL_FOUND AND DEFINED ENV{CONDA_PREFIX})
    set(_MIDNIGHT_CONDA_LIB_ROOT "$ENV{CONDA_PREFIX}/Library")

    find_path(OPENSSL_INCLUDE_DIR
        NAMES openssl/ssl.h
        PATHS "${_MIDNIGHT_CONDA_LIB_ROOT}/include"
        NO_DEFAULT_PATH
    )
    find_library(OPENSSL_SSL_LIBRARY
        NAMES libssl ssl
        PATHS "${_MIDNIGHT_CONDA_LIB_ROOT}/lib"
        NO_DEFAULT_PATH
    )
    find_library(OPENSSL_CRYPTO_LIBRARY
        NAMES libcrypto crypto
        PATHS "${_MIDNIGHT_CONDA_LIB_ROOT}/lib"
        NO_DEFAULT_PATH
    )

    if(OPENSSL_INCLUDE_DIR AND OPENSSL_SSL_LIBRARY AND OPENSSL_CRYPTO_LIBRARY)
        if(NOT TARGET OpenSSL::SSL)
            add_library(OpenSSL::SSL UNKNOWN IMPORTED)
            set_target_properties(OpenSSL::SSL PROPERTIES
                IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
            )
        endif()

        if(NOT TARGET OpenSSL::Crypto)
            add_library(OpenSSL::Crypto UNKNOWN IMPORTED)
            set_target_properties(OpenSSL::Crypto PROPERTIES
                IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
            )
        endif()

        set(OPENSSL_FOUND TRUE)
        message(STATUS "Using OpenSSL from CONDA_PREFIX: ${_MIDNIGHT_CONDA_LIB_ROOT}")
    endif()
endif()

if(NOT Sodium_FOUND AND DEFINED ENV{CONDA_PREFIX})
    set(_MIDNIGHT_CONDA_LIB_ROOT "$ENV{CONDA_PREFIX}/Library")

    find_path(SODIUM_INCLUDE_DIR
        NAMES sodium.h
        PATHS "${_MIDNIGHT_CONDA_LIB_ROOT}/include"
        NO_DEFAULT_PATH
    )
    find_library(SODIUM_LIBRARY
        NAMES libsodium sodium
        PATHS "${_MIDNIGHT_CONDA_LIB_ROOT}/lib"
        NO_DEFAULT_PATH
    )

    if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
        if(NOT TARGET Sodium::Sodium)
            add_library(Sodium::Sodium UNKNOWN IMPORTED)
            set_target_properties(Sodium::Sodium PROPERTIES
                IMPORTED_LOCATION "${SODIUM_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_DIR}"
            )
        endif()

        set(Sodium_FOUND TRUE)
        message(STATUS "Using libsodium from CONDA_PREFIX: ${_MIDNIGHT_CONDA_LIB_ROOT}")
    endif()
endif()

if(ENABLE_BLOCKCHAIN AND MIDNIGHT_REQUIRE_OPENSSL_FOR_BLOCKCHAIN AND NOT OPENSSL_FOUND)
    message(FATAL_ERROR "OpenSSL is required for production Midnight HTTPS endpoints. Install OpenSSL or configure with -DMIDNIGHT_REQUIRE_OPENSSL_FOR_BLOCKCHAIN=OFF for local HTTP-only builds.")
endif()

target_link_libraries(midnight-core
    PUBLIC
        nlohmann_json::nlohmann_json
        fmt::fmt
        $<$<TARGET_EXISTS:jsoncpp_static>:jsoncpp_static>
        $<$<TARGET_EXISTS:jsoncpp_lib>:jsoncpp_lib>
        $<$<TARGET_EXISTS:${JSONCPP_LIBRARY}>:${JSONCPP_LIBRARY}>
        $<$<TARGET_EXISTS:JsonCpp::JsonCpp>:JsonCpp::JsonCpp>
        $<$<BOOL:${OPENSSL_FOUND}>:OpenSSL::SSL>
        $<$<BOOL:${OPENSSL_FOUND}>:OpenSSL::Crypto>
        $<$<BOOL:${Sodium_FOUND}>:Sodium::Sodium>
        $<$<PLATFORM_ID:Windows>:ws2_32>
        $<$<PLATFORM_ID:Windows>:crypt32>
        $<$<PLATFORM_ID:Windows>:secur32>
        $<$<PLATFORM_ID:Windows>:bcrypt>
        $<$<PLATFORM_ID:Windows>:iphlpapi>
        $<$<AND:$<NOT:$<PLATFORM_ID:Windows>>,$<NOT:$<PLATFORM_ID:Darwin>>>:dl>
    PRIVATE
        $<$<BOOL:${ENABLE_GRPC}>:grpc++_unsecure>
        $<$<BOOL:${ENABLE_GRPC}>:grpc Reflection>
        $<$<BOOL:${ENABLE_GRPC}>:gpr>
        $<$<BOOL:${ENABLE_GRPC}>:protobuf::libprotobuf>
)

if(APPLE AND OPENSSL_FOUND)
    target_link_libraries(midnight-core
        PUBLIC
            "-framework CoreFoundation"
            "-framework Security"
    )
endif()

target_compile_definitions(midnight-core
    PRIVATE
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
        $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>
        $<$<PLATFORM_ID:Windows>:WINVER=0x0A00>
        $<$<BOOL:${OPENSSL_FOUND}>:CPPHTTPLIB_OPENSSL_SUPPORT>
        $<$<BOOL:${OPENSSL_FOUND}>:MIDNIGHT_ENABLE_OPENSSL_CRYPTO=1>
        $<$<BOOL:${Sodium_FOUND}>:MIDNIGHT_ENABLE_SODIUM=1>
)

# Set warning levels
if(MSVC)
    target_compile_options(midnight-core PRIVATE /W4)
else()
    target_compile_options(midnight-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Set C++ standard
target_compile_features(midnight-core PUBLIC cxx_std_20)
