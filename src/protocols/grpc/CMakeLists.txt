# gRPC Protocol Buffers
find_package(Protobuf REQUIRED)

# Proto file
set(GRPC_PROTO "${CMAKE_SOURCE_DIR}/proto/midnight.proto")
set(GRPC_PB_SRC "${CMAKE_CURRENT_BINARY_DIR}/midnight.pb.cc")
set(GRPC_PB_HDR "${CMAKE_CURRENT_BINARY_DIR}/midnight.pb.h")
set(GRPC_GRPC_SRC "${CMAKE_CURRENT_BINARY_DIR}/midnight.grpc.pb.cc")
set(GRPC_GRPC_HDR "${CMAKE_CURRENT_BINARY_DIR}/midnight.grpc.pb.h")

add_custom_command(
    OUTPUT ${GRPC_PB_SRC} ${GRPC_PB_HDR} ${GRPC_GRPC_SRC} ${GRPC_GRPC_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --proto_path=${CMAKE_SOURCE_DIR}/proto
        --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
        --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
        --plugin=protoc-gen-grpc=${gRPC_GRPCPP_PLUGIN}
        ${GRPC_PROTO}
    DEPENDS ${GRPC_PROTO} ${Protobuf_PROTOC_EXECUTABLE} ${gRPC_GRPCPP_PLUGIN}
    COMMENT "Generating gRPC protobuf files from midnight.proto"
    VERBATIM
)

add_library(midnight_grpc_proto
    ${GRPC_PB_SRC}
    ${GRPC_GRPC_SRC}
)
target_include_directories(midnight_grpc_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(midnight_grpc_proto
    PUBLIC
        protobuf::libprotobuf
        grpc++_unsecure
        grpc Reflection
        gpr
)
target_compile_features(midnight_grpc_proto PUBLIC cxx_std_20)
