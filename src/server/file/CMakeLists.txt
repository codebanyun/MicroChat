cmake_minimum_required(VERSION 3.10)
project(file_server)

set(target "file_server")

set(proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../proto)
set(proto_files file.proto base.proto)
set(proto_hxx "")
set(proto_cxx "")
set(proto_srcs "")
foreach(proto_file ${proto_files})
    string(REPLACE ".proto" ".pb.cc" proto_cc ${proto_file})
    string(REPLACE ".proto" ".pb.h" proto_hh  ${proto_file})
    if (NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${proto_cc})
        add_custom_command(
            PRE_BUILD
            COMMAND protoc
            ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR} -I ${proto_path}  ${proto_path}/${proto_file}
            DEPENDS ${proto_path}/${proto_file}
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${proto_cc}
            COMMENT "生成Protobuf框架代码文件:" ${CMAKE_CURRENT_BINARY_DIR}/${proto_cc}
        )
    endif()
    list(APPEND proto_srcs ${CMAKE_CURRENT_BINARY_DIR}/${proto_cc})
endforeach()

add_executable(${target}
    file_server.cc
    ${proto_srcs}
)
target_include_directories(${target} PRIVATE /opt/openssl-1.1/include 
                                            /usr/local/brpc-ssl1.1/include)
# 链接库
target_link_libraries(${target} PRIVATE
    gtest
    gflags
    spdlog
    fmt
    /usr/local/brpc-ssl1.1/lib/libbrpc.so
    /opt/openssl-1.1/lib/libssl.so
    /opt/openssl-1.1/lib/libcrypto.so
    protobuf
    leveldb
    etcd-cpp-api
    cpprest
    curl
    /usr/lib/x86_64-linux-gnu/libjsoncpp.so.19
)

set_target_properties(${target} PROPERTIES
    BUILD_RPATH "/opt/openssl-1.1/lib:/usr/local/brpc-ssl1.1/lib"
    INSTALL_RPATH "/opt/openssl-1.1/lib:/usr/local/brpc-ssl1.1/lib"
)

# test client
add_executable(file_client
    ${CMAKE_CURRENT_SOURCE_DIR}/test/file_client.cc
    ${proto_srcs}
)
target_include_directories(file_client PRIVATE /opt/openssl-1.1/include 
                                            /usr/local/brpc-ssl1.1/include)
target_link_libraries(file_client PRIVATE
    gflags
    gtest
    spdlog
    fmt
    /usr/local/brpc-ssl1.1/lib/libbrpc.so
    /opt/openssl-1.1/lib/libssl.so
    /opt/openssl-1.1/lib/libcrypto.so
    protobuf
    leveldb
    etcd-cpp-api
    cpprest
    curl
    /usr/lib/x86_64-linux-gnu/libjsoncpp.so.19
)   

set_target_properties(file_client PROPERTIES
    BUILD_RPATH "/opt/openssl-1.1/lib:/usr/local/brpc-ssl1.1/lib"
    INSTALL_RPATH "/opt/openssl-1.1/lib:/usr/local/brpc-ssl1.1/lib"
)
# 包含目录
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party)