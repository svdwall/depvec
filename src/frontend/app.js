const $ = (selector) => document.querySelector(selector);
const NS = 'http://www.w3.org/2000/svg';
const EDGE_LABEL_MAX_WIDTH = 180;
const EDGE_LANE_SPACING = 16;
const EDGE_TRACK_LABEL_GAP = 8;
const EDGE_ENDPOINT_SPACING = 2;

function edgeLabelWidth(kind, assertion) {
  const kindWidth = Math.min(72, String(kind || 'dependency').length * 6.1);
  const assertionWidth = Math.max(38, (String(assertion || '').length + 4) * 6.1);
  return Math.min(EDGE_LABEL_MAX_WIDTH, 14 + kindWidth + 8 + assertionWidth);
}

const state = {
  irFile: null,
  irText: '',
  graphFile: null,
  graphDoc: null,
  layoutDoc: { format: 'depvec-layout-v1', functions: {} },
  models: [],
  drag: null,
  suppressEdgeTextClick: null,
};

const irInput = $('#ir-file');
const graphInput = $('#graph-file');
const layoutInput = $('#layout-file');
const statusMessage = $('#status-message');
const workspace = $('#workspace');
const emptyState = $('#empty-state');
const functionGrid = $('#function-grid');
const saveButton = $('#save-layout');
const exportGraphButton = $('#export-graph');
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

loadCommandLineSession();

async function loadCommandLineSession() {
  try {
    const sessionResponse = await fetch('/api/session');
    if (!sessionResponse.ok) return;
    const session = await sessionResponse.json();
    const [irResponse, graphResponse] = await Promise.all([
      fetch('/api/ir'),
      fetch('/api/graphs'),
    ]);
    if (!irResponse.ok || !graphResponse.ok || state.irFile || state.graphDoc) return;
    const graphDocument = await graphResponse.json();
    validateGraphDocument(graphDocument);
    state.irText = await irResponse.text();
    state.irFile = { name: session.irName };
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
  state.suppressEdgeTextClick = String(drag.edge.id);
  if (!drag.moved) {
    selectEntity(drag.model, 'edge', drag.edge.id);
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

function clientPointInScene(scene, event) {
  const rect = scene.getBoundingClientRect();
  return {
    x: event.clientX - rect.left,
    y: event.clientY - rect.top,
  };
}

function openEdgeLabelEditor(model, edge, position) {
  model.scene.querySelector('.edge-inline-editor')?.remove();
  const displayedLabel = [...model.labelLayer.querySelectorAll('.edge-label-frame')]
    .find((label) => label.dataset.edgeId === String(edge.id));
  displayedLabel?.classList.add('editing');
  const edgeLayout = getFunctionLayout(model.function.name).edges[edge.id];
  const originalLabel = edgeLayout.label ?? edge.assertion ?? '';
  const kindText = String(edge.kind || 'dependency');
  const frame = document.createElement('div');
  frame.className = 'edge-inline-editor';
  frame.dataset.edgeId = String(edge.id);
  frame.style.top = `${position.y}px`;
  frame.style.left = `${position.left}px`;
  frame.style.width = `${position.width}px`;
  frame.style.height = `${position.height}px`;
  const kind = document.createElement('span');
  kind.className = 'edge-inline-editor-kind';
  kind.textContent = kindText;
  kind.style.color = edgeColor(edge.kind);
  const assertion = document.createElement('span');
  assertion.className = 'edge-inline-editor-assertion';
  const openBrace = document.createElement('span');
  openBrace.className = 'edge-assertion-brace';
  openBrace.textContent = '{\u00a0';
  const input = document.createElement('textarea');
  input.className = 'edge-inline-editor-input';
  input.rows = 1;
  input.maxLength = 100;
  input.setAttribute('aria-label', 'Edge assertion');
  input.value = originalLabel;
  const closeBrace = document.createElement('span');
  closeBrace.className = 'edge-assertion-brace';
  closeBrace.textContent = '\u00a0}';
  assertion.append(openBrace, input, closeBrace);
  frame.append(kind, assertion);

  const resizeForAssertion = () => {
    input.style.height = 'auto';
    const inputHeight = Math.max(18, input.scrollHeight);
    input.style.height = `${inputHeight}px`;
    frame.style.height = `${inputHeight + 2}px`;
  };
  model.scene.append(frame);
  input.style.height = `${Math.max(18, position.height - 2)}px`;
  input.focus();
  input.select();

  let finished = false;
  const finish = (commit) => {
    if (finished) return;
    finished = true;
    if (commit) edgeLayout.label = input.value;
    else if (originalLabel) edgeLayout.label = originalLabel;
    else delete edgeLayout.label;
    frame.remove();
    setupOverlay(model);
  };

  input.addEventListener('input', () => {
    edgeLayout.label = input.value;
    resizeForAssertion();
  });
  input.addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      finish(true);
    } else if (event.key === 'Escape') {
      event.preventDefault();
      finish(false);
    }
  });
  input.addEventListener('blur', () => finish(true));
}

function setStatus(message, isError = false) {
  statusMessage.textContent = message;
  statusMessage.classList.toggle('error-banner', isError);
}

function validateGraphDocument(document) {
  if (document?.format !== 'depvec-graph-v2' || !Array.isArray(document.functions)) {
    throw new Error('This is not a DepVec graph export. Generate one with `make graphs IR=path/to/file.ll`.');
  }
  for (const fn of document.functions) {
    if (typeof fn.name !== 'string' || typeof fn.ir !== 'string' ||
        !Array.isArray(fn.nodes) || !Array.isArray(fn.edges)) {
      throw new Error('The graph export is missing a function name, IR text, nodes, or edges.');
    }
  }
}

function refreshWorkspace() {
  if (!state.irFile || !state.graphDoc) {
    workspace.hidden = true;
    emptyState.hidden = false;
    saveButton.disabled = true;
    exportGraphButton.disabled = true;
    setStatus(state.irFile || state.graphDoc
      ? 'Choose the matching LLVM IR and graph export in the header.'
      : 'Choose LLVM IR and graph JSON files above.');
    return;
  }

  state.models.forEach((model) => model.resizeObserver?.disconnect());
  state.models = state.graphDoc.functions.map((fn, index) => createFunctionModel(fn, index));
  workspace.hidden = false;
  emptyState.hidden = true;
  saveButton.disabled = false;
  exportGraphButton.disabled = false;
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
    } else {
      warning = 'Some instructions could not be aligned to the selected module. Showing the canonical LLVM IR emitted with this graph.';
    }
  } else {
    warning = `Function ${fn.name} was not found in the selected module. Showing the canonical LLVM IR emitted with this graph.`;
  }

  if (!lines) {
    lines = fn.ir.replace(/\r\n/g, '\n').split('\n');
    nodeLines = new Map(fn.nodes.map((node) => [Number(node.id), Number(node.line)]));
  }

  return {
    function: fn,
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

function renderWorkspace() {
  functionGrid.replaceChildren();
  state.models.forEach((model) => functionGrid.append(renderFunctionCard(model)));
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
  stats.append(
    stat(`${fn.nodes.length}`, 'nodes'),
    stat(`${fn.edges.length}`, 'hyperedges'),
  );
  header.append(title, stats);

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
    lineNode.textContent = line || '\u200b';
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
    if (nodes.some((node) => node.kind === 'init')) lineNode.classList.add('kind-init');
    if (nodes.some((node) => node.kind === 'exit')) lineNode.classList.add('kind-exit');
    lineNode.setAttribute('role', 'button');
    lineNode.tabIndex = 0;
    const layout = getFunctionLayout(fn.name);
    if (nodes.some((node) => layout.nodes[String(node.id)]?.label)) lineNode.classList.add('node-labeled');
    lineNode.addEventListener('click', () => selectEntity(model, 'node', Number(nodes[0].id)));
    lineNode.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        selectEntity(model, 'node', Number(nodes[0].id));
      }
    });
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
  const icon = document.createElement('span');
  icon.className = `selection-icon${selected?.type === 'edge' ? ' edge' : ''}`;
  icon.textContent = selected?.type === 'edge' ? '↗' : selected ? '•' : '＋';
  const copy = document.createElement('div');
  copy.className = 'selection-copy';
  const heading = document.createElement('strong');
  const detail = document.createElement('span');
  if (!selected) {
    heading.textContent = 'Select an instruction or hyperedge';
    detail.textContent = 'Click assertion text to edit it; drag its frame along the edge';
  } else if (selected.type === 'node') {
    const node = model.function.nodes.find((entry) => Number(entry.id) === Number(selected.id));
    heading.textContent = `${node?.kind ?? 'instruction'} node n${selected.id}`;
    detail.textContent = node?.instruction || 'Function boundary';
  } else {
    const edge = model.function.edges.find((entry) => entry.id === selected.id);
    heading.textContent = `${edge?.kind ?? 'dependency'} hyperedge`;
    detail.textContent = `${(edge?.sources || []).map((source) => `n${source}`).join(' + ')} → n${edge?.target ?? '?'}`;
  }
  copy.append(heading, detail);

  inspector.append(icon, copy);
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

function setupOverlay(model) {
  if (!model.card?.isConnected) return;
  model.svg?.remove();
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

  const endpointsByNode = new Map();
  const addEndpoint = (nodeId, endpoint) => {
    if (!endpointsByNode.has(nodeId)) endpointsByNode.set(nodeId, []);
    endpointsByNode.get(nodeId).push(endpoint);
  };
  for (const track of candidates) {
    for (const source of track.sources) addEndpoint(source.nodeId, { source });
    addEndpoint(Number(track.edge.target), { track });
  }
  for (const endpoints of endpointsByNode.values()) {
    endpoints.forEach((endpoint, index) => {
      const offset = (index - (endpoints.length - 1) / 2) * EDGE_ENDPOINT_SPACING;
      if (endpoint.source) endpoint.source.offset = offset;
      else endpoint.track.targetOffset = offset;
    });
  }
  for (const track of candidates) {
    const endpointYs = [
      ...track.sources.map((source) => source.anchor.y + source.offset),
      track.target.y + track.targetOffset,
    ];
    track.startY = Math.min(...endpointYs);
    track.endY = Math.max(...endpointYs);
  }
  candidates.sort((a, b) => a.startY - b.startY || b.endY - a.endY || a.index - b.index);

  const laneEnds = [];
  for (const track of candidates) {
    let lane = laneEnds.findIndex((endY) => endY < track.startY);
    if (lane < 0) lane = laneEnds.length;
    laneEnds[lane] = track.endY;
    track.lane = lane;
  }

  const layout = getFunctionLayout(model.function.name);
  const firstTrackX = 12 + EDGE_LABEL_MAX_WIDTH;
  model.codeLeft = laneEnds.length
    ? firstTrackX + 10 + (laneEnds.length - 1) * EDGE_LANE_SPACING
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
  for (const track of candidates) {
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
  const editingEdgeId = model.scene.querySelector('.edge-inline-editor')?.dataset.edgeId;
  tracks.sort((a, b) => Number(isSelected(model, 'edge', a.edge.id))
    - Number(isSelected(model, 'edge', b.edge.id)));
  const leftMostTrackX = tracks.length ? Math.min(...tracks.map((track) => track.x)) : 0;
  const labelRight = leftMostTrackX - EDGE_TRACK_LABEL_GAP;

  for (const track of tracks) {
    const { edge, sources, target, targetOffset } = track;
    const color = edgeColor(edge.kind);

    const edgeLayout = layout.edges[String(edge.id)];
    const customLabel = edgeLayout.label ?? edge.assertion ?? '';
    const fullLabel = `{ ${customLabel} }`;
    const kindText = String(edge.kind || 'dependency');
    const labelWidth = edgeLabelWidth(edge.kind, customLabel);
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
      class: 'edge-label-tether',
      stroke: color,
    }));

    const labelFrame = document.createElement('div');
    labelFrame.className = `edge-label-frame${isSelected(model, 'edge', edge.id) ? ' selected' : ''}`;
    if (editingEdgeId === String(edge.id)) labelFrame.classList.add('editing');
    labelFrame.dataset.edgeId = String(edge.id);
    labelFrame.setAttribute('role', 'group');
    labelFrame.setAttribute('aria-label', `Drag the ${edge.kind} assertion label up or down along its edge`);
    labelFrame.tabIndex = 0;
    labelFrame.style.left = `${labelLeft}px`;
    labelFrame.style.top = `${labelY}px`;
    labelFrame.style.width = `${labelWidth}px`;
    labelFrame.title = edge.values?.length
      ? `${kindText} · ${fullLabel} · ${edge.values.join(', ')}`
      : `${kindText} · ${fullLabel}`;
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
        selectEntity(model, 'edge', edge.id);
        focusEdgeLabelFrame(model, edge.id);
      }
    });

    const kindLabel = document.createElement('span');
    kindLabel.className = 'edge-kind-label';
    kindLabel.textContent = kindText;
    kindLabel.style.color = color;

    const assertionLabel = document.createElement('span');
    assertionLabel.className = 'edge-assertion-label';
    assertionLabel.setAttribute('role', 'button');
    assertionLabel.setAttribute('tabindex', '0');
    assertionLabel.setAttribute('aria-label', `Edit assertion for ${edge.kind} hyperedge`);
    const openBrace = document.createElement('span');
    openBrace.className = 'edge-assertion-brace';
    openBrace.textContent = '{\u00a0';
    const labelText = document.createElement('span');
    labelText.className = 'edge-label-text';
    labelText.textContent = customLabel;
    const closeBrace = document.createElement('span');
    closeBrace.className = 'edge-assertion-brace';
    closeBrace.textContent = '\u00a0}';
    assertionLabel.append(openBrace, labelText, closeBrace);
    assertionLabel.addEventListener('pointerdown', (event) => {
      state.suppressEdgeTextClick = null;
      event.stopPropagation();
    });
    assertionLabel.addEventListener('click', (event) => {
      event.preventDefault();
      event.stopPropagation();
      if (state.suppressEdgeTextClick === String(edge.id)) {
        state.suppressEdgeTextClick = null;
        return;
      }
      const editorPosition = {
        left: labelLeft,
        y: labelY,
        width: labelWidth,
        height: labelFrame.offsetHeight,
      };
      selectEntity(model, 'edge', edge.id);
      openEdgeLabelEditor(model, edge, editorPosition);
    });
    assertionLabel.addEventListener('keydown', (event) => {
      event.stopPropagation();
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        const editorPosition = {
          left: labelLeft,
          y: labelY,
          width: labelWidth,
          height: labelFrame.offsetHeight,
        };
        selectEntity(model, 'edge', edge.id);
        openEdgeLabelEditor(model, edge, editorPosition);
      }
    });
    labelFrame.append(kindLabel, assertionLabel);
    model.labelLayer.append(labelFrame);
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
  const hit = svgElement('path', {
    d, class: `edge-hit${isSelected(model, 'edge', edge.id) ? ' selected' : ''}`,
  });
  hit.addEventListener('click', () => selectEntity(model, 'edge', edge.id));
  model.svg.append(hit);
  const visible = svgElement('path', {
    d,
    class: `edge-visible${isSelected(model, 'edge', edge.id) ? ' selected' : ''}`,
    stroke: color,
    fill: color,
  });
  model.svg.append(visible);
}

function selectEntity(model, type, id) {
  model.selected = { type, id };
  model.lineNodes.forEach((line) => line.classList.remove('selected'));
  if (type === 'node') {
    const line = model.nodeLines.get(Number(id));
    model.lineNodes.get(line)?.classList.add('selected');
  }
  updateInspector(model);
  drawOverlay(model);
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

function exportGraph() {
  if (!state.graphDoc) return;

  const graph = JSON.parse(JSON.stringify(state.graphDoc));
  for (const fn of graph.functions) {
    const editedEdges = state.layoutDoc.functions?.[fn.name]?.edges || {};
    for (const edge of fn.edges) {
      const editedAssertion = editedEdges[String(edge.id)]?.label;
      edge.assertion = editedAssertion === undefined
        ? String(edge.assertion || '').trim()
        : String(editedAssertion).trim();
    }
  }

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
