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