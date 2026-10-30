# DepVec Graph JSON Format

DepVec writes graph exports as UTF-8 JSON using the single format identifier
`depvec-graph-v2`. Both the frontend and backend reader require this format.

## Top-level object

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Format identifier, currently `depvec-graph-v2`. |
| `precondition` | string | Global precondition for the graph. |
| `postcondition` | string | Global postcondition for the graph. |
| `globalInitialValues` | object | Initial values of scalar integer globals, keyed by LLVM name (including `@`) and represented as assertion literals. |
| `module` | string | LLVM module identifier. |
| `functions` | array | Graphs for functions annotated with `depvec`. |

## Function object

| Field | Type | Meaning |
| --- | --- | --- |
| `name` | string | LLVM function name. |
| `ir` | string | LLVM's printed representation of the function. |
| `nodes` | array | The function's graph nodes. |
| `edges` | array | The function's dependency hyperedges. |

## Node object

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | integer | Node identifier, unique within the function. |
| `kind` | string | `init`, `instruction`, or `exit`. |
| `line` | integer | Zero-based line index in `ir`; `-1` if no line could be matched. |
| `instruction` | string | LLVM instruction text, or an empty string for the init node. |
| `operation` | object | Structured instruction data for `instruction` nodes. Omitted for init and exit markers. |

The `operation.opcode` field selects the instruction variant. The supported
opcodes are `add`, `sub`, `mul`, `icmp`, `alloca`, `load`, `store`, `cmpxchg`,
`extractvalue`, `br`, and `phi`. Other LLVM instructions are serialized with
`opcode: "unsupported"` and a `reason`; the backend reader reports these with
the function and node ID.

Instruction references use names without LLVM's `%` sigil. Integer values are
objects of the form `{ "kind": "integer", "value": "42" }`; SSA values are
`{ "kind": "ref", "name": "n" }`. Integer widths are represented as bit
counts, such as `32` for `i32`. Pointers are identified as either an alloca
result, `{ "alloca": "buf" }`, or a global variable,
`{ "global": "shared_value" }`.

For example, an integer add has `opcode`, `result`, `type`, `left`, `right`,
`nsw`, and `nuw` fields. An `icmp` has `result`, `predicate`, `operandType`,
`left`, and `right`. Loads and stores have `pointer`, `type`, and `ordering`;
`cmpxchg` has `pointer`, `expected`, `replacement`, `type`, `successOrdering`,
`failureOrdering`, and `weak`. A branch has a nullable `condition`, `target`,
and nullable `falseTarget`. A phi has `result`, `type`, and `incoming`, an array
of `{ "value": ..., "predecessor": "block" }` entries. `extractvalue` records
its aggregate, index, and result type.

Memory ordering strings are `relaxed`, `acquire`, `release`, `acq_rel`, and
`seq_cst`. LLVM `unordered`, `monotonic`, and non-atomic loads and stores are
represented as `relaxed` in the current model. Pointers are restricted to
direct alloca results and direct global variables; pointer arithmetic, casts,
and other pointer origins are unsupported.

Node IDs are assigned in graph order: the init node comes first, followed by
instructions in LLVM function order. A return instruction is represented by
an `exit` node and is only a graph marker. Node IDs and edge IDs are local to a
function; edge IDs such as `e0` are not stable across exports.

## Edge object

An edge is a hyperedge: it connects zero or more source nodes to one target
node. The extractor emits one source per edge, except for data edges into a
phi. A data edge into any other instruction connects one SSA value's defining
node to that instruction, so an instruction that uses several values has one
data edge per value. Each phi instead has a single data hyperedge whose sources
define all of its incoming values.

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | string | Edge identifier, unique within the function. |
| `sources` | integer array | Source node IDs. |
| `target` | integer | Target node ID. |
| `kind` | string | Dependency category, such as data or memory dependency. |
| `values` | string array | SSA values associated with the dependency, or branch-arm markers on control edges. |
| `assertion` | string | Assertion label for the edge. Currently emitted as an empty string. |
| `interferences` | integer array | IDs of instruction nodes in the same function that interfere with the edge, as defined in `INTERFERENCE.md`. Never contains the init or exit node. |

If `precondition` is empty, the frontend builds it from `globalInitialValues`.
An existing non-empty precondition takes precedence. Initial values are emitted
for initialized scalar integer globals; `i1` values use `true` or `false`, and
other integer values use signed decimal notation. Aggregate and non-integer
initializers are omitted.

The backend parses a non-empty `assertion` string into a formula. An empty
string means the formula `true`, so the edge adds no assertion constraint.

Control edges leaving a conditional branch node carry either
`"true-branch"` or `"false-branch"` in `values`, according to the LLVM
successor index. The extractor creates these edges only for branches with two
successors. The backend treats `br` operations as branch nodes and forms their separate
true and false postconditions by conjoining assertions on control edges marked
for each arm. The extractor propagates arm-labeled control edges from each branch successor
through blocks dominated by the branch, stopping at blocks that post-dominate
the branch, unless the other successor always returns to the branch, as at a
loop exit.
Edges leaving a branch node must be control edges.

## Example

```json
{
  "format": "depvec-graph-v2",
  "precondition": "",
  "postcondition": "",
  "globalInitialValues": {},
  "module": "example.ll",
  "functions": [
    {
      "name": "f",
      "ir": "define void @f() {\nentry:\n  %buf = alloca i32, align 4\n  store i32 42, ptr %buf, align 4\n  ret void\n}",
      "nodes": [
        { "id": 0, "kind": "init", "line": 0, "instruction": "" },
        {
          "id": 1,
          "kind": "instruction",
          "line": 2,
          "instruction": "%buf = alloca i32, align 4",
          "operation": {
            "opcode": "alloca",
            "result": "buf",
            "allocatedType": 32
          }
        },
        {
          "id": 2,
          "kind": "instruction",
          "line": 3,
          "instruction": "store i32 42, ptr %buf, align 4",
          "operation": {
            "opcode": "store",
            "value": { "kind": "integer", "value": "42" },
            "pointer": { "alloca": "buf" },
            "type": 32,
            "ordering": "relaxed"
          }
        },
        { "id": 3, "kind": "exit", "line": 4, "instruction": "ret void" }
      ],
      "edges": [
        {
          "id": "e0",
          "sources": [1],
          "target": 2,
          "kind": "data",
          "values": ["%buf"],
          "assertion": "",
          "interferences": []
        }
      ]
    }
  ]
}
```
