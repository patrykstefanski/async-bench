cmake_minimum_required(VERSION 3.8)
project(async-bench-tools LANGUAGES C)

option(ENABLE_STATIC_ANALYSIS "Enable static analysis" OFF)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_MEMSAN "Enable Memory Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)

find_package(Threads REQUIRED)

function(set_compile_options target)
  if(NOT (CMAKE_C_COMPILER_ID MATCHES Clang OR CMAKE_COMPILER_IS_GNUCC))
    return()
  endif()

  target_compile_options(${target} PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Wnull-dereference -Wvla)
  target_compile_definitions(${target} PRIVATE -D_GNU_SOURCE)

  if(ENABLE_STATIC_ANALYSIS)
    if(CMAKE_COMPILER_IS_GNUCC)
      target_compile_options(${target} PRIVATE --analyzer)
    elseif(CMAKE_C_COMPILER_ID MATCHES Clang)
      target_compile_options(${target} PRIVATE -fanalyzer)
    endif()
  endif()

  if(ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address)
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -fsanitize=address")
  endif()

  if(ENABLE_MEMSAN)
    target_compile_options(${target} PRIVATE -fsanitize=memory)
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -fsanitize=memory")
  endif()

  if(ENABLE_UBSAN)
    target_compile_options(${target} PRIVATE -fsanitize=undefined)
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -fsanitize=undefined")
  endif()
endfunction()

foreach(tool bench-latency bench-throughput)
  add_executable(${tool} ${tool}.c)
  target_link_libraries(${tool} PRIVATE ${CMAKE_THREAD_LIBS_INIT})
  set_property(TARGET ${tool} PROPERTY C_STANDARD 11)
  set_compile_options(${tool})
endforeach()
