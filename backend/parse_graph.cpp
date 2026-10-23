#include "depvec/graph_json_reader.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: depvec-graph-parse <graph.json>\n";
    return 2;
  }

  try {
    const depvec::DFG dfg = depvec::readGraphFile(argv[1]);
    std::size_t nodeCount = 0;
    std::size_t edgeCount = 0;
    for (const depvec::FunctionDFG &function : dfg.functions) {
      nodeCount += function.nodes.size();
      edgeCount += function.edges.size();
    }
    std::cout << "Parsed " << dfg.functions.size() << " functions, "
              << nodeCount << " nodes, and " << edgeCount << " edges from "
              << argv[1] << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
