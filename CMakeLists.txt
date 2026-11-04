cmake_minimum_required(VERSION 3.20)
project(pgt VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} (${LLVM_DIR})")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(pgt
    src/main.cpp
    src/lexer/Lexer.cpp
    src/parser/Parser.cpp
    src/codegen/CodeGen.cpp
    src/codegen/ObjectEmitter.cpp
    src/driver/Driver.cpp
)
target_include_directories(pgt PRIVATE src)
target_compile_definitions(pgt PRIVATE PGT_VERSION="${PROJECT_VERSION}")
target_compile_options(pgt PRIVATE -Wall -Wextra)
target_link_libraries(pgt PRIVATE LLVM)
