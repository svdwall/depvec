#include "InterferenceModel.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace depvec {
namespace {

using Bits = std::vector<bool>;
constexpr unsigned InitNode = 0;

// The instruction CFG of Section 1: each node steps to the next node of its
// block, each terminator to the first node of every successor block, and init
// to the first node of the entry block.
class InstructionCfg {
public:
  explicit InstructionCfg(const InterferenceProgram &Program)
      : Program(Program), Succ(Program.numNodes), Pred(Program.numNodes),
        BlockPreds(Program.code.size()) {
    for (unsigned B = 0; B < Program.code.size(); ++B) {
      const std::vector<unsigned> &Code = Program.code[B];
      for (size_t K = 0; K + 1 < Code.size(); ++K)
        addStep(Code[K], Code[K + 1]);
      for (unsigned Next : Program.succ[B]) {
        addStep(Code.back(), first(Next));
        BlockPreds[Next].push_back(B);
      }
    }
    addStep(InitNode, first(Program.entry));
    for (std::vector<unsigned> &Preds : BlockPreds) {
      std::sort(Preds.begin(), Preds.end());
      Preds.erase(std::unique(Preds.begin(), Preds.end()), Preds.end());
    }
  }

  unsigned first(unsigned Block) const { return Program.code[Block].front(); }
  unsigned term(unsigned Block) const { return Program.code[Block].back(); }
  const std::vector<std::vector<unsigned>> &succ() const { return Succ; }
  const std::vector<std::vector<unsigned>> &pred() const { return Pred; }
  const std::vector<unsigned> &blockPreds(unsigned Block) const {
    return BlockPreds[Block];
  }

private:
  void addStep(unsigned From, unsigned To) {
    Succ[From].push_back(To);
    Pred[To].push_back(From);
  }

  const InterferenceProgram &Program;
  std::vector<std::vector<unsigned>> Succ;
  std::vector<std::vector<unsigned>> Pred;
  std::vector<std::vector<unsigned>> BlockPreds;
};

// Memoized reachability on the instruction CFG. Every query is answered for
// all end nodes at once and cached per start node and avoided set.
class Reachability {
public:
  Reachability(const InstructionCfg &Cfg, unsigned NumNodes)
      : Cfg(Cfg), NumNodes(NumNodes) {}

  // reach^{∖X}(A, B) for every B: a non-empty path from A to B with no node
  // of X strictly inside.
  const Bits &from(unsigned A, std::vector<unsigned> Avoid) {
    std::sort(Avoid.begin(), Avoid.end());
    Avoid.erase(std::unique(Avoid.begin(), Avoid.end()), Avoid.end());
    auto [It, Inserted] = Avoiding.try_emplace({A, Avoid});
    if (Inserted)
      It->second = search(A, Avoid, /*Backward=*/false, std::nullopt);
    return It->second;
  }

  // reach_{↛J}(A, B) for every B: a non-empty path that takes no entry into
  // block J.
  const Bits &fromNoEntry(unsigned A, unsigned J) {
    auto [It, Inserted] = NoEntryFrom.try_emplace({A, J});
    if (Inserted)
      It->second = search(A, {}, /*Backward=*/false, Cfg.first(J));
    return It->second;
  }

  // reach_{↛J}(A, B) for every A.
  const Bits &toNoEntry(unsigned B, unsigned J) {
    auto [It, Inserted] = NoEntryTo.try_emplace({B, J});
    if (Inserted)
      It->second = search(B, {}, /*Backward=*/true, Cfg.first(J));
    return It->second;
  }

private:
  // Searches from Start along steps, or against them if Backward. Nodes in
  // Avoid are reached but not passed through. A step into Blocked is never
  // taken.
  Bits search(unsigned Start, const std::vector<unsigned> &Avoid,
              bool Backward, std::optional<unsigned> Blocked) const {
    const std::vector<std::vector<unsigned>> &Adjacent =
        Backward ? Cfg.pred() : Cfg.succ();
    Bits Reached(NumNodes);
    std::vector<unsigned> Stack;
    auto Expand = [&](unsigned Node) {
      if (Backward && Node == Blocked)
        return;
      for (unsigned Next : Adjacent[Node])
        if (!(!Backward && Next == Blocked) && !Reached[Next])
          Stack.push_back(Next);
    };
    Expand(Start);
    while (!Stack.empty()) {
      const unsigned Node = Stack.back();
      Stack.pop_back();
      if (Reached[Node])
        continue;
      Reached[Node] = true;
      if (std::find(Avoid.begin(), Avoid.end(), Node) == Avoid.end())
        Expand(Node);
    }
    return Reached;
  }

  const InstructionCfg &Cfg;
  unsigned NumNodes;
  std::map<std::pair<unsigned, std::vector<unsigned>>, Bits> Avoiding;
  std::map<std::pair<unsigned, unsigned>, Bits> NoEntryFrom;
  std::map<std::pair<unsigned, unsigned>, Bits> NoEntryTo;
};

std::optional<std::string>
checkProgram(const InterferenceProgram &Program) {
  if (Program.numNodes == 0)
    return "the program has no init node";
  if (Program.code.size() != Program.succ.size())
    return "the program has successors for a different number of blocks";
  if (Program.entry >= Program.code.size())
    return "the entry block does not exist";
  std::vector<bool> Placed(Program.numNodes);
  for (unsigned B = 0; B < Program.code.size(); ++B) {
    const std::string Block = "block " + std::to_string(B);
    if (Program.code[B].empty())
      return Block + " is empty";
    for (unsigned Node : Program.code[B]) {
      if (Node == InitNode || Node >= Program.numNodes)
        return Block + " contains an invalid node " + std::to_string(Node);
      if (Placed[Node])
        return "node " + std::to_string(Node) + " is in more than one block";
      Placed[Node] = true;
    }
    for (unsigned Next : Program.succ[B])
      if (Next >= Program.code.size())
        return Block + " has an invalid successor " + std::to_string(Next);
  }
  if (Program.uses.size() > Program.numNodes ||
      Program.defs.size() > Program.numNodes)
    return "the program has registers for nodes that do not exist";
  return std::nullopt;
}

class InterferenceSolver {
public:
  InterferenceSolver(const InterferenceProgram &Program,
                     const std::vector<InterferenceEdge> &Edges)
      : Program(Program), Edges(Edges), NumNodes(Program.numNodes),
        Cfg(Program), Reach(Cfg, NumNodes) {}

  std::optional<InterferenceError> check();
  std::vector<std::set<unsigned>> solve();

private:
  bool reach(unsigned A, unsigned B, std::vector<unsigned> Avoid) {
    return Reach.from(A, std::move(Avoid))[B];
  }
  bool strictlyDominates(unsigned A, unsigned B) {
    return A != B && (A == InitNode || !reach(InitNode, B, {A}));
  }
  std::optional<std::string> checkEdge(const InterferenceEdge &Edge);

  void computePrec0();
  void computeRegions();
  void seedRegisterOrder();
  bool fixpointPass();
  bool sourceOrdered(unsigned A, unsigned S, unsigned T);
  bool rule3(unsigned A, size_t E);
  bool separated(size_t E, unsigned I);

  const InterferenceProgram &Program;
  const std::vector<InterferenceEdge> &Edges;
  unsigned NumNodes;
  InstructionCfg Cfg;
  Reachability Reach;
  // Prec0[a][b] is a ≺₀ b.
  std::vector<Bits> Prec0;
  // Region[e][x] is x ∈ region(e), for guarded edges.
  std::vector<Bits> Region;
  // Before[a][b] is a ⊲ b, BeforeEdge[a][e] is a ⊲ e.
  std::vector<Bits> Before;
  std::vector<Bits> BeforeEdge;
};

std::optional<std::string>
InterferenceSolver::checkEdge(const InterferenceEdge &Edge) {
  const unsigned T = Edge.target;
  if (T == InitNode || T >= NumNodes)
    return "the target is not an instruction node";

  if (Edge.plainSource) {
    const unsigned S = *Edge.plainSource;
    if (S >= NumNodes)
      return "the source does not exist";
    if (!strictlyDominates(S, T))
      return "the source does not strictly dominate the target";
    return std::nullopt;
  }

  // Guarded edge: (G1)-(G3).
  const unsigned J = Edge.selectionBlock;
  if (J >= Program.code.size())
    return "the selection block does not exist";
  if (J == Program.entry)
    return "the selection block is the entry block";
  if (Reach.fromNoEntry(InitNode, J)[T])
    return "the selection block does not dominate the target";
  const std::vector<unsigned> &Preds = Cfg.blockPreds(J);
  for (const auto &[P, S] : Edge.selection) {
    if (!std::binary_search(Preds.begin(), Preds.end(), P))
      return "a source is selected for block " + std::to_string(P) +
             ", which is not a predecessor of the selection block";
    if (S >= NumNodes)
      return "the source selected for block " + std::to_string(P) +
             " does not exist";
  }
  for (unsigned P : Preds) {
    auto It = Edge.selection.find(P);
    if (It == Edge.selection.end())
      return "no source is selected for predecessor block " +
             std::to_string(P);
    if (!strictlyDominates(It->second, Cfg.term(P)))
      return "the source selected for block " + std::to_string(P) +
             " does not strictly dominate its terminator";
  }
  return std::nullopt;
}

std::optional<InterferenceError> InterferenceSolver::check() {
  for (size_t E = 0; E < Edges.size(); ++E)
    if (std::optional<std::string> Error = checkEdge(Edges[E]))
      return InterferenceError{E, *Error};
  return std::nullopt;
}

// a ≺₀ b: the transitive closure of the plain edges.
void InterferenceSolver::computePrec0() {
  Prec0.assign(NumNodes, Bits(NumNodes));
  for (const InterferenceEdge &Edge : Edges)
    if (Edge.plainSource)
      Prec0[*Edge.plainSource][Edge.target] = true;
  for (unsigned K = 0; K < NumNodes; ++K)
    for (unsigned A = 0; A < NumNodes; ++A)
      if (Prec0[A][K])
        for (unsigned B = 0; B < NumNodes; ++B)
          if (Prec0[K][B])
            Prec0[A][B] = true;
}

// region(e) = { x | (x = first(J) ∨ reach_{↛J}(first(J), x)) ∧
//                   reach_{↛J}(x, t) }.
void InterferenceSolver::computeRegions() {
  Region.assign(Edges.size(), Bits(NumNodes));
  for (size_t E = 0; E < Edges.size(); ++E) {
    const InterferenceEdge &Edge = Edges[E];
    if (Edge.plainSource)
      continue;
    const unsigned J = Edge.selectionBlock;
    const unsigned First = Cfg.first(J);
    const Bits &FromFirst = Reach.fromNoEntry(First, J);
    const Bits &ToTarget = Reach.toNoEntry(Edge.target, J);
    for (unsigned X = 0; X < NumNodes; ++X)
      Region[E][X] = (X == First || FromFirst[X]) && ToTarget[X];
  }
}

// R4: u ⊲ d when u uses or assigns a register that d assigns.
void InterferenceSolver::seedRegisterOrder() {
  std::map<unsigned, std::vector<unsigned>> Assigners;
  for (unsigned D = 0; D < Program.defs.size(); ++D)
    for (unsigned R : Program.defs[D])
      Assigners[R].push_back(D);
  auto Order = [&](unsigned U, const std::vector<std::vector<unsigned>> &Regs) {
    if (U >= Regs.size())
      return;
    for (unsigned R : Regs[U]) {
      auto It = Assigners.find(R);
      if (It != Assigners.end())
        for (unsigned D : It->second)
          Before[U][D] = true;
    }
  };
  for (unsigned U = 0; U < NumNodes; ++U) {
    Order(U, Program.uses);
    Order(U, Program.defs);
  }
}

// The ordered-source and recurrence conditions of R3 for source S.
bool InterferenceSolver::sourceOrdered(unsigned A, unsigned S, unsigned T) {
  const bool Upcoming = A == S || reach(A, S, {T});
  if (!Upcoming)
    return true;
  return (A == S || Before[A][S]) && (!reach(S, S, {T}) || Before[S][S]);
}

// R3: a ⊲ e.
bool InterferenceSolver::rule3(unsigned A, size_t E) {
  const InterferenceEdge &Edge = Edges[E];
  const unsigned T = Edge.target;
  if (Edge.plainSource) {
    const unsigned S = *Edge.plainSource;
    const bool Covered = A == S || !reach(A, T, {S, T});
    return Covered && sourceOrdered(A, S, T);
  }

  if (Region[E][A])
    return false;
  for (unsigned P : Cfg.blockPreds(Edge.selectionBlock)) {
    // Only predecessors that can make the next selecting entry matter.
    const unsigned Term = Cfg.term(P);
    if (A != Term && !reach(A, Term, {T}))
      continue;
    const unsigned S = Edge.selection.at(P);
    const bool Covered = A == S || (A != Term && !reach(A, Term, {S, T}));
    if (!Covered || !sourceOrdered(A, S, T))
      return false;
  }
  return true;
}

// One pass of R0-R3 over all facts not derived yet. Returns whether it
// derived a new fact.
bool InterferenceSolver::fixpointPass() {
  bool Changed = false;
  auto Derive = [&Changed](Bits &Row, size_t Column) {
    if (!Row[Column]) {
      Row[Column] = true;
      Changed = true;
    }
  };

  for (unsigned A = 0; A < NumNodes; ++A) {
    // R0: a ≺₀ b and (reach^{∖b}(a, a) → a ⊲ a).
    for (unsigned B = 0; B < NumNodes; ++B)
      if (!Before[A][B] && Prec0[A][B] && (!reach(A, A, {B}) || Before[A][A]))
        Derive(Before[A], B);

    // R1: a ⊲ e → a ⊲ t_e.
    for (size_t E = 0; E < Edges.size(); ++E)
      if (BeforeEdge[A][E])
        Derive(Before[A], Edges[E].target);

    // R2: a ⊲ b, b ⊲ c, and ¬reach^{∖b}(a, c).
    for (unsigned B = 0; B < NumNodes; ++B) {
      if (!Before[A][B])
        continue;
      const Bits &Reached = Reach.from(A, {B});
      for (unsigned C = 0; C < NumNodes; ++C)
        if (Before[B][C] && !Reached[C])
          Derive(Before[A], C);
    }

    // R3.
    for (size_t E = 0; E < Edges.size(); ++E)
      if (!BeforeEdge[A][E] && rule3(A, E))
        Derive(BeforeEdge[A], E);
  }
  return Changed;
}

// The Layer 3 table.
bool InterferenceSolver::separated(size_t E, unsigned I) {
  const unsigned T = Edges[E].target;
  const bool IReachesT = reach(I, T, {});
  const bool TReachesI = reach(T, I, {});
  // (a)
  if (IReachesT && !BeforeEdge[I][E])
    return false;
  // (b)
  if (TReachesI && !Before[T][I])
    return false;
  // (c)
  if (IReachesT && reach(T, T, {I}) && !BeforeEdge[T][E])
    return false;
  // (d)
  if (TReachesI && reach(I, I, {T}) && !Before[I][I])
    return false;
  return true;
}

std::vector<std::set<unsigned>> InterferenceSolver::solve() {
  computePrec0();
  computeRegions();
  Before.assign(NumNodes, Bits(NumNodes));
  BeforeEdge.assign(NumNodes, Bits(Edges.size()));
  seedRegisterOrder();
  while (fixpointPass()) {
  }

  std::vector<std::set<unsigned>> Result(Edges.size());
  for (size_t E = 0; E < Edges.size(); ++E)
    for (unsigned I = 0; I < NumNodes; ++I)
      if (I < Program.candidate.size() && Program.candidate[I] &&
          !separated(E, I))
        Result[E].insert(I);
  return Result;
}

} // namespace

std::optional<InterferenceError>
checkInterferenceEdges(const InterferenceProgram &Program,
                       const std::vector<InterferenceEdge> &Edges) {
  if (std::optional<std::string> Error = checkProgram(Program))
    return InterferenceError{std::nullopt, *Error};
  return InterferenceSolver(Program, Edges).check();
}

std::variant<InterferenceError, std::vector<std::set<unsigned>>>
computeInterferences(const InterferenceProgram &Program,
                     const std::vector<InterferenceEdge> &Edges) {
  if (std::optional<InterferenceError> Error =
          checkInterferenceEdges(Program, Edges))
    return *Error;
  return InterferenceSolver(Program, Edges).solve();
}

} // namespace depvec
