#include "depvec/assertion_parser.hpp"
#include "depvec/graph_json_reader.hpp"
#include "depvec/verification_conditions.hpp"
#include "depvec/weakest_precondition.hpp"
#include "smtlib_solver.hpp"
#include "z3_smtlib_engine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <variant>

TEST_CASE("verification conditions record solver results and skipped nodes") {
  depvec::FunctionDFG function;
  function.name = "verification_test";

  depvec::InitNode proved{};
  proved.id = 0;
  proved.precondition = depvec::parseAssertion("true");
  proved.weakest_precondition = depvec::parseAssertion("true");
  proved.weakest_precondition_status = "computed";
  function.nodes.emplace_back(std::move(proved));

  depvec::InitNode counterexample{};
  counterexample.id = 1;
  counterexample.precondition = depvec::parseAssertion("true");
  counterexample.weakest_precondition = depvec::parseAssertion("false");
  counterexample.weakest_precondition_status = "computed";
  function.nodes.emplace_back(std::move(counterexample));

  depvec::ExitNode unavailable{};
  unavailable.id = 2;
  unavailable.precondition = depvec::parseAssertion("true");
  unavailable.weakest_precondition_status = "no weakest precondition";
  function.nodes.emplace_back(std::move(unavailable));

  depvec::Z3SmtLibEngine engine;
  depvec::SmtLibSolver solver(depvec::verificationSymbols(function), engine);
  depvec::checkVerificationConditions(function, solver);

  REQUIRE(std::get<depvec::InitNode>(function.nodes[0]).verification.status ==
          depvec::VerificationStatus::Proved);
  REQUIRE(std::get<depvec::InitNode>(function.nodes[1]).verification.status ==
          depvec::VerificationStatus::Counterexample);
  const auto &skipped = std::get<depvec::ExitNode>(function.nodes[2]);
  REQUIRE(skipped.verification.status == depvec::VerificationStatus::NotChecked);
  REQUIRE(skipped.verification.detail == "no weakest precondition");
}

namespace {

// n1 stores 1 to @g, and n2 loads @h. The edge e0 into n2 carries assertion,
// and n1 interferes with it. The edge e1 into the exit carries exitAssertion.
std::string interferenceGraph(const std::string &assertion,
                              const std::string &exitAssertion = "") {
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
          "operation": { "opcode": "store", "ordering": "relaxed",
                         "pointer": { "global": "g" }, "type": 32,
                         "value": { "kind": "integer", "value": "1" } } },
        { "id": 2, "kind": "instruction", "line": 2, "instruction": "",
          "operation": { "opcode": "load", "ordering": "relaxed",
                         "pointer": { "global": "h" }, "result": "v", "type": 32 } },
        { "id": 3, "kind": "exit", "line": 3, "instruction": "ret i32 %v" }
      ],
      "edges": [
        { "id": "e0", "sources": [0], "target": 2, "kind": "init", "values": [],
          "assertion": ")" + assertion + R"(", "interferences": [1] },
        { "id": "e1", "sources": [2], "target": 3, "kind": "data", "values": ["%v"],
          "assertion": ")" + exitAssertion + R"(", "interferences": [] }
      ]
    }]
  })";
}

depvec::FunctionDFG verify(const std::string &graph) {
  depvec::DFG dfg = depvec::readGraphJson(graph);
  depvec::generateWeakestPreconditions(dfg);
  depvec::FunctionDFG function = std::move(dfg.functions.at(0));
  depvec::Z3SmtLibEngine engine;
  depvec::SmtLibSolver solver(depvec::verificationSymbols(function), engine);
  depvec::checkVerificationConditions(function, solver);
  return function;
}

} // namespace

TEST_CASE("interference checks whether an interfering node keeps the assertion") {
  // The store to @g leaves @h alone but breaks @g |-> 0.
  const auto kept = verify(interferenceGraph("@h |-> 0"));
  REQUIRE(kept.edges.at(0).interferenceChecks.size() == 1);
  REQUIRE(kept.edges.at(0).interferenceChecks.at(0).node == 1);
  REQUIRE(kept.edges.at(0).interferenceChecks.at(0).verification.status ==
          depvec::VerificationStatus::Proved);

  const auto broken = verify(interferenceGraph("@g |-> 0"));
  REQUIRE(broken.edges.at(0).interferenceChecks.at(0).verification.status ==
          depvec::VerificationStatus::Counterexample);
}

TEST_CASE("interference checks wait until every node is proved") {
  // The load cannot establish %v = 1, so n2's verification condition fails.
  const auto function = verify(interferenceGraph("@g |-> 0", "%v = 1"));
  REQUIRE(std::get<depvec::InstructionNode>(function.nodes.at(2)).verification.status ==
          depvec::VerificationStatus::Counterexample);
  const auto &check = function.edges.at(0).interferenceChecks.at(0);
  REQUIRE(check.verification.status == depvec::VerificationStatus::NotChecked);
  REQUIRE(check.verification.detail == "not every node verification condition is proved");
}
