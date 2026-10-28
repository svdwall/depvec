#include "depvec/graph_json_reader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string graphWithInterferences(const std::string &interferences) {
  return R"({
    "format": "depvec-graph-v2",
    "precondition": "",
    "postcondition": "",
    "globalInitialValues": {},
    "module": "test.ll",
    "functions": [{
      "name": "f",
      "ir": "define void @f() {\nentry:\n  ret void\n}",
      "nodes": [
        { "id": 0, "kind": "init", "line": 0, "instruction": "" },
        { "id": 1, "kind": "exit", "line": 2, "instruction": "ret void" }
      ],
      "edges": [{
        "id": "e0", "sources": [0], "target": 1, "kind": "data",
        "values": [], "assertion": "", "interferences": )" +
         interferences + R"(
      }]
    }]
  })";
}

} // namespace

TEST_CASE("graph reader parses edge interferences") {
  const depvec::DFG graph = depvec::readGraphJson(graphWithInterferences("[1, 0]"));
  REQUIRE(graph.functions.at(0).edges.at(0).interferences ==
          std::vector<depvec::NodeId>{1, 0});
}

TEST_CASE("graph reader rejects interferences on unknown nodes") {
  REQUIRE_THROWS_AS(depvec::readGraphJson(graphWithInterferences("[2]")),
                    std::runtime_error);
}

TEST_CASE("graph reader requires edge interferences") {
  std::string graph = graphWithInterferences("[]");
  graph.replace(graph.find(R"(, "interferences": [])"),
                std::string(R"(, "interferences": [])").size(), "");
  REQUIRE_THROWS_AS(depvec::readGraphJson(graph), std::runtime_error);
}
