add_executable(mqtt_example mqtt_example.cpp)
target_link_libraries(mqtt_example PRIVATE midnight-core)
target_include_directories(mqtt_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(http_example http_example.cpp)
target_link_libraries(http_example PRIVATE midnight-core)
target_include_directories(http_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(http_connectivity_test http_connectivity_test.cpp)
target_link_libraries(http_connectivity_test PRIVATE midnight-core)
target_include_directories(http_connectivity_test PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(phase4a_zk_types_example phase4a_zk_types_example.cpp)
target_link_libraries(phase4a_zk_types_example PRIVATE midnight-core)
target_include_directories(phase4a_zk_types_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(phase4b_private_state_example phase4b_private_state_example.cpp)
target_link_libraries(phase4b_private_state_example PRIVATE midnight-core)
target_include_directories(phase4b_private_state_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(phase4c_proof_server_resilience_example phase4c_proof_server_resilience_example.cpp)
target_link_libraries(phase4c_proof_server_resilience_example PRIVATE midnight-core)
target_include_directories(phase4c_proof_server_resilience_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(phase4d_ledger_state_sync_example phase4d_ledger_state_sync_example.cpp)
target_link_libraries(phase4d_ledger_state_sync_example PRIVATE midnight-core)
target_include_directories(phase4d_ledger_state_sync_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(wallet-generator wallet-generator.cpp)
target_link_libraries(wallet-generator PRIVATE midnight-core)
target_include_directories(wallet-generator PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(native_contract_interactor_example native_contract_interactor_example.cpp)
target_link_libraries(native_contract_interactor_example PRIVATE midnight-core)
target_include_directories(native_contract_interactor_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(e2e_transfer_night e2e_transfer_night.cpp)
target_link_libraries(e2e_transfer_night PRIVATE midnight-core)
target_include_directories(e2e_transfer_night PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(midnight-wallet-tool midnight-wallet-tool.cpp)
target_link_libraries(midnight-wallet-tool PRIVATE midnight-core)
target_include_directories(midnight-wallet-tool PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(indexer-probe indexer-probe.cpp)
target_link_libraries(indexer-probe PRIVATE midnight-core)
target_include_directories(indexer-probe PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${HTTPLIB_SOURCE_DIR}
)
if(WIN32)
    target_compile_definitions(indexer-probe PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00
    )
endif()
target_compile_definitions(indexer-probe PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

add_executable(ws-probe ws-probe.cpp)
target_link_libraries(ws-probe PRIVATE midnight-core)
target_include_directories(ws-probe PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${HTTPLIB_SOURCE_DIR}
)
if(WIN32)
    target_compile_definitions(ws-probe PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00
    )
endif()
target_compile_definitions(ws-probe PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
