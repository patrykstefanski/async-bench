cmake_minimum_required(VERSION 3.8)
project(libuv-bench LANGUAGES C)

find_package(Threads REQUIRED)

add_subdirectory(libuv)
include_directories(libuv/include)

add_executable(hello hello.c)
target_link_libraries(hello PRIVATE uv_a ${CMAKE_THREAD_LIBS_INIT})
set_property(TARGET hello PROPERTY C_STANDARD 11)
if(CMAKE_C_COMPILER_ID MATCHES Clang OR CMAKE_COMPILER_IS_GNUCC)
  target_compile_options(hello PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Wnull-dereference -Wvla)
endif()
