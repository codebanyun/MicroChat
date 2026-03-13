cmake_minimum_required(VERSION 3.10)
project(message_server)

set(target "message_server")

set(proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../proto)
set(proto_files user.proto base.proto file.proto message.proto)
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

set(odb_path ${CMAKE_CURRENT_SOURCE_DIR}/../odb)
set(odb_files message.hxx message_outbox.hxx)
set(odb_hxx "")
set(odb_cxx "")
set(odb_srcs "")
foreach(odb_file ${odb_files})
    string(REPLACE ".hxx" "-odb.hxx" odb_hxx_file ${odb_file})
    string(REPLACE ".hxx" "-odb.cxx" odb_cxx_file ${odb_file})
    
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${odb_cxx_file} ${CMAKE_CURRENT_BINARY_DIR}/${odb_hxx_file}
        COMMAND odb -d mysql --std c++11 --generate-query --generate-schema --profile boost/date-time ${odb_path}/${odb_file}
        DEPENDS ${odb_path}/${odb_file}
        COMMENT "生成ODB框架代码文件: ${odb_file}"
    )
    list(APPEND odb_srcs ${CMAKE_CURRENT_BINARY_DIR}/${odb_cxx_file})
endforeach()

add_executable(${target}
    message_server.cc
    ${proto_srcs}
    ${odb_srcs}
)

set(test_client "message_client")
add_executable(${test_client} 
               ${CMAKE_CURRENT_SOURCE_DIR}/test/message_client.cc 
               ${proto_srcs})

target_include_directories(${target} PRIVATE /opt/openssl-1.1/include 
                                            /usr/local/brpc-ssl1.1/include)
target_include_directories(${test_client} PRIVATE /opt/openssl-1.1/include 
                                            /usr/local/brpc-ssl1.1/include)

target_link_libraries(${target} -lgflags 
    -lspdlog -lfmt 
    /usr/local/brpc-ssl1.1/lib/libbrpc.so 
    /opt/openssl-1.1/lib/libssl.so 
    /opt/openssl-1.1/lib/libcrypto.so 
    -lprotobuf -lleveldb -letcd-cpp-api 
    -lcpprest -lcurl -lodb-mysql -lodb -lodb-boost
    /usr/lib/x86_64-linux-gnu/libjsoncpp.so.19
    -lcpr -lelasticlient
    -lamqpcpp -lev)

target_link_libraries(${test_client} 
                    -pthread -lgtest -lgflags -lspdlog -lfmt 
                    /usr/local/brpc-ssl1.1/lib/libbrpc.so 
                    /opt/openssl-1.1/lib/libssl.so 
                    /opt/openssl-1.1/lib/libcrypto.so 
                    -lprotobuf -lleveldb -letcd-cpp-api -lcpprest -lcurl /usr/lib/x86_64-linux-gnu/libjsoncpp.so.19)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../odb)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party)