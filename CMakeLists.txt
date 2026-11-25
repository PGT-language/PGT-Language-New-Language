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

add_library(pgtrt STATIC runtime/pgt_runtime.c)
set_target_properties(pgtrt PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
target_compile_options(pgtrt PRIVATE -O2 -Wall -Wextra)

add_executable(pgt
    src/main.cpp
    src/common/Paths.cpp
    src/lexer/Lexer.cpp
    src/parser/Parser.cpp
    src/modfile/ModFile.cpp
    src/loader/Loader.cpp
    src/scaffold/ProjectGenerator.cpp
    src/scaffold/Templates.cpp
    src/codegen/Types.cpp
    src/codegen/CodeGen.cpp
    src/codegen/CodeGenStmt.cpp
    src/codegen/CodeGenInline.cpp
    src/codegen/CodeGenExpr.cpp
    src/codegen/CodeGenBuiltins.cpp
    src/codegen/CodeGenMemory.cpp
    src/codegen/CodeGenMaps.cpp
    src/codegen/CodeGenMulti.cpp
    src/codegen/ObjectEmitter.cpp
    src/driver/Driver.cpp
    src/driver/BuildCache.cpp
    src/debug/Dump.cpp
    src/lsp/Json.cpp
    src/lsp/Index.cpp
    src/lsp/Server.cpp
)
target_include_directories(pgt PRIVATE src)
target_compile_definitions(pgt PRIVATE
    PGT_VERSION="${PROJECT_VERSION}"
    PGT_STD_DIR="${CMAKE_SOURCE_DIR}/std"
    PGT_RUNTIME_LIB="$<TARGET_FILE:pgtrt>")
add_dependencies(pgt pgtrt)

install(TARGETS pgt RUNTIME DESTINATION bin)
install(TARGETS pgtrt ARCHIVE DESTINATION lib/pgt)
install(DIRECTORY std/ DESTINATION lib/pgt/std)

enable_testing()
add_test(NAME pgt-tests
    COMMAND ${CMAKE_SOURCE_DIR}/tests/run.sh $<TARGET_FILE:pgt>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests)
target_compile_options(pgt PRIVATE -Wall -Wextra)
target_link_libraries(pgt PRIVATE LLVM)
