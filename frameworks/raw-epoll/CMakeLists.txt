cmake_minimum_required(VERSION 3.8)
project(raw-epoll-bench LANGUAGES C)

find_package(Threads REQUIRED)

add_executable(hello hello.c)
target_link_libraries(hello ${CMAKE_THREAD_LIBS_INIT})
set_property(TARGET hello PROPERTY C_STANDARD 11)
target_compile_definitions(hello PRIVATE -D_GNU_SOURCE)
if(CMAKE_C_COMPILER_ID MATCHES Clang OR CMAKE_COMPILER_IS_GNUCC)
  target_compile_options(hello PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Wnull-dereference -Wvla)
endif()
