cmake_minimum_required(VERSION 3.10)
project(gateway_server)

set(target "gateway_server")

set(proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../proto)
set(proto_files user.proto base.proto file.proto message.proto friend.proto gateway.proto notify.proto speech.proto forward.proto)
set(proto_hxx "")
set(proto_cxx "")
set(proto_srcs "")
foreach(proto_file ${proto_files})
    string(REPLACE ".proto" ".pb.cc" proto_cc ${proto_file})
    string(REPLACE ".proto" ".pb.h" proto_hh  ${proto_file})
    
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${proto_cc} ${CMAKE_CURRENT_BINARY_DIR}/${proto_hh}
            COMMAND protoc --cpp_out=${CMAKE_CURRENT_BINARY_DIR} -I ${proto_path} ${proto_path}/${proto_file}
            DEPENDS ${proto_path}/${proto_file}
            COMMENT "生成Protobuf框架代码文件: ${proto_file}"
        )
    list(APPEND proto_srcs ${CMAKE_CURRENT_BINARY_DIR}/${proto_cc})
endforeach()

add_executable(${target}
    gateway_server.cc
    ${proto_srcs}
)

target_include_directories(${target} PRIVATE /opt/openssl-1.1/include 
                                            /usr/local/brpc-ssl1.1/include)

target_link_libraries(${target} -lgflags 
    -lspdlog -lfmt 
    /usr/local/brpc-ssl1.1/lib/libbrpc.so 
    /opt/openssl-1.1/lib/libssl.so 
    /opt/openssl-1.1/lib/libcrypto.so 
    -lprotobuf -lleveldb -letcd-cpp-api 
    -lcpprest -lcurl 
    /usr/lib/x86_64-linux-gnu/libjsoncpp.so.19
    -lcpr -lelasticlient
    -lredis++ -lhiredis)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party)