include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/exchange)

set(TEST_LIBS libexchange libcommon pthread)

add_executable(matching_engine_test matching_engine_test.cpp)
target_link_libraries(matching_engine_test PUBLIC ${TEST_LIBS})
add_test(NAME matching_engine_test COMMAND matching_engine_test)

add_executable(tcp_server_test tcp_server_test.cpp)
target_link_libraries(tcp_server_test PUBLIC ${TEST_LIBS})
add_test(NAME tcp_server_test COMMAND tcp_server_test)
