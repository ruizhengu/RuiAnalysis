cmake_minimum_required(VERSION 4.0)
project(RuiAnalysis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM Config: ${LLVM_DIR}")
message(STATUS "Using LLVM libraries: ${LLVM_LIBRARY_DIRS}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
message(STATUS "CLANG_INCLUDE_DIRS: ${CLANG_INCLUDE_DIRS}")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(SYSTEM ${CLANG_INCLUDE_DIRS})

add_definitions(${LLVM_DEFINITIONS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXX_FLAGS}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
enable_testing()

add_executable(RuiAnalysis src/main.cpp)

target_include_directories(RuiAnalysis PRIVATE
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
)

target_link_libraries(RuiAnalysis
        PRIVATE
        clangTooling
        clangToolingCore
        clangFrontend
        clangDriver
        clangSerialization
        clangParse
        clangSema
        clangAnalysis
        clangAST
        clangASTMatchers
        clangEdit
        clangLex
        clangBasic
)

#add_executable(dummy_for_test examples/test.cpp)

#target_include_directories(dummy_for_test PRIVATE
#        ${LLVM_INCLUDE_DIRS}
#        ${CLANG_INCLUDE_DIRS}
#)

#if(APPLE)
#    # Detect the SDK path (you may adjust this to your local setup)
#    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE MACOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
#    message(STATUS "macOS SDK path: ${MACOS_SDK_PATH}")
#
#    target_compile_options(RuiAnalysis PRIVATE
#            "-stdlib=libc++"
#            "-isysroot" "${MACOS_SDK_PATH}"
#    )
#    # Also for dummy_for_test, the same
#    target_compile_options(dummy_for_test PRIVATE
#            "-stdlib=libc++"
#            "-isysroot" "${MACOS_SDK_PATH}"
#    )
#endif()

execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)