cmake_minimum_required(VERSION 3.8)
project(threads-bench LANGUAGES C)

find_package(Threads REQUIRED)

add_executable(hello hello.c)

add_executable(hello-timeout hello.c)
target_compile_definitions(hello-timeout PRIVATE -DWITH_TIMEOUT)

foreach(target hello hello-timeout)
  target_link_libraries(${target} ${CMAKE_THREAD_LIBS_INIT})
  set_property(TARGET ${target} PROPERTY C_STANDARD 11)
  if(CMAKE_C_COMPILER_ID MATCHES Clang OR CMAKE_COMPILER_IS_GNUCC)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Wnull-dereference -Wvla)
  endif()
endforeach()
