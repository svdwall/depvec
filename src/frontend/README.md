# DepVec Graph Studio

The frontend is a static browser app for viewing and editing graph layouts. It
can be opened directly for manual file selection, or launched with the command
below to run the pass and load its outputs automatically. The launcher serves
the app only on localhost.

## Create a graph export

From the repository root, first compile the C examples to LLVM IR under
`build/examples/ir`, then export a graph:

```sh
make compile-examples
make graphs IR=build/examples/ir/message_passing.ll
```

This writes `build/examples/graphs/message_passing.depvec-graph.json`. Choose
that JSON file and the matching `.ll` file in `src/frontend/index.html` (open
the HTML file in a browser).
The graph export contains one graph for each function marked with the `depvec`
annotation, along with the LLVM-printed function text and instruction line
anchors used to draw the overlays.

To use a different output path:

```sh
make graphs IR=build/examples/ir/message_passing.ll OUT=/tmp/message-passing.depvec-graph.json
```

## Launch directly from the command line

The launcher can build a graph from LLVM IR or reopen an existing graph JSON.
For example, launch an exported graph directly:

```sh
./scripts/depvec-studio message_passing.depvec-graph.json
```

The graph JSON contains each function's LLVM text, so a separate `.ll` file is
not needed. To regenerate the graph from source IR, run:

```sh
./scripts/depvec-studio build/examples/ir/message_passing.ll
```

It binds only to `127.0.0.1` and serves only the frontend, the selected input,
and the graph JSON. Keep the command running while using the app;
press Ctrl-C to stop it. For a terminal-only launch that prints the URL without
opening a browser, add `--no-open` (the server still runs until Ctrl-C).

Use `--port 8000` to choose a port; by default the launcher picks an available
local port.

## Edit and save

- Use the header theme button to switch between light and dark mode; the choice is remembered in this browser.
- Select an instruction line to add a note.
- Edit the graph-wide initial and final assertions above the function graphs; each function's init and exit node displays the corresponding assertion.
- If the graph has no initial assertion, the frontend derives one from the exported `globalInitialValues`. Existing graph preconditions take precedence.
- Hyperedges appear as colored vertical tracks left of the code, with taps at their source and target instructions.
- Selecting a dependency highlights the instructions that interfere with it (its `interferences`) in the code, and lists them in the editor heading.
- Select a dependency to edit its assertion in the editor below the graph (default: empty); drag its HTML frame vertically along the edge track. Focus the frame and use the up/down arrow keys to nudge it.
- Choose **Save edits** to download a `.depvec-layout.json` file.
- Use **Open edits** to restore those labels and positions along their edge tracks later.
- Choose **Export graph** to download a v2 graph JSON with the current edge assertions written into each edge's `assertion` field. Empty labels are exported as empty assertions, which the backend treats as `true`.

The loaded LLVM IR and graph are read-only; **Export graph** creates a separate
graph file that can be reopened with `./scripts/depvec-studio path/to/graph.json`.
Layout edits are stored separately and keyed by function, node ID, and edge ID.
