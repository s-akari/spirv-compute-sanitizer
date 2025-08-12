cmake_minimum_required(VERSION 3.13.5)

project(spirv-compute-sanitizer)

find_package(LLVM PATHS "${LLVM_DIR}" REQUIRED CONFIG NO_DEFAULT_PATH)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
link_directories(${LLVM_LIBRARY_DIRS})

message(STATUS "LLVM includes: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM definitions: ${LLVM_DEFINITIONS_LIST}")
message(STATUS "LLVM libraries: ${LLVM_LIBRARY_DIRS}")

message(STATUS "LLVM Config: ASSERTIONS:${LLVM_ENABLE_ASSERTIONS}, "
        "EH:${LLVM_ENABLE_EH}, RTTI:${LLVM_ENABLE_RTTI}")

# Depends on how clang was compiled, by default it's set to C++14
# https://llvm.org/docs/CMake.html#rarely-used-cmake-variables
if (LLVM_PACKAGE_VERSION VERSION_LESS "16.0.0")
  set(CMAKE_CXX_STANDARD 14)
else ()
  set(CMAKE_CXX_STANDARD 17)
endif ()

if (LLVM_ENABLE_LIBCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif ()

if (NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer -g")

add_library(SPIRVComputeSanitizer SHARED Plugin.cc)

target_include_directories(SPIRVComputeSanitizer PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(SPIRVComputeSanitizer PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_sources(SPIRVComputeSanitizer PUBLIC SPIRVComputeSanitizer.cc)

