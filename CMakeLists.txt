cmake_minimum_required(VERSION 3.20)
project(DepVecPass LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_llvm_pass_plugin(DepVecPass
  src/extractor/DepVecPass.cpp
  src/extractor/GraphSerialization.cpp
  src/extractor/DependencyGraphAnalysis.cpp
  src/extractor/ControlDependencyAnalysis.cpp
  src/extractor/DataDependencyAnalysis.cpp
  src/extractor/MemoryOrderAnalysis.cpp
  src/extractor/InterferenceAnalysis.cpp
  src/extractor/InterferenceModel.cpp
)
foreach(LLVM_INCLUDE_DIR IN LISTS LLVM_INCLUDE_DIRS)
  target_compile_options(DepVecPass PRIVATE "-isystem${LLVM_INCLUDE_DIR}")
endforeach()

option(DEPVEC_BUILD_EXTRACTOR_TESTS "Build the extractor unit tests" OFF)
if(DEPVEC_BUILD_EXTRACTOR_TESTS)
  enable_testing()
  find_package(Catch2 3 CONFIG REQUIRED)
  add_subdirectory(test/extractor)
endif()

install(TARGETS DepVecPass LIBRARY DESTINATION lib)
