add_executable(server_02_zly 02_zly_server.cpp common.cpp common.h)
add_executable(client_02_zly 02_zly_client.cpp common.cpp common.h)
target_link_libraries(server_02_zly Threads::Threads)
target_link_libraries(client_02_zly Threads::Threads)
