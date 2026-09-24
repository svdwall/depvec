cmake_minimum_required(VERSION 3.20)
project(DepVecPass LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_llvm_pass_plugin(DepVecPass
  src/DepVecPass.cpp
  src/InstructionGraphAnalysis.cpp
)
foreach(LLVM_INCLUDE_DIR IN LISTS LLVM_INCLUDE_DIRS)
  target_compile_options(DepVecPass PRIVATE "-isystem${LLVM_INCLUDE_DIR}")
endforeach()
install(TARGETS DepVecPass LIBRARY DESTINATION lib)
