const $ = (selector) => document.querySelector(selector);
const NS = 'http://www.w3.org/2000/svg';
const EDGE_LABEL_MAX_WIDTH = 280;
const EDGE_LANE_SPACING = 16;
const EDGE_TRACK_LABEL_GAP = 8;
const EDGE_ENDPOINT_SPACING = 5;
const EDGE_ENDPOINT_BASE_SPACING = 8;

function edgeLabelWidth(kind, assertion, showKind, showAssertion) {
  const kindWidth = showKind ? Math.min(72, String(kind || 'dependency').length * 6.1) + 8 : 0;
  const assertionWidth = showAssertion ? Math.max(38, String(assertion || '').length * 6.1) : 0;
  return Math.min(EDGE_LABEL_MAX_WIDTH, 14 + kindWidth + assertionWidth);
}

function findIRVariables(ir) {
  const variables = new Set();
  const variablePattern = /[%@](?:[A-Za-z_][A-Za-z_0-9.]*|[0-9]+)/g;
  for (const match of String(ir ?? '').matchAll(variablePattern)) variables.add(match[0]);
  return variables;
}

function appendHighlightedAssertion(container, assertion, irVariables) {
  const text = String(assertion ?? '');
  const tokenPattern = /\s+|\|->|->|&&|\|\||!=|<=|>=|\b(?:true|false)\b|%(?:[A-Za-z_][A-Za-z_0-9.]*|[0-9]+)(?:\[[0-9]+\])?|@[A-Za-z_][A-Za-z_0-9.]*|@[0-9]+|[0-9]+|[A-Za-z_][A-Za-z_0-9.]*|[=<>+*/!()-]|[{}]|./gy;
  let index = 0;
  while (index < text.length) {
    tokenPattern.lastIndex = index;
    const match = tokenPattern.exec(text);
    if (!match) break;
    const token = match[0];
    index = tokenPattern.lastIndex;
    let type = '';
    if (/^\s+$/.test(token)) type = 'whitespace';
    else if (/^(?:true|false)$/.test(token)) type = 'keyword';
    else if (/^(?:\|->|->|&&|\|\||!=|<=|>=|[=<>+*/!-])$/.test(token)) type = 'operator';
    // A member such as %r[0] is checked by its base value.
    else if (/^%/.test(token)) type = irVariables.has(token.replace(/\[[0-9]+\]$/, '')) ? 'local' : 'invalid';
    else if (/^@/.test(token)) type = irVariables.has(token) ? 'global' : 'invalid';
    else if (/^[0-9]+$/.test(token)) type = 'number';
    else if (/^[A-Za-z_]/.test(token)) type = irVariables.has(`%${token}`) ? 'alloca' : 'invalid';
    else if (/^[(){}]$/.test(token)) type = 'punctuation';
    else type = 'invalid';

    const span = document.createElement('span');
    if (type !== 'whitespace') span.className = `assertion-token ${type}`;
    span.textContent = token;
    container.append(span);
  }
}

const state = {
  irFile: null,
  irText: '',
  graphFile: null,
  graphDoc: null,
  graphIRVariables: new Set(),
  graphPreconditionProvided: false,
  layoutDoc: { format: 'depvec-layout-v1', functions: {} },
  models: [],
  drag: null,
  // Backend results by function name, cleared whenever an assertion changes.
  verification: null,
};

const irInput = $('#ir-file');
const graphInput = $('#graph-file');
const layoutInput = $('#layout-file');
const statusMessage = $('#status-message');
const workspace = $('#workspace');
const emptyState = $('#empty-state');
const functionContracts = $('#function-contracts');
const functionGrid = $('#function-grid');
const saveButton = $('#save-layout');
const exportGraphButton = $('#export-graph');
const verifyButton = $('#verify-graph');
const themeToggle = $('#theme-toggle');
const themeLabel = $('#theme-label');

function readThemePreference() {
  try {
    return localStorage.getItem('depvec-theme') === 'dark' ? 'dark' : 'light';
  } catch (_error) {
    return 'light';
  }
}

function setTheme(theme, persist = true) {
  const mode = theme === 'dark' ? 'dark' : 'light';
  document.documentElement.dataset.theme = mode;
  $('#theme-color').content = mode === 'dark' ? '#0d1320' : '#f4f6fb';
  const nextMode = mode === 'dark' ? 'light' : 'dark';
  themeToggle.setAttribute('aria-pressed', String(mode === 'dark'));
  themeToggle.setAttribute('aria-label', `Switch to ${nextMode} mode`);
  themeLabel.textContent = `${nextMode[0].toUpperCase()}${nextMode.slice(1)} mode`;

  if (persist) {
    try {
      localStorage.setItem('depvec-theme', mode);
    } catch (_error) {
      // Theme changes still work when browser storage is unavailable.
    }
  }

  for (const model of state.models) {
    model.card?.querySelectorAll('.legend-swatch').forEach((swatch) => {
      swatch.style.background = edgeColor(swatch.dataset.kind);
    });
    drawOverlay(model);
  }
}

setTheme(readThemePreference(), false);
themeToggle.addEventListener('click', () => {
  setTheme(document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark');
});

irInput.addEventListener('change', async () => {
  const file = irInput.files?.[0];
  if (!file) return;
  state.irFile = file;
  state.irText = await file.text();
  $('#ir-name').textContent = file.name;
  refreshWorkspace();
});

graphInput.addEventListener('change', async () => {
  const file = graphInput.files?.[0];
  if (!file) return;
  try {
    const document = JSON.parse(await file.text());
    validateGraphDocument(document);
    state.graphPreconditionProvided = Boolean(document.precondition.trim());
    state.graphFile = file;
    state.graphDoc = document;
    $('#graph-name').textContent = file.name;
    refreshWorkspace();
  } catch (error) {
    setStatus(error.message || 'That graph file could not be read.');
  }
});

layoutInput.addEventListener('change', async () => {
  const file = layoutInput.files?.[0];
  if (!file) return;
  try {
    const document = JSON.parse(await file.text());
    if (document.format !== 'depvec-layout-v1' || !document.functions) {
      throw new Error('This is not a DepVec layout edits file.');
    }
    state.layoutDoc = document;
    refreshWorkspace();
    setStatus(`Loaded saved labels and positions along their edge tracks from ${file.name}.`);
  } catch (error) {
    setStatus(error.message || 'That edits file could not be read.');
  }
});

saveButton.addEventListener('click', saveLayout);
exportGraphButton.addEventListener('click', exportGraph);
verifyButton.addEventListener('click', verifyGraph);

loadCommandLineSession();

async function loadCommandLineSession() {
  try {
    const sessionResponse = await fetch('/api/session');
    if (!sessionResponse.ok) return;
    const session = await sessionResponse.json();
    // Only the command-line launcher can run the backend.
    verifyButton.hidden = false;
    const [irResponse, graphResponse] = await Promise.all([
      fetch('/api/ir'),
      fetch('/api/graphs'),
    ]);
    if (!irResponse.ok || !graphResponse.ok || state.irFile || state.graphDoc) return;
    const graphDocument = await graphResponse.json();
    validateGraphDocument(graphDocument);
    state.graphPreconditionProvided = Boolean(graphDocument.precondition.trim());
    state.irText = await irResponse.text();
    state.irFile = session.irName ? { name: session.irName } : null;
    state.graphFile = { name: session.graphName };
    state.graphDoc = graphDocument;
    $('#ir-name').textContent = session.irName;
    $('#graph-name').textContent = session.graphName;
    refreshWorkspace();
  } catch (_error) {
    // Opening index.html directly remains supported; the /api routes only
    // exist when the command-line launcher is serving the app.
  }
}

window.addEventListener('pointermove', (event) => {
  const drag = state.drag;
  if (!drag || event.pointerId !== drag.pointerId) return;
  drag.lastEvent = event;
  const position = clientPointInScene(drag.model.scene, event);
  const nextY = position.y + drag.offsetY;
  if (!drag.moved && Math.abs(nextY - drag.startY) < 3) return;
  drag.moved = true;
  if (drag.frame) return;
  drag.frame = requestAnimationFrame(() => {
    const activeDrag = state.drag;
    if (!activeDrag) return;
    activeDrag.frame = 0;
    const current = clientPointInScene(activeDrag.model.scene, activeDrag.lastEvent);
    const edgeLayout = getFunctionLayout(activeDrag.model.function.name).edges[activeDrag.edge.id];
    edgeLayout.y = clamp(current.y + activeDrag.offsetY, activeDrag.trackTop, activeDrag.trackBottom);
    drawOverlay(activeDrag.model);
  });
});

window.addEventListener('pointerup', (event) => {
  if (state.drag && event.pointerId !== state.drag.pointerId) return;
  const drag = state.drag;
  if (drag?.frame) cancelAnimationFrame(drag.frame);
  state.drag = null;
  if (!drag) return;
  if (!drag.moved) {
    if (drag.model.altShowAllEdges && !edgeHasAssertion(drag.model, drag.edge.id)) {
      selectUnannotatedEdge(drag.model, drag.edge);
    } else {
      selectEntity(drag.model, 'edge', drag.edge.id);
    }
  } else {
    const position = clientPointInScene(drag.model.scene, event);
    const edgeLayout = getFunctionLayout(drag.model.function.name).edges[drag.edge.id];
    edgeLayout.y = clamp(position.y + drag.offsetY, drag.trackTop, drag.trackBottom);
    selectEntity(drag.model, 'edge', drag.edge.id);
  }
});

window.addEventListener('pointercancel', (event) => {
  if (state.drag && event.pointerId !== state.drag.pointerId) return;
  if (state.drag?.frame) cancelAnimationFrame(state.drag.frame);
  state.drag = null;
});

function setTemporaryAllEdges(active) {
  const changed = state.models.some((model) => model.altShowAllEdges !== active);
  if (!changed) return;
  state.models.forEach((model) => { model.altShowAllEdges = active; });
  state.models.forEach(setupOverlay);
}

window.addEventListener('keydown', (event) => {
  if (event.key !== 'Alt' || event.repeat || event.target.matches('input, textarea, [contenteditable="true"]')) return;
  setTemporaryAllEdges(true);
});

window.addEventListener('keydown', (event) => {
  if (event.key !== 'Escape' || event.target.matches('input, textarea, [contenteditable="true"]')) return;
  state.models.filter(initModeArmed).forEach(clearSelection);
});

window.addEventListener('keyup', (event) => {
  if (event.key === 'Alt') setTemporaryAllEdges(false);
});

window.addEventListener('blur', () => setTemporaryAllEdges(false));

function clientPointInScene(scene, event) {
  const rect = scene.getBoundingClientRect();
  return {
    x: event.clientX - rect.left,
    y: event.clientY - rect.top,
  };
}

function setStatus(message, isError = false) {
  statusMessage.textContent = message;
  statusMessage.classList.toggle('error-banner', isError);
}

function validateGraphDocument(document) {
  if (document?.format !== 'depvec-graph-v2' || !Array.isArray(document.functions)) {
    throw new Error('This is not a DepVec graph export. Generate one with `make graphs IR=path/to/file.ll`.');
  }
  if (typeof document.precondition !== 'string') document.precondition = '';
  if (typeof document.postcondition !== 'string') document.postcondition = '';
  if (!document.globalInitialValues || typeof document.globalInitialValues !== 'object' || Array.isArray(document.globalInitialValues)) {
    document.globalInitialValues = {};
  }
  for (const fn of document.functions) {
    if (typeof fn.name !== 'string' || typeof fn.ir !== 'string' ||
        !Array.isArray(fn.nodes) || !Array.isArray(fn.edges)) {
      throw new Error('The graph export is missing a function name, IR text, nodes, or edges.');
    }
  }
}

function initialGlobalAssertions(globalInitialValues) {
  return Object.entries(globalInitialValues || {})
    .filter(([name, value]) => /^@(?:[A-Za-z_][A-Za-z_0-9.]*|[0-9]+)$/.test(name) && /^(?:-?[0-9]+|true|false)$/.test(String(value)))
    .map(([name, value]) => `${name} |-> ${value}`)
    .join(' && ');
}

function refreshWorkspace() {
  if (!state.graphDoc) {
    workspace.hidden = true;
    emptyState.hidden = false;
    saveButton.disabled = true;
    exportGraphButton.disabled = true;
    verifyButton.disabled = true;
    setStatus(state.irFile
      ? 'Choose the matching graph export in the header.'
      : 'Choose a graph JSON export above, or choose a matching LLVM IR and graph JSON pair.');
    return;
  }

  if (!state.graphPreconditionProvided) {
    state.graphDoc.precondition = initialGlobalAssertions(state.graphDoc.globalInitialValues);
  }
  state.models.forEach((model) => model.resizeObserver?.disconnect());
  state.models = state.graphDoc.functions.map((fn, index) => createFunctionModel(fn, index));
  workspace.hidden = false;
  emptyState.hidden = true;
  saveButton.disabled = false;
  exportGraphButton.disabled = false;
  verifyButton.disabled = false;
  state.verification = null;
  setStatus(state.models.length
    ? `${state.models.length} marked function${state.models.length === 1 ? '' : 's'} loaded from the graph export.`
    : 'No functions marked with the depvec annotation were found in this graph export.');
  renderWorkspace();
}

function createFunctionModel(fn, index) {
  const source = findFunctionSource(state.irText, fn.name);
  let lines;
  let nodeLines = new Map();
  let warning = '';

  if (source) {
    const mapping = mapInstructions(source.lines, fn.nodes);
    if (mapping.complete) {
      lines = source.lines;
      nodeLines = mapping.nodeLines;
    } else if (state.irFile) {
      warning = 'Some instructions could not be aligned to the selected module. Showing the canonical LLVM IR emitted with this graph.';
    }
  } else if (state.irFile) {
    warning = `Function ${fn.name} was not found in the selected module. Showing the canonical LLVM IR emitted with this graph.`;
  }

  if (!lines) {
    const graphIRLines = fn.ir.replace(/\r\n/g, '\n').split('\n');
    while (graphIRLines.at(-1) === '') graphIRLines.pop();
    const firstFunctionLine = graphIRLines.findIndex((line) => /^\s*define\b/.test(line));
    const lineOffset = firstFunctionLine > 0 && graphIRLines
      .slice(0, firstFunctionLine)
      .every((line) => !line.trim() || /^\s*;/.test(line))
      ? firstFunctionLine
      : 0;
    lines = graphIRLines.slice(lineOffset);
    nodeLines = new Map(fn.nodes.map((node) => {
      const line = Number(node.line);
      return [Number(node.id), node.kind === 'init' ? 0 : line >= 0 ? line - lineOffset : line];
    }));
  }

  return {
    function: fn,
    irVariables: findIRVariables(fn.ir),
    index,
    lines,
    nodeLines,
    warning,
    selected: null,
    card: null,
    svg: null,
    pre: null,
    viewport: null,
    scene: null,
    labelLayer: null,
    codeWidth: 0,
    codeLeft: 0,
    sceneWidth: 0,
    sceneHeight: 0,
    anchors: new Map(),
    edgeTracks: new Map(),
    lineNodes: new Map(),
    showAllEdges: false,
    altShowAllEdges: false,
    initPreview: null,
    edgeVisibilityButton: null,
  };
}

function findFunctionSource(moduleText, expectedName) {
  if (!moduleText) return null;
  const lines = moduleText.replace(/\r\n/g, '\n').split('\n');
  for (let start = 0; start < lines.length; start += 1) {
    if (!/^\s*define\b/.test(lines[start])) continue;
    const body = findFunctionBody(lines, start);
    if (!body) continue;
    const header = lines.slice(start, body.openLine + 1).join('\n');
    const symbol = /@(?:"((?:\\[0-9a-fA-F]{2}|\\.|[^"])*)"|([-a-zA-Z$._0-9]+))\s*\(/.exec(header);
    if (!symbol) continue;
    const name = decodeLLVMName(symbol[1] ?? symbol[2]);
    if (name === expectedName)
      return { lines: lines.slice(start, body.closeLine + 1) };
    start = body.closeLine;
  }
  return null;
}

function findFunctionBody(lines, startLine) {
  const source = lines.slice(startLine).join('\n');
  const symbol = /@(?:"((?:\\[0-9a-fA-F]{2}|\\.|[^"])*)"|([-a-zA-Z$._0-9]+))\s*\(/.exec(source);
  if (!symbol) return null;
  const openParen = symbol.index + symbol[0].lastIndexOf('(');

  const quoted = (text, index, state) => {
    const character = text[index];
    if (state.inString) {
      if (state.escaped) state.escaped = false;
      else if (character === '\\') state.escaped = true;
      else if (character === '"') state.inString = false;
      return true;
    }
    if (character === '"') { state.inString = true; return true; }
    return false;
  };
  const lineForOffset = (offset) => startLine + source.slice(0, offset).split('\n').length - 1;

  let parenDepth = 0;
  let closeParen = -1;
  const quoteState = { inString: false, escaped: false };
  for (let index = openParen; index < source.length; index += 1) {
    const character = source[index];
    if (quoted(source, index, quoteState)) continue;
    if (character === ';') {
      const newline = source.indexOf('\n', index);
      if (newline < 0) break;
      index = newline;
      continue;
    }
    if (character === '(') parenDepth += 1;
    else if (character === ')' && --parenDepth === 0) { closeParen = index; break; }
  }
  if (closeParen < 0) return null;

  let openBrace = -1;
  quoteState.inString = false;
  quoteState.escaped = false;
  for (let index = closeParen + 1; index < source.length; index += 1) {
    const character = source[index];
    if (quoted(source, index, quoteState)) continue;
    if (character === ';') {
      const newline = source.indexOf('\n', index);
      if (newline < 0) break;
      index = newline;
      continue;
    }
    if (character === '{') { openBrace = index; break; }
  }
  if (openBrace < 0) return null;

  let braceDepth = 1;
  quoteState.inString = false;
  quoteState.escaped = false;
  for (let index = openBrace + 1; index < source.length; index += 1) {
    const character = source[index];
    if (quoted(source, index, quoteState)) continue;
    if (character === ';') {
      const newline = source.indexOf('\n', index);
      if (newline < 0) break;
      index = newline;
      continue;
    }
    if (character === '{') braceDepth += 1;
    else if (character === '}' && --braceDepth === 0) {
      return { openLine: lineForOffset(openBrace), closeLine: lineForOffset(index) };
    }
  }
  return null;
}

function decodeLLVMName(name) {
  return name.replace(/\\([0-9a-fA-F]{2})/g, (_match, hex) =>
    String.fromCharCode(parseInt(hex, 16)));
}

function mapInstructions(lines, nodes) {
  const nodeLines = new Map();
  let cursor = 0;
  let complete = true;
  for (const node of nodes) {
    const id = Number(node.id);
    if (!node.instruction) {
      nodeLines.set(id, node.kind === 'init' ? 0 : Math.max(0, Number(node.line)));
      continue;
    }
    const expected = normalizeIR(node.instruction);
    let found = -1;
    for (let line = cursor; line < lines.length; line += 1) {
      if (normalizeIR(lines[line]) === expected) { found = line; break; }
    }
    if (found < 0) {
      complete = false;
      continue;
    }
    nodeLines.set(id, found);
    cursor = found + 1;
  }
  return { complete, nodeLines };
}

function normalizeIR(line) { return String(line).trim().replace(/\s+/g, ' '); }

function simplifyDisplayedIR(line) {
  let text = String(line);
  const commentStart = findLLVMCommentStart(text);
  if (commentStart >= 0) text = text.slice(0, commentStart);

  // Metadata attachments are presentation noise here; retain the instruction
  // and its original line index so graph anchors remain stable.
  text = text.replace(/,\s*![\w.$-]+\s+![\w.$-]+/g, '');
  if (/^\s*!\d+\s*=/.test(text)) return '';

  if (/^\s*br\b/.test(text)) text = text.replace(/\blabel\s+(%)/g, '$1');
  if (/\b(?:load|store)\b/.test(text)) text = text.replace(/,\s*align\s+\d+\b/g, '');
  return text;
}

function findLLVMCommentStart(text) {
  let inString = false;
  let escaped = false;
  for (let index = 0; index < text.length; index += 1) {
    const character = text[index];
    if (inString) {
      if (escaped) escaped = false;
      else if (character === '\\') escaped = true;
      else if (character === '"') inString = false;
    } else if (character === '"') inString = true;
    else if (character === ';') return index;
  }
  return -1;
}

function renderWorkspace() {
  renderFunctionContracts();
  functionGrid.replaceChildren();
  state.models.forEach((model) => functionGrid.append(renderFunctionCard(model)));
}

function renderFunctionContracts() {
  functionContracts.replaceChildren();
  state.graphIRVariables = findIRVariables(state.graphDoc.functions.map((fn) => fn.ir).join('\n'));
  Object.keys(state.graphDoc.globalInitialValues || {}).forEach((name) => state.graphIRVariables.add(name));
  for (const [field, label, ariaLabel] of [
    ['precondition', 'Initial assertion', 'Global initial assertion'],
    ['postcondition', 'Final assertion', 'Global final assertion'],
  ]) {
    const control = document.createElement('label');
    control.className = 'function-contract';
    const title = document.createElement('span');
    title.className = 'function-contract-title';
    title.textContent = label;
    const editorControl = document.createElement('div');
    editorControl.className = 'assertion-editor-control';
    const highlight = document.createElement('div');
    highlight.className = 'assertion-editor-highlight';
    highlight.setAttribute('aria-hidden', 'true');
    const highlightedText = document.createElement('pre');
    highlightedText.className = 'assertion-editor-highlight-text';
    highlight.append(highlightedText);
    const input = document.createElement('textarea');
    input.className = 'assertion-editor-input contract-editor-input';
    input.rows = 2;
    input.setAttribute('aria-label', ariaLabel);
    input.value = state.graphDoc[field];
    appendHighlightedAssertion(highlightedText, input.value, state.graphIRVariables);
    input.addEventListener('scroll', () => {
      highlightedText.style.transform = `translate(${-input.scrollLeft}px, ${-input.scrollTop}px)`;
    });
    input.addEventListener('input', () => {
      highlightedText.replaceChildren();
      appendHighlightedAssertion(highlightedText, input.value, state.graphIRVariables);
      state.graphDoc[field] = input.value;
      if (field === 'precondition') state.graphPreconditionProvided = true;
      clearVerification();
      state.models.forEach(setupOverlay);
    });
    editorControl.append(highlight, input);
    control.append(title, editorControl);
    functionContracts.append(control);
  }
}

function renderFunctionCard(model) {
  const fn = model.function;
  const card = document.createElement('article');
  card.className = 'function-card';
  model.card = card;

  const header = document.createElement('div');
  header.className = 'function-header';
  const title = document.createElement('div');
  title.className = 'function-title';
  const name = document.createElement('h3');
  name.textContent = `@${fn.name}`;
  const signature = document.createElement('p');
  signature.textContent = 'LLVM function · instruction layout preserved';
  title.append(name, signature);

  const stats = document.createElement('div');
  stats.className = 'function-stats';
  const initEdgeCount = fn.edges.filter(isInitEdge).length;
  stats.append(
    stat(`${fn.nodes.length}`, 'nodes'),
    stat(`${fn.edges.length - initEdgeCount}`, 'hyperedges'),
  );
  if (initEdgeCount) stats.append(stat(`${initEdgeCount}`, 'init edges'));
  const headerActions = document.createElement('div');
  headerActions.className = 'function-header-actions';
  const edgeVisibilityButton = document.createElement('button');
  edgeVisibilityButton.className = 'edge-visibility-button';
  edgeVisibilityButton.type = 'button';
  edgeVisibilityButton.title = 'Hold Option (Alt) to temporarily show all dependencies';
  edgeVisibilityButton.addEventListener('click', () => {
    model.altShowAllEdges = false;
    model.showAllEdges = !model.showAllEdges;
    if (!model.showAllEdges && model.selected?.type === 'edge' && !edgeHasAssertion(model, model.selected.id) &&
        !initModeArmed(model)) {
      model.selected = null;
      updateLineHighlights(model);
      updateInspector(model);
    }
    updateEdgeVisibilityControl(model);
    setupOverlay(model);
  });
  model.edgeVisibilityButton = edgeVisibilityButton;
  headerActions.append(stats, edgeVisibilityButton);
  header.append(title, headerActions);
  updateEdgeVisibilityControl(model);

  const viewport = document.createElement('div');
  viewport.className = 'graph-viewport';
  viewport.setAttribute('aria-label', `LLVM IR and dependency graph for ${fn.name}`);
  model.viewport = viewport;
  const scene = document.createElement('div');
  scene.className = 'graph-scene';
  model.scene = scene;
  const pre = document.createElement('pre');
  pre.className = 'ir-code';
  pre.setAttribute('aria-label', `LLVM IR for ${fn.name}`);
  model.pre = pre;
  const code = document.createElement('code');
  model.lineNodes.clear();
  model.lines.forEach((line, lineIndex) => {
    const lineNode = document.createElement('span');
    lineNode.className = 'ir-line';
    lineNode.dataset.line = String(lineIndex);
    lineNode.dataset.lineNumber = String(lineIndex + 1);
    const displayedLine = simplifyDisplayedIR(line);
    lineNode.textContent = displayedLine || '\u200b';
    code.append(lineNode);
    model.lineNodes.set(lineIndex, lineNode);
  });
  pre.append(code);
  const labelLayer = document.createElement('div');
  labelLayer.className = 'edge-label-layer';
  model.labelLayer = labelLayer;
  scene.append(pre, labelLayer);
  viewport.append(scene);

  const byLine = new Map();
  for (const node of fn.nodes) {
    const line = model.nodeLines.get(Number(node.id));
    if (!Number.isInteger(line) || line < 0 || line >= model.lines.length) continue;
    if (!byLine.has(line)) byLine.set(line, []);
    byLine.get(line).push(node);
  }
  model.lineNodes.forEach((lineNode, line) => {
    const nodes = byLine.get(line) || [];
    if (!nodes.length) return;
    lineNode.classList.add('is-node');
    lineNode.setAttribute('role', 'button');
    lineNode.tabIndex = 0;
    const layout = getFunctionLayout(fn.name);
    if (nodes.some((node) => layout.nodes[String(node.id)]?.label)) lineNode.classList.add('node-labeled');
    lineNode.addEventListener('click', () => selectLine(model, nodes[0]));
    lineNode.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        selectLine(model, nodes[0]);
      }
    });
    lineNode.addEventListener('mouseenter', () => previewInitEdge(model, nodes[0]));
    lineNode.addEventListener('mouseleave', () => setInitPreview(model, null));
  });

  const inspector = createInspector(model);
  const legend = createLegend(fn.edges);
  card.append(header, viewport, inspector, legend);
  if (model.warning) {
    const warning = document.createElement('div');
    warning.className = 'mapping-warning';
    warning.textContent = model.warning;
    card.append(warning);
  }

  requestAnimationFrame(() => setupOverlay(model));
  return card;
}

function stat(value, label) {
  const chip = document.createElement('span');
  chip.className = 'stat-chip';
  const count = document.createElement('b');
  count.textContent = value;
  chip.append(count, document.createTextNode(` ${label}`));
  return chip;
}

function createInspector(model) {
  const inspector = document.createElement('div');
  inspector.className = 'inspector';
  inspector.dataset.inspector = '';
  model.inspector = inspector;
  updateInspector(model);
  return inspector;
}

function updateInspector(model) {
  const inspector = model.inspector;
  if (!inspector) return;
  inspector.replaceChildren();
  const selected = model.selected;
  inspector.classList.toggle('has-edge-selection', selected?.type === 'edge');
  const summary = document.createElement('div');
  summary.className = 'inspector-summary';
  const icon = document.createElement('span');
  icon.className = `selection-icon${selected?.type === 'edge' ? ' edge' : ''}`;
  icon.textContent = selected?.type === 'edge' ? '↗' : selected ? '•' : '＋';
  const copy = document.createElement('div');
  copy.className = 'selection-copy';
  const heading = document.createElement('strong');
  const detail = document.createElement('span');
  if (!selected) {
    heading.textContent = 'Select an instruction or hyperedge';
    detail.textContent = 'Select an edge to edit its assertion below';
  } else if (selected.type === 'node') {
    const node = model.function.nodes.find((entry) => Number(entry.id) === Number(selected.id));
    heading.textContent = `${node?.kind ?? 'instruction'} node n${selected.id}`;
    detail.textContent = node?.kind === 'init'
      ? 'Click an instruction to select its init edge · Esc to leave'
      : node?.instruction || 'Function boundary';
    const result = verificationOf(model)?.nodes.get(Number(selected.id));
    if (result && node?.kind === 'instruction') {
      detail.textContent += ` · node condition ${result.status}`;
      if (result.status !== 'proved' && result.detail) detail.textContent += ` (${result.detail})`;
    }
  } else {
    const edge = model.function.edges.find((entry) => String(entry.id) === String(selected.id));
    heading.textContent = `${edge?.kind ?? 'dependency'} hyperedge`;
    const interferences = edge?.interferences || [];
    detail.textContent = `${(edge?.sources || []).map((source) => `n${source}`).join(' + ')} → n${edge?.target ?? '?'}` +
      ` · ${interferences.length ? `interferes with ${interferences.map((node) => `n${node}`).join(', ')}` : 'no interferences'}`;
    const failed = edge ? failedInterferenceChecks(model, edge) : [];
    if (failed.length) detail.textContent += ` · interference fails at ${failed.map(describeCheck).join('; ')}`;
    else if (verificationOf(model)?.nodesProved && interferences.length) detail.textContent += ' · interference proved';
  }
  copy.append(heading, detail);
  summary.append(icon, copy);
  inspector.append(summary);

  if (selected?.type === 'edge') {
    const edge = model.function.edges.find((entry) => String(entry.id) === String(selected.id));
    if (!edge) return;
    const incoming = incomingAssertionsOf(edge);
    if (!incoming) {
      inspector.append(createAssertionEditor(model, 'Assertion', `Assertion for ${edge.kind} hyperedge`,
        edgeLabelText(model, edge), (value) => { editedEdge(model, edge).label = value; }));
      return;
    }
    // A phi takes its value from a different source on each path, so each
    // predecessor gets its own assertion.
    const editors = document.createElement('div');
    editors.className = 'assertion-editors';
    for (const path of incoming) {
      editors.append(createAssertionEditor(model, `from ${path.predecessor} (n${path.source})`,
        `Assertion for the path from ${path.predecessor}`, incomingAssertionText(model, edge, path),
        (value) => {
          const edited = editedEdge(model, edge);
          edited.incomingLabels ||= {};
          edited.incomingLabels[path.predecessor] = value;
        }));
    }
    inspector.append(editors);
  }
}

function createAssertionEditor(model, title, ariaLabel, value, save) {
  const editor = document.createElement('label');
  editor.className = 'assertion-editor';
  const editorTitle = document.createElement('span');
  editorTitle.className = 'assertion-editor-title';
  editorTitle.textContent = title;
  const editorControl = document.createElement('div');
  editorControl.className = 'assertion-editor-control';
  const highlight = document.createElement('div');
  highlight.className = 'assertion-editor-highlight';
  highlight.setAttribute('aria-hidden', 'true');
  const highlightedText = document.createElement('pre');
  highlightedText.className = 'assertion-editor-highlight-text';
  highlight.append(highlightedText);
  const input = document.createElement('textarea');
  input.className = 'assertion-editor-input';
  input.rows = 2;
  input.maxLength = 100;
  input.setAttribute('aria-label', ariaLabel);
  input.value = value;
  appendHighlightedAssertion(highlightedText, input.value, model.irVariables);
  input.addEventListener('scroll', () => {
    highlightedText.style.transform = `translate(${-input.scrollLeft}px, ${-input.scrollTop}px)`;
  });
  input.addEventListener('input', () => {
    highlightedText.replaceChildren();
    appendHighlightedAssertion(highlightedText, input.value, model.irVariables);
    save(input.value);
    clearVerification();
    updateEdgeVisibilityControl(model);
    setupOverlay(model);
  });
  editorControl.append(highlight, input);
  editor.append(editorTitle, editorControl);
  return editor;
}

function createLegend(edges) {
  const legend = document.createElement('div');
  legend.className = 'edge-legend';
  const kinds = [...new Set(edges.map((edge) => edge.kind))];
  if (!kinds.length) {
    const item = document.createElement('span');
    item.className = 'legend-item';
    item.textContent = 'No dependency hyperedges';
    legend.append(item);
    return legend;
  }
  for (const kind of kinds) {
    const item = document.createElement('span');
    item.className = 'legend-item';
    const swatch = document.createElement('i');
    swatch.className = 'legend-swatch';
    swatch.dataset.kind = kind;
    swatch.style.background = edgeColor(kind);
    const text = document.createElement('span');
    text.textContent = kind;
    item.append(swatch, text);
    legend.append(item);
  }
  return legend;
}

// A data edge into a phi keeps one assertion per incoming predecessor instead
// of a single one.
function incomingAssertionsOf(edge) {
  return Array.isArray(edge?.incomingAssertions) && edge.incomingAssertions.length
    ? edge.incomingAssertions
    : null;
}

function editedEdge(model, edge) {
  const edges = getFunctionLayout(model.function.name).edges;
  edges[String(edge.id)] ||= {};
  return edges[String(edge.id)];
}

function edgeLabelText(model, edge) {
  const savedLabel = getFunctionLayout(model.function.name).edges[String(edge.id)]?.label;
  return String(savedLabel ?? edge.assertion ?? '');
}

function incomingAssertionText(model, edge, path) {
  const saved = getFunctionLayout(model.function.name).edges[String(edge.id)]
    ?.incomingLabels?.[path.predecessor];
  return String(saved ?? path.assertion ?? '');
}

// The non-empty assertions shown on an edge's label, each tagged with its
// predecessor on a phi edge.
function edgeAssertionLines(model, edge) {
  const incoming = incomingAssertionsOf(edge);
  if (!incoming) {
    const text = edgeLabelText(model, edge).trim();
    return text ? [{ tag: null, text }] : [];
  }
  return incoming
    .map((path) => ({ tag: path.predecessor, text: incomingAssertionText(model, edge, path).trim() }))
    .filter((line) => line.text);
}

function edgeHasAssertion(model, edgeId) {
  const edge = model.function.edges.find((candidate) => String(candidate.id) === String(edgeId));
  return Boolean(edge) && edgeAssertionLines(model, edge).length > 0;
}

function isInitEdge(edge) {
  return edge?.kind === 'init';
}

// Init edges reach every instruction, so showing all dependencies leaves them
// out. They appear when annotated, selected, or previewed in init mode.
function edgeIsVisible(model, edge) {
  if (isSelected(model, 'edge', edge.id) || isInitPreview(model, edge) ||
      edgeHasAssertion(model, edge.id) || failedInterferenceChecks(model, edge).length)
    return true;
  return !isInitEdge(edge) && (model.showAllEdges || model.altShowAllEdges);
}

function isInitPreview(model, edge) {
  return model.initPreview !== null && String(model.initPreview) === String(edge.id);
}

function updateEdgeVisibilityControl(model) {
  const button = model.edgeVisibilityButton;
  if (!button) return;
  const hiddenCount = model.function.edges
    .filter((edge) => !isInitEdge(edge) && !edgeHasAssertion(model, edge.id)).length;
  button.hidden = hiddenCount === 0;
  button.textContent = model.showAllEdges
    ? 'Hide unannotated dependencies'
    : `Show all dependencies (${hiddenCount} hidden)`;
  button.setAttribute('aria-pressed', String(model.showAllEdges));
}

function updateInstructionSpacing(model) {
  const endpointCounts = new Map();
  const addEndpoint = (nodeId) => {
    const id = Number(nodeId);
    endpointCounts.set(id, (endpointCounts.get(id) || 0) + 1);
  };
  for (const edge of model.function.edges) {
    // A hover preview must not move the line under the pointer.
    if (!edgeIsVisible(model, edge) || isInitPreview(model, edge)) continue;
    edge.sources.forEach(addEndpoint);
    addEndpoint(edge.target);
  }

  const lineCounts = new Map();
  for (const node of model.function.nodes) {
    if (node.kind !== 'instruction') continue;
    const line = model.nodeLines.get(Number(node.id));
    if (!Number.isInteger(line)) continue;
    lineCounts.set(line, (lineCounts.get(line) || 0) + (endpointCounts.get(Number(node.id)) || 0));
  }
  model.lineNodes.forEach((lineNode, line) => {
    const degree = lineCounts.get(line) || 0;
    const extraSpacing = Math.min(48, Math.max(0, degree - 1) * EDGE_ENDPOINT_BASE_SPACING * 2);
    lineNode.style.lineHeight = `${19 + extraSpacing}px`;
  });
}

function setupOverlay(model) {
  if (!model.card?.isConnected) return;
  model.svg?.remove();
  updateInstructionSpacing(model);
  model.codeWidth = Math.max(280, model.pre.offsetWidth);
  model.sceneHeight = Math.max(model.pre.offsetHeight, 150);

  const sceneRect = model.scene.getBoundingClientRect();
  model.anchors.clear();
  for (const node of model.function.nodes) {
    const line = model.nodeLines.get(Number(node.id));
    const lineNode = model.lineNodes.get(line);
    if (!lineNode) continue;
    const lineRect = lineNode.getBoundingClientRect();
    model.anchors.set(Number(node.id), {
      x: 0,
      y: lineRect.top - sceneRect.top + lineRect.height / 2,
    });
  }

  const candidates = model.function.edges.map((edge, index) => {
      const sources = edge.sources.map((id, sourceIndex) => ({
        sourceIndex,
        nodeId: Number(id),
        anchor: model.anchors.get(Number(id)),
        offset: 0,
      })).filter((source) => source.anchor);
      const target = model.anchors.get(Number(edge.target));
      if (!target) return null;
      const endpoints = [...sources.map((source) => source.anchor), target];
      const startY = Math.min(...endpoints.map((anchor) => anchor.y));
      const endY = Math.max(...endpoints.map((anchor) => anchor.y));
      return { edge, index, sources, target, targetOffset: 0, startY, endY };
    }).filter(Boolean).sort((a, b) => a.startY - b.startY || b.endY - a.endY || a.index - b.index);

  // Reserve enough horizontal room for the full graph so hiding edges never
  // moves the code, but pack visible edges independently below.
  // Unannotated init edges share one extra lane, since init mode shows at most
  // one of them.
  const reservedLaneEnds = [];
  const reserved = candidates.filter((track) =>
    !isInitEdge(track.edge) || edgeHasAssertion(model, track.edge.id));
  for (const track of reserved) {
    let lane = reservedLaneEnds.findIndex((endY) => endY < track.startY);
    if (lane < 0) lane = reservedLaneEnds.length;
    reservedLaneEnds[lane] = track.endY;
  }
  if (reserved.length < candidates.length) reservedLaneEnds.push(Infinity);
  const visibleCandidates = candidates.filter((track) => edgeIsVisible(model, track.edge));

  const endpointsByNode = new Map();
  const addEndpoint = (nodeId, endpoint) => {
    if (!endpointsByNode.has(nodeId)) endpointsByNode.set(nodeId, []);
    endpointsByNode.get(nodeId).push(endpoint);
  };
  for (const track of visibleCandidates) {
    for (const source of track.sources) addEndpoint(source.nodeId, { source });
    addEndpoint(Number(track.edge.target), { track });
  }
  for (const endpoints of endpointsByNode.values()) {
    // Incoming edges connect above outgoing ones.
    endpoints.sort((a, b) => Number(Boolean(b.track)) - Number(Boolean(a.track)));
    const offsets = [0];
    for (let index = 1; index < endpoints.length; index += 1) {
      const previous = endpoints[index - 1];
      const current = endpoints[index];
      const spacing = previous.track && current.track
        ? EDGE_ENDPOINT_SPACING
        : EDGE_ENDPOINT_BASE_SPACING;
      offsets.push(offsets[index - 1] + spacing);
    }
    const midpoint = (offsets[0] + offsets[offsets.length - 1]) / 2;
    endpoints.forEach((endpoint, index) => {
      const offset = offsets[index] - midpoint;
      if (endpoint.source) endpoint.source.offset = offset;
      else endpoint.track.targetOffset = offset;
    });
  }
  for (const track of visibleCandidates) {
    const endpointYs = [
      ...track.sources.map((source) => source.anchor.y + source.offset),
      track.target.y + track.targetOffset,
    ];
    track.startY = Math.min(...endpointYs);
    track.endY = Math.max(...endpointYs);
  }
  visibleCandidates.sort((a, b) => a.startY - b.startY || b.endY - a.endY || a.index - b.index);

  const laneEnds = [];
  for (const track of visibleCandidates) {
    let lane = laneEnds.findIndex((endY) => endY < track.startY);
    if (lane < 0) lane = laneEnds.length;
    laneEnds[lane] = track.endY;
    track.lane = lane;
  }

  const layout = getFunctionLayout(model.function.name);
  const firstTrackX = 12 + EDGE_LABEL_MAX_WIDTH;
  model.codeLeft = reservedLaneEnds.length
    ? firstTrackX + 14 + (reservedLaneEnds.length - 1) * EDGE_LANE_SPACING
    : 0;
  model.pre.style.marginLeft = `${model.codeLeft}px`;
  const hasNodeNotes = model.function.nodes.some((node) =>
    getFunctionLayout(model.function.name).nodes[String(node.id)]?.label?.trim());
  const rightGutter = hasNodeNotes ? 210 : 24;
  model.sceneWidth = Math.max(model.codeLeft + model.codeWidth + rightGutter, model.viewport.clientWidth);
  model.scene.style.width = `${model.sceneWidth}px`;
  model.scene.style.height = `${model.sceneHeight}px`;
  model.svg = document.createElementNS(NS, 'svg');
  model.svg.classList.add('edge-overlay');
  model.svg.setAttribute('width', String(model.sceneWidth));
  model.svg.setAttribute('height', String(model.sceneHeight));
  model.svg.setAttribute('viewBox', `0 0 ${model.sceneWidth} ${model.sceneHeight}`);
  model.svg.setAttribute('aria-label', `Editable dependency hyperedges for ${model.function.name}`);
  model.scene.append(model.svg);

  for (const anchor of model.anchors.values()) anchor.x = model.codeLeft - 2;
  for (const track of candidates) {
    track.x = firstTrackX + track.lane * EDGE_LANE_SPACING;
  }

  model.edgeTracks = new Map();
  for (const track of visibleCandidates) {
    const trackTop = track.startY;
    const trackBottom = track.endY;
    const edgeLayout = layout.edges[String(track.edge.id)] || {};
    delete edgeLayout.x;
    edgeLayout.y = clamp(Number.isFinite(edgeLayout.y)
      ? edgeLayout.y
      : (trackTop + trackBottom) / 2,
    trackTop, trackBottom);
    layout.edges[String(track.edge.id)] = edgeLayout;
    track.trackTop = trackTop;
    track.trackBottom = trackBottom;
    model.edgeTracks.set(String(track.edge.id), track);
  }

  drawOverlay(model);

  if (!model.resizeObserver) {
    model.resizeObserver = new ResizeObserver(() => {
      requestAnimationFrame(() => setupOverlay(model));
    });
    model.resizeObserver.observe(model.viewport);
  }
}

function drawOverlay(model) {
  if (!model.svg || !model.svg.isConnected) return;
  model.svg.replaceChildren();
  model.labelLayer.replaceChildren();
  const layout = getFunctionLayout(model.function.name);
  const tracks = [...model.edgeTracks.values()];
  tracks.sort((a, b) =>
    Number(isEdgeDeemphasized(model, b.edge)) - Number(isEdgeDeemphasized(model, a.edge)) ||
    Number(isSelected(model, 'edge', a.edge.id)) - Number(isSelected(model, 'edge', b.edge.id)));
  const leftMostTrackX = tracks.length ? Math.min(...tracks.map((track) => track.x)) : 0;
  const labelRight = (leftMostTrackX || 12 + EDGE_LABEL_MAX_WIDTH) - EDGE_TRACK_LABEL_GAP;

  for (const track of tracks) {
    const { edge, sources, target, targetOffset } = track;
    const color = edgeColor(edge.kind);

    const edgeLayout = layout.edges[String(edge.id)];
    const assertionLines = edgeAssertionLines(model, edge);
    const lineText = (line) => (line.tag ? `${line.tag}: ${line.text}` : line.text);
    const longestLine = assertionLines.map(lineText).reduce((a, b) => (b.length > a.length ? b : a), '');
    const kindText = String(edge.kind || 'dependency');
    const hasAssertion = assertionLines.length > 0;
    const showKind = !hasAssertion;
    const showAssertion = hasAssertion;
    const fullLabel = showAssertion ? assertionLines.map(lineText).join('\n') : kindText;
    const labelWidth = edgeLabelWidth(edge.kind, longestLine, showKind, showAssertion);
    const labelY = edgeLayout.y;
    const labelLeft = labelRight - labelWidth;

    const edgePath = [`M ${track.x} ${track.trackTop} V ${track.trackBottom}`];
    for (const source of sources) {
      const sourceY = source.anchor.y + source.offset;
      edgePath.push(`M ${track.x} ${sourceY} H ${source.anchor.x}`);
    }
    const targetY = target.y + targetOffset;
    const arrowBaseX = target.x - 6;
    edgePath.push(
      `M ${track.x} ${targetY} H ${arrowBaseX}`,
      `M ${arrowBaseX} ${targetY - 3} L ${target.x} ${targetY} L ${arrowBaseX} ${targetY + 3} Z`,
    );
    addEdgePath(model, edge, edgePath.join(' '), color);
    model.svg.append(svgElement('path', {
      d: `M ${labelLeft + labelWidth} ${labelY} L ${track.x} ${labelY}`,
      class: `edge-label-tether${isEdgeDeemphasized(model, edge) ? ' muted' : ''}`,
      stroke: color,
    }));

    const labelFrame = document.createElement('div');
    labelFrame.className = `edge-label-frame${isSelected(model, 'edge', edge.id) ? ' selected' : ''}`;
    if (isEdgeDeemphasized(model, edge)) labelFrame.classList.add('muted');
    if (failedInterferenceChecks(model, edge).length) labelFrame.classList.add('vc-failed');
    labelFrame.dataset.edgeId = String(edge.id);
    labelFrame.setAttribute('role', 'group');
    labelFrame.setAttribute('aria-label', `Select or move the ${edge.kind} dependency label along its edge`);
    labelFrame.tabIndex = 0;
    labelFrame.style.left = `${labelLeft}px`;
    labelFrame.style.top = `${labelY}px`;
    labelFrame.style.width = `${labelWidth}px`;
    labelFrame.title = edge.values?.length
      ? `${fullLabel} · ${edge.values.join(', ')}`
      : fullLabel;
    labelFrame.addEventListener('pointerdown', (event) => {
      if (event.button !== 0) return;
      event.preventDefault();
      event.stopPropagation();
      if (model.viewport.setPointerCapture) model.viewport.setPointerCapture(event.pointerId);
      const pointer = clientPointInScene(model.scene, event);
      state.drag = {
        model,
        edge,
        trackTop: track.trackTop,
        trackBottom: track.trackBottom,
        lastEvent: event,
        pointerId: event.pointerId,
        offsetY: labelY - pointer.y,
        startY: labelY,
        labelWidth,
        moved: false,
        frame: 0,
      };
    });
    labelFrame.addEventListener('keydown', (event) => {
      if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
        event.preventDefault();
        const step = event.shiftKey ? 10 : 2;
        edgeLayout.y = clamp(labelY + (event.key === 'ArrowUp' ? -step : step), track.trackTop, track.trackBottom);
        selectEntity(model, 'edge', edge.id);
        focusEdgeLabelFrame(model, edge.id);
        return;
      }
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        if (model.altShowAllEdges && !edgeHasAssertion(model, edge.id)) {
          selectUnannotatedEdge(model, edge);
        } else {
          selectEntity(model, 'edge', edge.id);
        }
        focusEdgeLabelFrame(model, edge.id);
      }
    });

    if (showKind) {
      const kindLabel = document.createElement('span');
      kindLabel.className = 'edge-kind-label';
      kindLabel.textContent = kindText;
      kindLabel.style.color = color;
      labelFrame.append(kindLabel);
    }
    if (showAssertion) {
      const assertionLabel = document.createElement('span');
      assertionLabel.className = 'edge-assertion-label';
      if (incomingAssertionsOf(edge)) assertionLabel.classList.add('incoming');
      for (const line of assertionLines) {
        const labelText = document.createElement('span');
        labelText.className = 'edge-label-text';
        if (line.tag) {
          const tag = document.createElement('span');
          tag.className = 'edge-label-tag';
          tag.textContent = line.tag;
          labelText.append(tag);
        }
        appendHighlightedAssertion(labelText, line.text, model.irVariables);
        assertionLabel.append(labelText);
      }
      labelFrame.append(assertionLabel);
    }
    model.labelLayer.append(labelFrame);
  }

  for (const node of model.function.nodes) {
    if (node.kind !== 'init' && node.kind !== 'exit') continue;
    const anchor = model.anchors.get(Number(node.id));
    if (!anchor) continue;
    const isInit = node.kind === 'init';
    const condition = state.graphDoc[isInit ? 'precondition' : 'postcondition'] || 'true';
    const label = document.createElement('div');
    label.className = `contract-node-label ${isInit ? 'initial' : 'final'}`;
    label.style.left = `${labelRight - EDGE_LABEL_MAX_WIDTH}px`;
    label.style.top = `${anchor.y}px`;
    label.style.width = `${EDGE_LABEL_MAX_WIDTH}px`;
    label.setAttribute('aria-label', `${isInit ? 'Initial' : 'Final'} assertion: ${condition}`);
    const kindLabel = document.createElement('span');
    kindLabel.className = 'contract-node-label-kind';
    kindLabel.textContent = isInit ? 'pre' : 'post';
    const conditionLabel = document.createElement('span');
    conditionLabel.className = 'edge-label-text';
    appendHighlightedAssertion(conditionLabel, condition, state.graphIRVariables);
    label.append(kindLabel, conditionLabel);
    model.labelLayer.append(label);
  }

  for (const node of model.function.nodes) {
    const anchor = model.anchors.get(Number(node.id));
    const customLabel = layout.nodes[String(node.id)]?.label?.trim();
    if (!anchor || !customLabel) continue;
    const labelWidth = Math.min(190, customLabel.length * 5.4 + 13);
    const group = svgElement('g', { class: 'node-note', transform: `translate(${model.codeLeft + model.codeWidth + 2} ${anchor.y - 9})` });
    group.append(svgElement('rect', { x: 0, y: 0, width: labelWidth, height: 18, rx: 5, class: 'node-note-bg' }));
    const text = svgElement('text', { x: 6, y: 12 });
    text.textContent = customLabel.length > 31 ? `${customLabel.slice(0, 28)}…` : customLabel;
    group.append(text);
    model.svg.append(group);
  }
}

function addEdgePath(model, edge, d, color) {
  const muted = isEdgeDeemphasized(model, edge);
  const hit = svgElement('path', {
    d, class: `edge-hit${isSelected(model, 'edge', edge.id) ? ' selected' : ''}`,
  });
  hit.addEventListener('click', () => {
    if (model.altShowAllEdges && !edgeHasAssertion(model, edge.id)) {
      selectUnannotatedEdge(model, edge);
    } else {
      selectEntity(model, 'edge', edge.id);
    }
  });
  model.svg.append(hit);
  const visible = svgElement('path', {
    d,
    class: `edge-visible${isSelected(model, 'edge', edge.id) ? ' selected' : ''}${muted ? ' muted' : ''}` +
      `${failedInterferenceChecks(model, edge).length ? ' vc-failed' : ''}`,
    stroke: color,
    fill: color,
  });
  model.svg.append(visible);
}

function isEdgeDeemphasized(model, edge) {
  return (model.showAllEdges || model.altShowAllEdges) && edgeHasAssertion(model, edge.id);
}

function selectUnannotatedEdge(model, edge) {
  model.altShowAllEdges = false;
  model.showAllEdges = false;
  updateEdgeVisibilityControl(model);
  selectEntity(model, 'edge', edge.id);
}

function selectEntity(model, type, id) {
  model.selected = { type, id };
  updateLineHighlights(model);
  updateInspector(model);
  setupOverlay(model);
}

function clearSelection(model) {
  model.selected = null;
  model.initPreview = null;
  updateLineHighlights(model);
  updateInspector(model);
  setupOverlay(model);
}

// Init mode is armed while the init node or an init edge is selected. Clicking
// an instruction then selects its init edge, and clicking init leaves.
function initModeArmed(model) {
  const selected = model.selected;
  if (selected?.type === 'node')
    return model.function.nodes.find((node) => Number(node.id) === Number(selected.id))?.kind === 'init';
  return selected?.type === 'edge' &&
    isInitEdge(model.function.edges.find((edge) => String(edge.id) === String(selected.id)));
}

function initEdgeFor(model, node) {
  if (!initModeArmed(model) || node.kind === 'init') return null;
  return model.function.edges.find((edge) => isInitEdge(edge) && Number(edge.target) === Number(node.id));
}

function selectLine(model, node) {
  if (initModeArmed(model) && node.kind === 'init') {
    clearSelection(model);
    return;
  }
  const edge = initEdgeFor(model, node);
  model.initPreview = null;
  if (edge) selectEntity(model, 'edge', edge.id);
  else selectEntity(model, 'node', Number(node.id));
}

function previewInitEdge(model, node) {
  const edge = initEdgeFor(model, node);
  setInitPreview(model, edge && !isSelected(model, 'edge', edge.id) ? edge.id : null);
}

function setInitPreview(model, edgeId) {
  if (model.initPreview === edgeId) return;
  model.initPreview = edgeId;
  updateLineHighlights(model);
  setupOverlay(model);
}

// Marks the selected instruction, or the instructions that interfere with the
// selected or previewed dependency. An init edge also marks its target.
// Verification results mark failed node conditions, and for a selected
// dependency, the instructions whose interference check failed.
function updateLineHighlights(model) {
  const selected = model.selected;
  model.card?.classList.toggle('init-armed', initModeArmed(model));
  model.lineNodes.forEach((line) => {
    line.classList.remove('selected', 'interferes', 'vc-failed', 'interference-failed');
    line.removeAttribute('title');
  });
  for (const node of model.function.nodes) {
    if (!nodeVcFailed(model, node.id)) continue;
    const line = model.lineNodes.get(model.nodeLines.get(Number(node.id)));
    const result = verificationOf(model).nodes.get(Number(node.id));
    line?.classList.add('vc-failed');
    line?.setAttribute('title', `Node condition ${result.status}${result.detail ? `: ${result.detail}` : ''}`);
  }
  const edgeId = model.initPreview ?? (selected?.type === 'edge' ? selected.id : null);
  if (selected?.type === 'node' && edgeId === null) {
    const line = model.nodeLines.get(Number(selected.id));
    model.lineNodes.get(line)?.classList.add('selected');
  } else if (edgeId !== null) {
    const edge = model.function.edges.find((entry) => String(entry.id) === String(edgeId));
    if (isInitEdge(edge))
      model.lineNodes.get(model.nodeLines.get(Number(edge.target)))?.classList.add('selected');
    for (const nodeId of edge?.interferences || []) {
      const line = model.nodeLines.get(Number(nodeId));
      model.lineNodes.get(line)?.classList.add('interferes');
    }
    for (const check of edge ? failedInterferenceChecks(model, edge) : []) {
      const line = model.lineNodes.get(model.nodeLines.get(Number(check.node)));
      line?.classList.add('interference-failed');
      line?.setAttribute('title', `Interference ${describeCheck(check)}`);
    }
  }
}

function isSelected(model, type, id) {
  return model.selected?.type === type && String(model.selected.id) === String(id);
}

function focusEdgeLabelFrame(model, edgeId) {
  const frame = [...model.labelLayer.querySelectorAll('.edge-label-frame')]
    .find((candidate) => candidate.dataset.edgeId === String(edgeId));
  frame?.focus();
}

function getFunctionLayout(name) {
  if (!state.layoutDoc.functions[name])
    state.layoutDoc.functions[name] = { nodes: {}, edges: {} };
  const fn = state.layoutDoc.functions[name];
  fn.nodes ||= {};
  fn.edges ||= {};
  return fn;
}

function edgeColor(kind = '') {
  const label = String(kind).toLowerCase();
  if (document.documentElement.dataset.theme !== 'dark') {
    if (label.includes('data')) return '#4d60b0';
    if (label.includes('acquire')) return '#14745e';
    if (label.includes('release')) return '#9a5a11';
    if (label.includes('seq_cst') || label.includes('seq')) return '#a64259';
    return '#705493';
  }
  if (label.includes('data')) return '#8298ff';
  if (label.includes('acquire')) return '#52bca2';
  if (label.includes('release')) return '#e5a34f';
  if (label.includes('seq_cst') || label.includes('seq')) return '#df778c';
  return '#b099ed';
}

function svgElement(name, attributes = {}) {
  const element = document.createElementNS(NS, name);
  for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, String(value));
  return element;
}

function clamp(value, low, high) {
  return Math.max(low, Math.min(high, value));
}

function saveLayout() {
  state.layoutDoc.format = 'depvec-layout-v1';
  state.layoutDoc.module = state.graphDoc?.module || '';
  state.layoutDoc.sourceFile = state.irFile?.name || '';
  const blob = new Blob([`${JSON.stringify(state.layoutDoc, null, 2)}\n`], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  const base = (state.irFile?.name || 'depvec').replace(/\.(ll|ir)$/i, '');
  link.href = url;
  link.download = `${base}.depvec-layout.json`;
  link.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  setStatus('Saved editable labels and their positions along the edge tracks.');
}

// The graph document with the current edge assertions.
function currentGraphDocument() {
  const graph = JSON.parse(JSON.stringify(state.graphDoc));
  graph.precondition ??= '';
  graph.postcondition ??= '';
  graph.globalInitialValues ??= {};
  for (const fn of graph.functions) {
    const editedEdges = state.layoutDoc.functions?.[fn.name]?.edges || {};
    for (const edge of fn.edges) {
      const edited = editedEdges[String(edge.id)];
      const editedAssertion = edited?.label;
      edge.assertion = editedAssertion === undefined
        ? String(edge.assertion || '').trim()
        : String(editedAssertion).trim();
      for (const path of incomingAssertionsOf(edge) || []) {
        const editedPath = edited?.incomingLabels?.[path.predecessor];
        path.assertion = String(editedPath ?? path.assertion ?? '').trim();
      }
      if (!Array.isArray(edge.interferences)) edge.interferences = [];
    }
  }
  return graph;
}

function exportGraph() {
  if (!state.graphDoc) return;
  const graph = currentGraphDocument();
  const blob = new Blob([`${JSON.stringify(graph, null, 2)}\n`], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  const base = (state.irFile?.name || state.graphFile?.name || 'depvec')
    .replace(/\.(ll|ir|json)$/i, '');
  link.href = url;
  link.download = `${base}.depvec-graph.json`;
  link.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  setStatus('Exported the graph with the current edge assertions.');
}

async function verifyGraph() {
  if (!state.graphDoc) return;
  verifyButton.disabled = true;
  setStatus('Verifying with the backend…');
  try {
    const response = await fetch('/api/verify', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(currentGraphDocument()),
    });
    const result = await response.json().catch(() => ({}));
    if (!response.ok || result.format !== 'depvec-results-v1') {
      throw new Error(result.error || `The backend request failed (${response.status}).`);
    }
    state.verification = new Map(result.functions.map((fn) => [fn.name, {
      nodesProved: fn.nodesProved,
      nodes: new Map(fn.nodes.map((node) => [Number(node.id), node])),
      edges: new Map(fn.edges.map((edge) => [String(edge.id), edge.interferenceChecks])),
    }]));
    setStatus(verificationSummary());
  } catch (error) {
    state.verification = null;
    setStatus(`Verification failed: ${error.message}`, true);
  } finally {
    verifyButton.disabled = !state.graphDoc;
    refreshVerificationMarks();
  }
}

function clearVerification() {
  if (!state.verification) return;
  state.verification = null;
  setStatus('Assertions changed. Verify again to update the results.');
  refreshVerificationMarks();
}

function refreshVerificationMarks() {
  for (const model of state.models) {
    updateLineHighlights(model);
    updateInspector(model);
    updateEdgeVisibilityControl(model);
    setupOverlay(model);
  }
}

function verificationSummary() {
  const parts = state.models.map((model) => {
    const name = `@${model.function.name}`;
    const failedNodes = model.function.nodes.filter((node) => nodeVcFailed(model, node.id)).length;
    if (failedNodes) return `${name}: ${failedNodes} node condition${failedNodes === 1 ? '' : 's'} failed`;
    const failedEdges = model.function.edges.filter((edge) => failedInterferenceChecks(model, edge).length).length;
    if (failedEdges) return `${name}: ${failedEdges} dependenc${failedEdges === 1 ? 'y' : 'ies'} with failed interference checks`;
    return `${name}: all proved`;
  });
  return `Verified. ${parts.join('; ')}.`;
}

function verificationOf(model) {
  return state.verification?.get(model.function.name) ?? null;
}

// Instruction nodes, branches included, must be proved; init and exit nodes
// have no verification condition.
function nodeVcFailed(model, nodeId) {
  const result = verificationOf(model)?.nodes.get(Number(nodeId));
  const node = model.function.nodes.find((entry) => Number(entry.id) === Number(nodeId));
  return Boolean(result) && node?.kind === 'instruction' && result.status !== 'proved';
}

// The backend checks interference only once every node is proved.
function failedInterferenceChecks(model, edge) {
  const verification = verificationOf(model);
  if (!verification?.nodesProved) return [];
  return (verification.edges.get(String(edge.id)) || []).filter((check) => check.status !== 'proved');
}

function describeCheck(check) {
  const path = check.predecessor ? ` on the path from ${check.predecessor}` : '';
  return `n${check.node}${path}: ${check.status}${check.detail ? ` (${check.detail})` : ''}`;
}
