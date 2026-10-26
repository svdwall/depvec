add_executable(DepVecBackendTests
  assertion_parser_test.cpp
  solver_test.cpp
)
target_include_directories(DepVecBackendTests
  PRIVATE "${PROJECT_SOURCE_DIR}/solver"
)
target_link_libraries(DepVecBackendTests
  PRIVATE DepVecBackend Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(DepVecBackendTests)
