add_executable(DepVecExtractorTests
  interference_test.cpp
  UnrollingOracle.cpp
  "${PROJECT_SOURCE_DIR}/src/extractor/InterferenceModel.cpp"
)
target_include_directories(DepVecExtractorTests
  PRIVATE "${PROJECT_SOURCE_DIR}/src/extractor"
)
target_link_libraries(DepVecExtractorTests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(DepVecExtractorTests)
