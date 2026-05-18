add_executable(mqtt_example mqtt_example.cpp)
target_link_libraries(mqtt_example PRIVATE midnight-core)
target_include_directories(mqtt_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(http_example http_example.cpp)
target_link_libraries(http_example PRIVATE midnight-core)
target_include_directories(http_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(http_connectivity_test http_connectivity_test.cpp)
target_link_libraries(http_connectivity_test PRIVATE midnight-core)
target_include_directories(http_connectivity_test PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(wallet-generator-new wallet_generator_new.cpp)
target_link_libraries(wallet-generator-new PRIVATE midnight-core)
target_include_directories(wallet-generator-new PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(TARGET midnight-ledger-ffi)
    add_dependencies(wallet-generator-new midnight-ledger-ffi)
endif()
if(DEFINED MIDNIGHT_LEDGER_FFI_LIBRARY)
    target_compile_definitions(wallet-generator-new
        PRIVATE
            MIDNIGHT_EXAMPLE_LEDGER_FFI_LIBRARY="${MIDNIGHT_LEDGER_FFI_LIBRARY}"
    )
endif()

add_executable(fast_balance_checker fast_balance_checker.cpp)
target_link_libraries(fast_balance_checker PRIVATE midnight-core)
target_include_directories(fast_balance_checker PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(balance_checker balance_checker.cpp)
target_link_libraries(balance_checker PRIVATE midnight-core)
target_include_directories(balance_checker PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(preprod_midnight_format_check preprod_midnight_format_check.cpp)
target_link_libraries(preprod_midnight_format_check PRIVATE midnight-core)
target_include_directories(preprod_midnight_format_check PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(production_transactions_example production_transactions_example.cpp)
target_link_libraries(production_transactions_example PRIVATE midnight-core)
target_include_directories(production_transactions_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(preprod_live_flow preprod_live_flow.cpp)
target_link_libraries(preprod_live_flow PRIVATE midnight-core)
target_include_directories(preprod_live_flow PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(decode_address decode_address.cpp)
target_link_libraries(decode_address PRIVATE midnight-core)
target_include_directories(decode_address PRIVATE ${CMAKE_SOURCE_DIR}/include)
