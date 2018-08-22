add_executable(server_01_zly send_server.cpp)
add_executable(client_01_zly send_client.cpp)
target_link_libraries(server_01_zly Threads::Threads)
target_link_libraries(client_01_zly Threads::Threads)
