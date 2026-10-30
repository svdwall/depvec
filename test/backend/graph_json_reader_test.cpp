#include "depvec/graph_json_reader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <variant>
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

namespace {

std::string graphWithCompareAssertion(const std::string &assertion) {
  return R"({
    "format": "depvec-graph-v2",
    "precondition": "",
    "postcondition": "",
    "globalInitialValues": {},
    "module": "test.ll",
    "functions": [{
      "name": "f",
      "ir": "define void @f(i32 %x) {\nentry:\n  %cmp = icmp eq i32 %x, 0\n  ret void\n}",
      "nodes": [
        { "id": 0, "kind": "init", "line": 0, "instruction": "" },
        { "id": 1, "kind": "instruction", "line": 2,
          "instruction": "  %cmp = icmp eq i32 %x, 0",
          "operation": { "opcode": "icmp", "predicate": "eq", "result": "cmp",
                         "left": { "kind": "ref", "name": "x" },
                         "right": { "kind": "integer", "value": "0" },
                         "operandType": 32 } },
        { "id": 2, "kind": "exit", "line": 3, "instruction": "ret void" }
      ],
      "edges": [{
        "id": "e0", "sources": [1], "target": 2, "kind": "data",
        "values": [], "assertion": ")" + assertion + R"(", "interferences": []
      }]
    }]
  })";
}

} // namespace

TEST_CASE("graph reader types icmp results as booleans") {
  const depvec::DFG graph =
      depvec::readGraphJson(graphWithCompareAssertion("%cmp = false -> %x != 0"));
  const auto &assertion = *graph.functions.at(0).edges.at(0).assertion;
  const auto *implication = std::get_if<depvec::LogicalFormula>(&assertion.node);
  REQUIRE(implication != nullptr);
  const auto *iff = std::get_if<depvec::LogicalFormula>(&implication->left->node);
  REQUIRE(iff != nullptr);
  REQUIRE(iff->op == depvec::LogicalOperator::Iff);

  REQUIRE_THROWS_AS(depvec::readGraphJson(graphWithCompareAssertion("%x")),
                    std::runtime_error);
  REQUIRE_THROWS_AS(depvec::readGraphJson(graphWithCompareAssertion("%cmp = 0")),
                    std::runtime_error);
}

namespace {

std::string graphWithCompareExchange(const std::string &assertion) {
  return R"({
    "format": "depvec-graph-v2",
    "precondition": "",
    "postcondition": "",
    "globalInitialValues": {},
    "module": "test.ll",
    "functions": [{
      "name": "f",
      "ir": "",
      "nodes": [
        { "id": 0, "kind": "init", "line": 0, "instruction": "" },
        { "id": 1, "kind": "instruction", "line": 1, "instruction": "",
          "operation": { "opcode": "cmpxchg", "result": "r",
                         "pointer": { "global": "g" },
                         "expected": { "kind": "integer", "value": "0" },
                         "replacement": { "kind": "integer", "value": "1" },
                         "type": 32, "successOrdering": "seq_cst",
                         "failureOrdering": "seq_cst", "weak": false } },
        { "id": 2, "kind": "exit", "line": 2, "instruction": "ret void" }
      ],
      "edges": [{
        "id": "e0", "sources": [1], "target": 2, "kind": "data",
        "values": [], "assertion": ")" + assertion + R"(", "interferences": []
      }]
    }]
  })";
}

// %a and %b flow into %x = phi [%a, then], [%b, else]. The phi edge's fields
// after "values" are replaced by phiFields, and the edge to ret by retFields.
std::string graphWithPhi(const std::string &phiFields,
                         const std::string &retFields = R"("assertion": "")") {
  return R"({
    "format": "depvec-graph-v2",
    "precondition": "",
    "postcondition": "",
    "globalInitialValues": {},
    "module": "test.ll",
    "functions": [{
      "name": "f",
      "ir": "",
      "nodes": [
        { "id": 0, "kind": "init", "line": 0, "instruction": "" },
        { "id": 1, "kind": "instruction", "line": 1, "instruction": "",
          "operation": { "opcode": "add", "result": "a", "type": 32,
                         "nsw": false, "nuw": false,
                         "left": { "kind": "ref", "name": "y" },
                         "right": { "kind": "integer", "value": "1" } } },
        { "id": 2, "kind": "instruction", "line": 2, "instruction": "",
          "operation": { "opcode": "add", "result": "b", "type": 32,
                         "nsw": false, "nuw": false,
                         "left": { "kind": "ref", "name": "y" },
                         "right": { "kind": "integer", "value": "2" } } },
        { "id": 3, "kind": "instruction", "line": 3, "instruction": "",
          "operation": { "opcode": "phi", "result": "x", "type": 32,
                         "incoming": [
                           { "value": { "kind": "ref", "name": "a" }, "predecessor": "then" },
                           { "value": { "kind": "ref", "name": "b" }, "predecessor": "else" }
                         ] } },
        { "id": 4, "kind": "exit", "line": 4, "instruction": "ret i32 %x" }
      ],
      "edges": [
        { "id": "e0", "sources": [1, 2], "target": 3, "kind": "data",
          "values": ["%a", "%b"], )" + phiFields + R"(, "interferences": [] },
        { "id": "e1", "sources": [3], "target": 4, "kind": "data",
          "values": ["%x"], )" + retFields + R"(, "interferences": [] }
      ]
    }]
  })";
}

const char *validIncoming = R"("assertion": "", "incomingAssertions": [
  { "predecessor": "then", "source": 1, "assertion": "%a > 0" },
  { "predecessor": "else", "source": 2, "assertion": "%b > 0" }])";

} // namespace

TEST_CASE("graph reader types the members of a cmpxchg result") {
  REQUIRE_NOTHROW(depvec::readGraphJson(
      graphWithCompareExchange("%r[1] -> %r[0] = 0 && @g |-> 1")));
  REQUIRE_THROWS_AS(depvec::readGraphJson(graphWithCompareExchange("%r = 0")),
                    std::runtime_error);
  REQUIRE_THROWS_AS(depvec::readGraphJson(graphWithCompareExchange("%r[1] = 0")),
                    std::runtime_error);
}

TEST_CASE("graph reader gives each phi source its own incoming assertion") {
  const depvec::DFG graph = depvec::readGraphJson(graphWithPhi(validIncoming));
  const depvec::FunctionDFG &function = graph.functions.at(0);
  const auto &incoming = function.edges.at(0).incomingAssertions;
  REQUIRE(incoming.size() == 2);

  // Each source's postcondition ends with its own path's assertion.
  for (std::size_t index = 0; index < 2; ++index) {
    const auto &source = std::get<depvec::InstructionNode>(function.nodes.at(index + 1));
    const auto *conjunction =
        std::get_if<depvec::LogicalFormula>(&source.postcondition->node);
    REQUIRE(conjunction != nullptr);
    REQUIRE(conjunction->right == incoming.at(index).assertion);
  }

  // The phi's weakest precondition accounts for the phi edge.
  const auto &phi = std::get<depvec::InstructionNode>(function.nodes.at(3));
  REQUIRE(std::holds_alternative<depvec::BooleanLiteral>(phi.precondition->node));
}

TEST_CASE("graph reader rejects malformed incoming assertions") {
  // A predecessor is missing.
  REQUIRE_THROWS_AS(depvec::readGraphJson(graphWithPhi(R"("assertion": "", "incomingAssertions": [
      { "predecessor": "then", "source": 1, "assertion": "" }])")),
                    std::runtime_error);
  // The value from then is %a, defined by node 1.
  REQUIRE_THROWS_AS(depvec::readGraphJson(graphWithPhi(R"("assertion": "", "incomingAssertions": [
      { "predecessor": "then", "source": 2, "assertion": "" },
      { "predecessor": "else", "source": 1, "assertion": "" }])")),
                    std::runtime_error);
  // A phi edge keeps no assertion of its own.
  std::string shared = validIncoming;
  shared.replace(shared.find(R"("assertion": "")"), 15, R"("assertion": "%a > 0")");
  REQUIRE_THROWS_AS(depvec::readGraphJson(graphWithPhi(shared)), std::runtime_error);
  // A data edge into a phi needs incoming assertions.
  REQUIRE_THROWS_AS(depvec::readGraphJson(graphWithPhi(R"("assertion": "")")),
                    std::runtime_error);
  // Only a data edge into a phi has them.
  REQUIRE_THROWS_AS(
      depvec::readGraphJson(graphWithPhi(validIncoming, R"("assertion": "", "incomingAssertions": [
          { "predecessor": "then", "source": 3, "assertion": "" }])")),
      std::runtime_error);
}
