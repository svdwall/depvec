#include "UnrollingOracle.h"

#include <algorithm>
#include <cassert>
#include <optional>

namespace depvec::test {
namespace {

class Oracle {
public:
  Oracle(const InterferenceProgram &Program,
         const std::vector<InterferenceEdge> &Edges, unsigned MaxVisits)
      : Program(Program), Edges(Edges), MaxVisits(MaxVisits),
        Visits(Program.code.size()) {}

  std::set<std::pair<size_t, unsigned>> run() {
    extend(Program.entry);
    return Pairs;
  }

private:
  // Unseparated pairs are prefix-closed: the dependencies of an event only
  // point to earlier events. So checking the maximal paths suffices.
  void extend(unsigned Block) {
    Path.push_back(Block);
    ++Visits[Block];
    bool Extended = false;
    for (unsigned Next : Program.succ[Block]) {
      if (Visits[Next] < MaxVisits) {
        extend(Next);
        Extended = true;
      }
    }
    if (!Extended)
      check();
    --Visits[Block];
    Path.pop_back();
  }

  // The latest event of Node at or before position Last.
  std::optional<size_t> latest(unsigned Node, size_t Last) const {
    for (size_t R = Last + 1; R-- > 0;)
      if (Nodes[R] == Node)
        return R;
    return std::nullopt;
  }

  static bool contains(const std::vector<std::vector<unsigned>> &Regs,
                       unsigned Node, unsigned Reg) {
    return Node < Regs.size() &&
           std::find(Regs[Node].begin(), Regs[Node].end(), Reg) !=
               Regs[Node].end();
  }

  // U uses or assigns a register that D assigns, so every U-event comes
  // before every later D-event.
  bool registerOrdered(unsigned U, unsigned D) const {
    if (D >= Program.defs.size())
      return false;
    for (unsigned Reg : Program.defs[D])
      if (contains(Program.uses, U, Reg) || contains(Program.defs, U, Reg))
        return true;
    return false;
  }

  // σ(o) for an occurrence at position Q of the edge.
  size_t sourceEvent(const InterferenceEdge &Edge, size_t Q) const {
    if (Edge.plainSource) {
      std::optional<size_t> Sigma = latest(*Edge.plainSource, Q - 1);
      assert(Sigma && "a plain source dominates its target");
      return *Sigma;
    }
    // The last entry into the selection block before Q.
    size_t Entry = 0;
    for (size_t J = BlockOf[Q] + 1; J-- > 0;) {
      if (Path[J] == Edge.selectionBlock) {
        Entry = J;
        break;
      }
    }
    assert(Entry > 0 && "the selection block dominates and is not the entry");
    const unsigned Pred = Path[Entry - 1];
    std::optional<size_t> Sigma =
        latest(Edge.selection.at(Pred), BlockEnd[Entry - 1]);
    assert(Sigma && "the selected source dominates the predecessor's end");
    return *Sigma;
  }

  void check() {
    Nodes.assign(1, 0);
    BlockOf.assign(1, 0);
    BlockEnd.clear();
    for (size_t J = 0; J < Path.size(); ++J) {
      for (unsigned Node : Program.code[Path[J]]) {
        Nodes.push_back(Node);
        BlockOf.push_back(J);
      }
      BlockEnd.push_back(Nodes.size() - 1);
    }
    const size_t Length = Nodes.size();

    struct Occurrence {
      size_t edge;
      size_t event;
      size_t source;
    };
    std::vector<Occurrence> Occurrences;
    std::vector<std::vector<size_t>> Dependents(Length);
    for (size_t E = 0; E < Edges.size(); ++E) {
      for (size_t Q = 1; Q < Length; ++Q) {
        if (Nodes[Q] != Edges[E].target)
          continue;
        const size_t Sigma = sourceEvent(Edges[E], Q);
        Occurrences.push_back({E, Q, Sigma});
        Dependents[Sigma].push_back(Q);
      }
    }
    for (size_t Q = 1; Q < Length; ++Q)
      for (size_t O = Q + 1; O < Length; ++O)
        if (registerOrdered(Nodes[Q], Nodes[O]))
          Dependents[Q].push_back(O);

    // After[x][y] is x ≺_π y. Dependencies and register order point forward
    // in position order.
    std::vector<std::vector<bool>> After(Length, std::vector<bool>(Length));
    for (size_t X = Length; X-- > 0;) {
      for (size_t Y : Dependents[X]) {
        After[X][Y] = true;
        for (size_t Z = 0; Z < Length; ++Z)
          if (After[Y][Z])
            After[X][Z] = true;
      }
    }

    for (const Occurrence &O : Occurrences) {
      for (size_t Iota = 1; Iota < Length; ++Iota) {
        const unsigned Node = Nodes[Iota];
        if (Iota == O.event || Iota == O.source ||
            Node >= Program.candidate.size() || !Program.candidate[Node])
          continue;
        if (!After[Iota][O.source] && !After[O.event][Iota])
          Pairs.emplace(O.edge, Node);
      }
    }
  }

  const InterferenceProgram &Program;
  const std::vector<InterferenceEdge> &Edges;
  unsigned MaxVisits;
  std::vector<unsigned> Visits;
  std::vector<unsigned> Path;
  // The current unrolling: node, path index, and the last event per block.
  std::vector<unsigned> Nodes;
  std::vector<size_t> BlockOf;
  std::vector<size_t> BlockEnd;
  std::set<std::pair<size_t, unsigned>> Pairs;
};

} // namespace

std::set<std::pair<size_t, unsigned>>
unseparatedPairs(const InterferenceProgram &Program,
                 const std::vector<InterferenceEdge> &Edges,
                 unsigned MaxVisits) {
  return Oracle(Program, Edges, MaxVisits).run();
}

} // namespace depvec::test
