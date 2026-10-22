# DepVec Graph Studio

The frontend is a static browser app for viewing and editing graph layouts. It
can be opened directly for manual file selection, or launched with the command
below to run the pass and load its outputs automatically. The launcher serves
the app only on localhost.

## Create a graph export

From the repository root, run:

```sh
make graphs IR=examples/message_passing.ll
```

This writes `depvec-graphs.json` in the repository root. Choose that JSON file
and the matching `.ll` file in `index.html` (open the HTML file in a browser).
The graph export contains one graph for each function marked with the `depvec`
annotation, along with the LLVM-printed function text and instruction line
anchors used to draw the overlays.

To use a different output path:

```sh
make graphs IR=examples/message_passing.ll OUT=/tmp/message-passing-graphs.json
```

## Launch directly from the command line

The launcher builds the plugin, runs it on the selected module, starts a local
web server for the app and its inputs, then opens the workspace in your browser:

```sh
./scripts/depvec-studio examples/message_passing.ll
```

It binds only to `127.0.0.1` and serves only the frontend, the selected IR file,
and the generated graph JSON. Keep the command running while using the app;
press Ctrl-C to stop it. For a terminal-only launch that prints the URL without
opening a browser, add `--no-open` (the server still runs until Ctrl-C).

Use `--port 8000` to choose a port; by default the launcher picks an available
local port.

## Edit and save

- Use the header theme button to switch between light and dark mode; the choice is remembered in this browser.
- Select an instruction line to add a note.
- Hyperedges appear as colored vertical tracks left of the code, with taps at their source and target instructions.
- Click assertion text to edit it (default: `{ }`); drag its HTML frame vertically along the edge track. Focus the frame and use the up/down arrow keys to nudge it.
- Choose **Save edits** to download a `.depvec-layout.json` file.
- Use **Open edits** to restore those labels and positions along their edge tracks later.

The LLVM IR and graph export are read-only in the frontend. Layout edits are
stored separately and keyed by function, node ID, and edge ID.
