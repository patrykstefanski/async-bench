cmake_minimum_required(VERSION 3.8)
project(fev-bench LANGUAGES C CXX)

add_subdirectory(libfev)

add_executable(hello hello.c)

add_executable(hello-timeout hello.c)
target_compile_definitions(hello-timeout PRIVATE -DWITH_TIMEOUT)

add_executable(hello++ hello++.cpp)

add_executable(hello-timeout++ hello++.cpp)
target_compile_definitions(hello-timeout++ PRIVATE -DWITH_TIMEOUT)

foreach(target hello hello-timeout hello++ hello-timeout++)
  target_link_libraries(${target} PRIVATE fev)
endforeach()

foreach(target hello hello-timeout)
  set_property(TARGET ${target} PROPERTY C_STANDARD 11)
  if(CMAKE_C_COMPILER_ID MATCHES Clang OR CMAKE_COMPILER_IS_GNUCC)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Wnull-dereference -Wvla)
  endif()
endforeach()

foreach(target hello++ hello-timeout++)
  set_property(TARGET ${target} PROPERTY CXX_STANDARD 17)
  if(CMAKE_CXX_COMPILER_ID MATCHES Clang OR CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Wnull-dereference -Wvla)
  endif()
endforeach()
