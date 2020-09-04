cmake_minimum_required(VERSION 3.8)
project(asio-bench LANGUAGES CXX)

find_package(Threads REQUIRED)

find_package(Boost REQUIRED COMPONENTS system)
include_directories(${Boost_INCLUDE_DIRS})

foreach(target hello hello-timeout hello-prefork hello-timeout-prefork)
  add_executable(${target} ${target}.cpp)
  target_link_libraries(${target} ${Boost_SYSTEM_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})
  set_property(TARGET ${target} PROPERTY CXX_STANDARD 17)
  if(CMAKE_CXX_COMPILER_ID MATCHES Clang OR CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Wnull-dereference -Wvla)
  endif()
endforeach()
