#include "InterferenceModel.h"
#include "UnrollingOracle.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <variant>
#include <vector>

using depvec::InterferenceEdge;
using depvec::InterferenceError;
using depvec::InterferenceProgram;

namespace {

// A program whose blocks hold the given nodes, with block 0 as the entry.
// Every node except init is a candidate.
InterferenceProgram program(std::vector<std::vector<unsigned>> Code,
                            std::vector<std::vector<unsigned>> Succ) {
  InterferenceProgram Program;
  for (const std::vector<unsigned> &Block : Code)
    for (unsigned Node : Block)
      Program.numNodes = std::max(Program.numNodes, Node + 1);
  Program.code = std::move(Code);
  Program.succ = std::move(Succ);
  Program.candidate.assign(Program.numNodes, true);
  Program.candidate[0] = false;
  return Program;
}

InterferenceEdge plain(unsigned Source, unsigned Target) {
  InterferenceEdge Edge;
  Edge.target = Target;
  Edge.plainSource = Source;
  return Edge;
}

InterferenceEdge guarded(unsigned Block, std::map<unsigned, unsigned> Selection,
                         unsigned Target) {
  InterferenceEdge Edge;
  Edge.target = Target;
  Edge.selectionBlock = Block;
  Edge.selection = std::move(Selection);
  return Edge;
}

// Node N reads the registers Uses[N] and assigns the registers Defs[N].
void registers(InterferenceProgram &Program,
               std::map<unsigned, std::vector<unsigned>> Uses,
               std::map<unsigned, std::vector<unsigned>> Defs) {
  Program.uses.assign(Program.numNodes, {});
  Program.defs.assign(Program.numNodes, {});
  for (auto &[Node, Regs] : Uses)
    Program.uses[Node] = std::move(Regs);
  for (auto &[Node, Regs] : Defs)
    Program.defs[Node] = std::move(Regs);
}

// Computes the interferences and checks them against the unrolling oracle.
std::vector<std::set<unsigned>>
checkSound(const InterferenceProgram &Program,
           const std::vector<InterferenceEdge> &Edges, unsigned MaxVisits = 3) {
  auto Result = depvec::computeInterferences(Program, Edges);
  if (const auto *Error = std::get_if<InterferenceError>(&Result))
    FAIL(Error->message);
  const auto &Interferences = std::get<std::vector<std::set<unsigned>>>(Result);
  for (const auto &[Edge, Node] :
       depvec::test::unseparatedPairs(Program, Edges, MaxVisits)) {
    INFO("edge " << Edge << ", node " << Node);
    CHECK(Interferences[Edge].count(Node) == 1);
  }
  return Interferences;
}

std::optional<InterferenceError>
check(const InterferenceProgram &Program,
      const std::vector<InterferenceEdge> &Edges) {
  return depvec::checkInterferenceEdges(Program, Edges);
}

} // namespace

TEST_CASE("a straight-line chain has no interferences") {
  const InterferenceProgram Program = program({{1, 2, 3}}, {{}});
  const auto Interferences = checkSound(Program, {plain(1, 2), plain(2, 3)});
  CHECK(Interferences[0].empty());
  CHECK(Interferences[1].empty());
}

TEST_CASE("instructions between source and target interfere") {
  // 1 and 3 are independent; 2 runs between the source 1 and the target 3.
  const InterferenceProgram Program = program({{1, 2, 3}}, {{}});
  const auto Interferences = checkSound(Program, {plain(1, 3)});
  CHECK(Interferences[0] == std::set<unsigned>{2});
}

TEST_CASE("a phi in a diamond") {
  // 0: 1 br | 1: 2 3 | 2: 4 5 | 3: 6 = phi(2, 4), 7
  const InterferenceProgram Program =
      program({{1}, {2, 3}, {4, 5}, {6, 7}}, {{1, 2}, {3}, {3}, {}});
  checkSound(Program, {plain(1, 2), plain(1, 4),
                       guarded(3, {{1, 2}, {2, 4}}, 6), plain(6, 7)});
}

TEST_CASE("a loop header phi carries order only through the recurrence") {
  // 0: 1 = x, 2 br | 1: 3 = h = phi(x, c), 4 = c, 5 br | 2: 6 = use h, 7
  const InterferenceProgram Program =
      program({{1, 2}, {3, 4, 5}, {6, 7}}, {{1}, {1, 2}, {}});
  const InterferenceEdge Phi = guarded(1, {{0, 1}, {1, 4}}, 3);

  SECTION("r = load: x is not ordered before the use") {
    const auto Interferences = checkSound(Program, {Phi, plain(3, 6)});
    CHECK(Interferences[1].count(1) == 1);
  }

  SECTION("r = r + load: x is ordered before the use") {
    const auto Interferences =
        checkSound(Program, {Phi, plain(3, 4), plain(3, 6)});
    CHECK(Interferences[2].count(1) == 0);
  }
}

TEST_CASE("swapped phis read their operands in parallel") {
  // 0: 1, 2, 3 br | 1: 4 = phi(1, 5), 5 = phi(2, 4), 6 br | 2: 7
  const InterferenceProgram Program =
      program({{1, 2, 3}, {4, 5, 6}, {7}}, {{1}, {1, 2}, {}});
  checkSound(Program, {guarded(1, {{0, 1}, {1, 5}}, 4),
                       guarded(1, {{0, 2}, {1, 4}}, 5), plain(4, 7)});
}

TEST_CASE("nested loops") {
  // 0: 1, 9 br | 1: 2 = phi(1, 4), 3 br | 2: 4 = phi(2, 5), 5, 6 br
  // | 3: 7 br | 4: 8
  const InterferenceProgram Program =
      program({{1, 9}, {2, 3}, {4, 5, 6}, {7}, {8}},
              {{1}, {2}, {2, 3}, {1, 4}, {}});
  checkSound(Program, {guarded(1, {{0, 1}, {3, 4}}, 2),
                       guarded(2, {{1, 2}, {2, 5}}, 4), plain(4, 5),
                       plain(2, 8)},
             2);
}

TEST_CASE("a phi with an argument as incoming value") {
  // 0: 1 br | 1: 2 = phi(arg, 3), 3, 4 br | 2: 5
  const InterferenceProgram Program =
      program({{1}, {2, 3, 4}, {5}}, {{1}, {1, 2}, {}});
  checkSound(Program, {guarded(1, {{0, 0}, {1, 3}}, 2), plain(2, 3),
                       plain(0, 5)});
}

TEST_CASE("a guarded edge whose target is in a loop after the join") {
  // 0: 1 br | 1: 2 = acq, 3 | 2: 4 = acq, 5 | 3: 6, 7 | 4: 8 = t, 9 br | 5: 10
  const InterferenceProgram Program =
      program({{1}, {2, 3}, {4, 5}, {6, 7}, {8, 9}, {10}},
              {{1, 2}, {3}, {3}, {4}, {4, 5}, {}});
  const auto Interferences =
      checkSound(Program, {guarded(3, {{1, 2}, {2, 4}}, 8)});
  // 6 runs after the selected acquire; 8 repeats without re-entering 3.
  CHECK(Interferences[0].count(6) == 1);
  CHECK(Interferences[0].count(8) == 1);
}

TEST_CASE("register order orders a loop without a header phi") {
  // The message_passing reader:
  // 0: 1 br | 1: 2 = load, 3 = icmp 2, 4 = br 3 | 2: 5 = load, 6 = ret 5
  InterferenceProgram Program =
      program({{1}, {2, 3, 4}, {5, 6}}, {{1}, {1, 2}, {}});
  const std::vector<InterferenceEdge> Edges{plain(2, 3), plain(3, 4),
                                            plain(5, 6), plain(2, 5),
                                            plain(4, 5), plain(4, 6)};

  SECTION("without registers, every loop instruction interferes") {
    const auto Interferences = checkSound(Program, Edges);
    CHECK(Interferences[2] == std::set<unsigned>{1, 2, 3, 4});
  }

  SECTION("with registers, only the entry branch interferes") {
    registers(Program, {{3, {2}}, {4, {3}}, {6, {5}}},
              {{2, {2}}, {3, {3}}, {5, {5}}});
    const auto Interferences = checkSound(Program, Edges);
    CHECK(Interferences[2] == std::set<unsigned>{1});
    // The latch branch is unordered with the next iteration's load.
    CHECK(Interferences[0].count(4) == 1);
  }
}

TEST_CASE("edges outside the model are rejected") {
  // 0: 1 br | 1: 2 | 2: 3 | 3: 4
  const InterferenceProgram Program =
      program({{1}, {2}, {3}, {4}}, {{1, 2}, {3}, {3}, {}});

  SECTION("a plain source must strictly dominate its target") {
    const auto Error = check(Program, {plain(1, 4), plain(2, 4)});
    REQUIRE(Error);
    CHECK(Error->edge == 1);
    CHECK_FALSE(check(Program, {plain(1, 4), plain(0, 4)}));
    CHECK(check(Program, {plain(4, 4)}));
  }

  SECTION("(G1) the selection block is not the entry and dominates") {
    CHECK(check(Program, {guarded(0, {}, 4)}));
    CHECK(check(Program, {guarded(1, {{0, 1}}, 4)}));
  }

  SECTION("(G2) every predecessor selects a source") {
    CHECK(check(Program, {guarded(3, {{1, 1}}, 4)}));
    CHECK(check(Program, {guarded(3, {{1, 1}, {2, 1}, {0, 1}}, 4)}));
    CHECK_FALSE(check(Program, {guarded(3, {{1, 1}, {2, 0}}, 4)}));
  }

  SECTION("(G3) the selected source strictly dominates the terminator") {
    // 2 is the terminator of block 1 itself; 3 is in the other branch.
    CHECK(check(Program, {guarded(3, {{1, 2}, {2, 1}}, 4)}));
    CHECK(check(Program, {guarded(3, {{1, 3}, {2, 1}}, 4)}));
  }
}

TEST_CASE("random programs are sound against the unrolling oracle") {
  for (unsigned Seed = 0; Seed < 5000; ++Seed) {
    std::mt19937 Random(Seed);
    auto Pick = [&Random](unsigned Bound) {
      return std::uniform_int_distribution<unsigned>(0, Bound - 1)(Random);
    };

    std::vector<std::vector<unsigned>> Code(2 + Pick(5));
    std::vector<std::vector<unsigned>> Succ(Code.size());
    unsigned Next = 1;
    for (size_t B = 0; B < Code.size(); ++B) {
      for (unsigned K = 1 + Pick(3); K > 0; --K)
        Code[B].push_back(Next++);
      for (unsigned K = Pick(3); K > 0; --K)
        Succ[B].push_back(Pick(Code.size()));
    }
    InterferenceProgram Program = program(Code, Succ);
    // Random registers, not necessarily in SSA form.
    std::map<unsigned, std::vector<unsigned>> Uses;
    std::map<unsigned, std::vector<unsigned>> Defs;
    for (unsigned Node = 1; Node < Program.numNodes; ++Node) {
      if (Pick(2) == 0)
        Uses[Node].push_back(Pick(4));
      if (Pick(2) == 0)
        Defs[Node].push_back(Pick(4));
    }
    registers(Program, Uses, Defs);

    std::vector<InterferenceEdge> Edges;
    auto TryAdd = [&](InterferenceEdge Edge) {
      std::vector<InterferenceEdge> With = Edges;
      With.push_back(std::move(Edge));
      if (!check(Program, With))
        Edges = std::move(With);
    };
    for (unsigned Attempt = 0; Attempt < 20 && Edges.size() < 4; ++Attempt)
      TryAdd(plain(Pick(Program.numNodes), 1 + Pick(Program.numNodes - 1)));
    for (unsigned Attempt = 0; Attempt < 40 && Edges.size() < 6; ++Attempt) {
      const unsigned J = 1 + Pick(Code.size() - 1);
      std::map<unsigned, unsigned> Selection;
      for (size_t P = 0; P < Code.size(); ++P)
        if (std::count(Succ[P].begin(), Succ[P].end(), J) > 0)
          Selection[P] = Pick(Program.numNodes);
      TryAdd(guarded(J, Selection, 1 + Pick(Program.numNodes - 1)));
    }

    INFO("seed " << Seed);
    checkSound(Program, Edges);
  }
}
