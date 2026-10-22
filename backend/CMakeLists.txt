cmake_minimum_required(VERSION 3.20)
project(DepVecBackend LANGUAGES CXX)

find_package(antlr4-runtime CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_program(ANTLR4_EXECUTABLE NAMES antlr4 REQUIRED)

set(ANTLR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(ANTLR_GENERATED_SOURCES
  "${ANTLR_GENERATED_DIR}/DepVecAssertionLexer.cpp"
  "${ANTLR_GENERATED_DIR}/DepVecAssertionParser.cpp"
  "${ANTLR_GENERATED_DIR}/DepVecAssertionVisitor.cpp"
  "${ANTLR_GENERATED_DIR}/DepVecAssertionBaseVisitor.cpp"
)
set(ANTLR_GENERATED_HEADERS
  "${ANTLR_GENERATED_DIR}/DepVecAssertionLexer.h"
  "${ANTLR_GENERATED_DIR}/DepVecAssertionParser.h"
  "${ANTLR_GENERATED_DIR}/DepVecAssertionVisitor.h"
  "${ANTLR_GENERATED_DIR}/DepVecAssertionBaseVisitor.h"
)

set(BACKEND_SOURCES
  AssertionParser.cpp
  AssertionAstBuilder.cpp
  GraphJsonReader.cpp
)

add_custom_command(
  OUTPUT ${ANTLR_GENERATED_SOURCES} ${ANTLR_GENERATED_HEADERS}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${ANTLR_GENERATED_DIR}"
  COMMAND "${ANTLR4_EXECUTABLE}" -Dlanguage=Cpp -visitor -no-listener
          -o "${ANTLR_GENERATED_DIR}" DepVecAssertion.g4
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/DepVecAssertion.g4"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  VERBATIM
)

add_library(DepVecBackend STATIC
  ${ANTLR_GENERATED_SOURCES}
  ${BACKEND_SOURCES}
)
target_compile_features(DepVecBackend PUBLIC cxx_std_17)
target_include_directories(DepVecBackend
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${ANTLR_GENERATED_DIR}"
)
target_link_libraries(DepVecBackend
  PUBLIC antlr4_shared nlohmann_json::nlohmann_json
)

add_executable(depvec-assertion-parse parse_assertion.cpp)
target_link_libraries(depvec-assertion-parse PRIVATE DepVecBackend)

add_executable(depvec-graph-parse parse_graph.cpp)
target_link_libraries(depvec-graph-parse PRIVATE DepVecBackend)
