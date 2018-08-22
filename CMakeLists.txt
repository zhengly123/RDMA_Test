cmake_minimum_required(VERSION 3.5)
project(RDMA_Test)

set(CMAKE_CXX_STANDARD 11)
link_libraries(rdmacm ibverbs)
find_package(Threads REQUIRED)

#add_subdirectory(server)
#add_subdirectory(client)
#
#add_subdirectory(01)
#
#add_subdirectory(rdma_cm)
#
#add_subdirectory(01-zly)
add_subdirectory(Send)

add_subdirectory(Write)
