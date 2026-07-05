const api = window.iec61850;

const modules = [
  { id: 'model', title: '数据模型', hint: 'LD/LN/DO/DA' },
  { id: 'data', title: '数据对象', hint: '读写与监视' },
  { id: 'datasets', title: '数据集', hint: '成员与快照' },
  { id: 'reports', title: '报告', hint: 'URCB/BRCB' },
  { id: 'control', title: '控制', hint: 'SBO/DO' },
  { id: 'files', title: '文件与日志', hint: 'File/Log' },
  { id: 'settings', title: '定值组', hint: 'SGCB' },
  { id: 'traffic', title: '通信', hint: 'MMS/GOOSE/SV' }
];

const state = {
  workspace: null,
  selectedNode: null,
  openTabs: [],
  activeModule: 'model',
  readValues: [],
  datasetSnapshot: null,
  reportBlocks: [],
  datasetReference: '',
  datasetTested: {},
  expandedPoints: {},
  pointInspections: {},
  reportLogicalNode: '',
  selectedReportReference: '',
  fileEntries: [],
  fileContent: null,
  logEntries: [],
  settingState: null,
  busy: null,
  modelSignature: '',
  pagesDirty: true,
  events: [],
  contextNode: null
};

const dom = {
  body: document.body,
  workspace: document.getElementById('workspace'),
  moduleList: document.getElementById('moduleList'),
  modelTree: document.getElementById('modelTree'),
  treeFilter: document.getElementById('treeFilter'),
  tabs: document.getElementById('tabs'),
  pageHost: document.getElementById('pageHost'),
  propertyPanel: document.getElementById('propertyPanel'),
  eventLog: document.getElementById('eventLog'),
  connLamp: document.getElementById('connLamp'),
  connState: document.getElementById('connState'),
  connTarget: document.getElementById('connTarget'),
  hostInput: document.getElementById('hostInput'),
  portInput: document.getElementById('portInput'),
  dialogHost: document.getElementById('dialogHost'),
  dialogPort: document.getElementById('dialogPort'),
  dialogMock: document.getElementById('dialogMock'),
  connectDialog: document.getElementById('connectDialog'),
  writeDialog: document.getElementById('writeDialog'),
  writeRef: document.getElementById('writeRef'),
  writeFc: document.getElementById('writeFc'),
  writeType: document.getElementById('writeType'),
  writeValue: document.getElementById('writeValue'),
  reportDialog: document.getElementById('reportDialog'),
  reportRef: document.getElementById('reportRef'),
  reportRptId: document.getElementById('reportRptId'),
  reportDataSet: document.getElementById('reportDataSet'),
  reportEnabled: document.getElementById('reportEnabled'),
  reportBuffered: document.getElementById('reportBuffered'),
  reportBufTm: document.getElementById('reportBufTm'),
  reportIntgPd: document.getElementById('reportIntgPd'),
  reportTrgOps: document.getElementById('reportTrgOps'),
  reportOptFlds: document.getElementById('reportOptFlds'),
  reportGi: document.getElementById('reportGi'),
  reportPurgeBuf: document.getElementById('reportPurgeBuf'),
  reportReservation: document.getElementById('reportReservation'),
  reportResvTms: document.getElementById('reportResvTms'),
  statusText: document.getElementById('statusText'),
  modelStats: document.getElementById('modelStats'),
  trafficStats: document.getElementById('trafficStats'),
  busyOverlay: document.getElementById('busyOverlay'),
  busyTitle: document.getElementById('busyTitle'),
  busyMessage: document.getElementById('busyMessage'),
  contextMenu: document.getElementById('contextMenu')
};

const params = new URLSearchParams(location.search);
const detachedModule = params.get('module');
const detached = params.get('detached') === '1';
if (detached) {
  dom.body.classList.add('detached');
  state.activeModule = detachedModule || 'model';
}

function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function logEvent(level, source, message) {
  state.events.unshift({
    time: new Date().toLocaleTimeString('zh-CN', { hour12: false }),
    level,
    source,
    message
  });
  state.events = state.events.slice(0, 200);
  renderEvents();
}

function modelSignature(model) {
  const roots = model?.roots || [];
  const countNodes = (nodes) => (nodes || []).reduce((total, node) => total + 1 + countNodes(node.children), 0);
  const childSignature = (nodes) => (nodes || [])
    .map((node) => `${node.reference || ''}>${(node.children || []).length}`)
    .join('|');
  return [
    model?.source || '',
    model?.logicalDevices || 0,
    model?.logicalNodes || 0,
    model?.dataObjects || 0,
    model?.dataAttributes || 0,
    countNodes(roots),
    childSignature(roots)
  ].join(':');
}

function setBusy(title, message) {
  state.busy = { title, message };
  dom.statusText.textContent = message;
  dom.busyTitle.textContent = title;
  dom.busyMessage.textContent = message;
  dom.busyOverlay.hidden = false;
  document.querySelectorAll('button, input, select').forEach((element) => {
    if (!element.closest('dialog')) element.disabled = true;
  });
}

function clearBusy() {
  state.busy = null;
  dom.busyOverlay.hidden = true;
  document.querySelectorAll('button, input, select').forEach((element) => {
    if (!element.closest('dialog')) element.disabled = false;
  });
  renderStatus();
}

function fallbackMock(value, fallback = '') {
  return state.workspace?.connection?.mock ? value : fallback;
}

async function withBusy(title, message, action) {
  setBusy(title, message);
  await new Promise((resolve) => requestAnimationFrame(resolve));
  try {
    return await action();
  } finally {
    clearBusy();
  }
}

function flattenTree(nodes, level = 0, rows = []) {
  for (const node of nodes || []) {
    rows.push({ node, level });
    flattenTree(node.children || [], level + 1, rows);
  }
  return rows;
}

function findNode(reference, nodes = state.workspace?.model?.roots || []) {
  for (const node of nodes) {
    if (node.reference === reference) return node;
    const found = findNode(reference, node.children || []);
    if (found) return found;
  }
  return null;
}

function findNodeFlexible(reference) {
  if (!reference) return null;
  const normalized = String(reference).replaceAll('$', '.');
  return findNode(reference) || findNode(normalized);
}

function logicalNodeFromReference(reference) {
  const value = String(reference || '').replaceAll('$', '.');
  for (const marker of ['.RP.', '.BR.']) {
    const index = value.indexOf(marker);
    if (index > 0) return value.slice(0, index);
  }
  const dot = value.indexOf('.');
  return dot > 0 ? value.slice(0, dot) : value;
}

function preferredReference(node) {
  return node?.linkedReference || node?.reference || '';
}

function stripFcSuffix(reference) {
  return String(reference || '').replaceAll('$', '.').replace(/\[[^\]]+\]$/, '');
}

function dataObjectReferenceFromMember(reference) {
  const value = stripFcSuffix(reference);
  const slash = value.indexOf('/');
  const firstDot = value.indexOf('.', slash >= 0 ? slash + 1 : 0);
  if (firstDot < 0) return value;
  const secondDot = value.indexOf('.', firstDot + 1);
  return secondDot < 0 ? value : value.slice(0, secondDot);
}

function dataSetNodes() {
  return flattenTree(state.workspace?.model?.roots || [])
    .map((row) => row.node)
    .filter((node) => node.kind === 'DS');
}

function reportNodes() {
  return flattenTree(state.workspace?.model?.roots || [])
    .map((row) => row.node)
    .filter((node) => node.kind === 'URCB' || node.kind === 'BRCB');
}

function reportNode(reference) {
  return reportNodes().find((node) => node.reference === reference) || findNodeFlexible(reference);
}

function selectedReportBlock() {
  const reference = state.selectedReportReference || state.reportBlocks[0]?.reference || '';
  return (state.reportBlocks || []).find((block) => block.reference === reference) ||
    (() => {
      const node = reportNode(reference);
      if (!node) return null;
      return {
        reference: node.reference,
        rptId: '',
        dataSet: node.linkedReference || '',
        enabled: false,
        buffered: node.kind === 'BRCB' || node.reference?.includes('.BR.'),
        bufTimeMs: 100,
        integrityPeriodMs: 1000,
        triggerOptions: '19',
        optionalFields: '31',
        confRev: 0,
        sqNum: 0,
        gi: true,
        purgeBuf: false,
        reservation: false,
        reservationTimeS: 0,
        owner: ''
      };
    })();
}

function dataSetTestKey(dataSetReference, objectReference) {
  return `${dataSetReference}::${objectReference}`;
}

function isPointTested(dataSetReference, objectReference) {
  return Boolean(state.datasetTested[dataSetTestKey(dataSetReference, objectReference)]);
}

function setPointTested(dataSetReference, objectReference, tested) {
  const key = dataSetTestKey(dataSetReference, objectReference);
  if (tested) state.datasetTested[key] = new Date().toISOString();
  else delete state.datasetTested[key];
  try {
    localStorage.setItem('iec61850.datasetTested', JSON.stringify(state.datasetTested));
  } catch {
    logEvent('WARN', '工程', '测试标记无法写入本机存储');
  }
}

function loadPointTested() {
  try {
    state.datasetTested = JSON.parse(localStorage.getItem('iec61850.datasetTested') || '{}') || {};
  } catch {
    state.datasetTested = {};
  }
}

async function togglePointExpanded(objectReference) {
  if (!state.datasetSnapshot?.reference || !objectReference) return;
  const key = dataSetTestKey(state.datasetSnapshot.reference, objectReference);
  const next = !state.expandedPoints[key];
  state.expandedPoints[key] = next;
  if (next && !state.pointInspections[key]) {
    const point = (state.datasetSnapshot.points || []).find((item) => item.objectReference === objectReference) || {};
    state.pointInspections[key] = await withBusy('读取点位属性', `正在读取 ${objectReference} 的属性...`, () => api.call('InspectDataObject', {
      objectReference,
      fc: point.fc || 'MX'
    }));
  }
  renderAll();
}

function isPointExpanded(dataSetReference, objectReference) {
  return Boolean(state.expandedPoints[dataSetTestKey(dataSetReference, objectReference)]);
}

function pointInspection(dataSetReference, objectReference) {
  return state.pointInspections[dataSetTestKey(dataSetReference, objectReference)] || null;
}

function liveValueKey(dataSetReference, reference) {
  return `${dataSetReference}|${dataObjectReferenceFromMember(reference)}`;
}

function applyLiveValuesToSnapshot(liveValues = []) {
  const snapshot = state.datasetSnapshot;
  if (!snapshot?.reference || !Array.isArray(snapshot.points) || snapshot.points.length === 0) return false;

  const updates = new Map();
  for (const item of liveValues || []) {
    const dataSet = item.dataSet || item.data_set || '';
    const value = item.value || {};
    if (dataSet !== snapshot.reference || !value.reference) continue;
    updates.set(liveValueKey(dataSet, value.reference), value);
  }
  if (updates.size === 0) return false;

  let changed = false;
  snapshot.points = snapshot.points.map((point) => {
    const key = liveValueKey(snapshot.reference, point.objectReference || point.memberReference || '');
    const value = updates.get(key);
    if (!value) return point;
    changed = true;
    return {
      ...point,
      value,
      quality: value.quality || point.quality,
      timestamp: value.timestamp || point.timestamp
    };
  });
  return changed;
}

function moduleById(id) {
  return modules.find((item) => item.id === id) || modules[0];
}

function openModule(id, activate = true) {
  const mod = moduleById(id);
  if (!state.openTabs.find((tab) => tab.id === id)) {
    state.openTabs.push(mod);
  }
  if (activate) state.activeModule = id;
  state.pagesDirty = true;
  renderTabs();
  renderModuleList();
  renderPages();
}

function closeModule(id) {
  if (state.openTabs.length <= 1) return;
  const index = state.openTabs.findIndex((tab) => tab.id === id);
  state.openTabs = state.openTabs.filter((tab) => tab.id !== id);
  if (state.activeModule === id) {
    state.activeModule = state.openTabs[Math.max(0, index - 1)]?.id || 'model';
  }
  state.pagesDirty = true;
  renderTabs();
  renderModuleList();
  renderPages();
}

function renderModuleList() {
  dom.moduleList.innerHTML = modules.map((mod) => `
    <button class="module-button ${state.activeModule === mod.id ? 'active' : ''}" data-module="${mod.id}">
      <span>${mod.title}</span><span>${mod.hint}</span>
    </button>
  `).join('');
}

function renderTabs() {
  dom.tabs.innerHTML = state.openTabs.map((tab) => `
    <div class="tab ${state.activeModule === tab.id ? 'active' : ''}" data-tab="${tab.id}">
      <span>${tab.title}</span>
      <button data-close-tab="${tab.id}" title="关闭">×</button>
    </div>
  `).join('');
}

function renderModelTree() {
  const filter = dom.treeFilter.value.trim().toLowerCase();
  const rows = flattenTree(state.workspace?.model?.roots || []);
  const visible = rows.filter(({ node }) => {
    if (!filter) return true;
    return `${node.name} ${node.reference} ${node.kind} ${node.fc}`.toLowerCase().includes(filter);
  });
  if (visible.length === 0) {
    dom.modelTree.innerHTML = '<div class="empty-note">未加载模型。请先连接 IED，然后刷新在线模型；需要离线体验时点击“模拟”。</div>';
    return;
  }
  dom.modelTree.innerHTML = visible.map(({ node, level }) => `
    <div class="tree-node ${state.selectedNode?.reference === node.reference ? 'selected' : ''}" data-ref="${escapeHtml(node.reference)}" data-kind="${escapeHtml(node.kind)}" data-linked-ref="${escapeHtml(node.linkedReference || '')}" title="${escapeHtml(node.description || node.reference)}" style="--level:${level}">
      <span><span class="tree-indent"></span><span class="tree-kind">${escapeHtml(node.kind)}</span></span>
      <span>${escapeHtml(node.name)}</span>
      <span>${escapeHtml(node.fc || '')}</span>
    </div>
  `).join('');
}

function actionButton(label, action, node, linkedReference = node?.linkedReference || '') {
  return `<button data-action="${escapeHtml(action)}" data-ref="${escapeHtml(node?.reference || '')}" data-linked-ref="${escapeHtml(linkedReference || '')}" data-kind="${escapeHtml(node?.kind || '')}">${escapeHtml(label)}</button>`;
}

function renderPropertyActions(node) {
  const buttons = [];
  if (node.kind === 'DS') {
    buttons.push(actionButton('打开数据集', 'dataset:open-node', node));
    buttons.push(actionButton('读取数据集', 'dataset:read-node', node));
  } else if (node.kind === 'URCB' || node.kind === 'BRCB') {
    buttons.push(actionButton('打开报告页', 'report:open-node', node));
    if (node.linkedReference) buttons.push(actionButton('跳转数据集', 'node:jump-link', node));
    buttons.push(actionButton('启用报告', 'report:enable-node', node));
    buttons.push(actionButton('停用报告', 'report:disable-node', node));
  } else if (node.kind === 'FCDA' || node.kind === 'LINK') {
    if (node.linkedReference) buttons.push(actionButton('跳转关联对象', 'node:jump-link', node));
    buttons.push(actionButton('读取引用', 'object:read-node', node));
  } else {
    buttons.push(actionButton('读取', 'object:read-node', node));
    buttons.push(actionButton('写入', 'object:write', node));
    buttons.push(actionButton('监视', 'watch:add', node));
  }
  buttons.push(actionButton('复制引用', 'node:copy', node));
  buttons.push(actionButton('独立窗口', 'module:detach', node));
  return buttons.join('');
}

function renderProperties() {
  const node = state.selectedNode;
  if (!node) {
    dom.propertyPanel.innerHTML = '<div class="property-title">未选择对象</div><div class="kv"><div>操作</div><div>从左侧模型树选择 LD/LN/DO/DA。</div></div>';
    return;
  }

  dom.propertyPanel.innerHTML = `
    <div class="property-title">${escapeHtml(node.name)}</div>
    <div class="kv">
      <div>引用</div><div>${escapeHtml(node.reference)}</div>
      <div>类型</div><div>${escapeHtml(node.kind)}</div>
      <div>FC</div><div>${escapeHtml(node.fc || '-')}</div>
      <div>数据类型</div><div>${escapeHtml(node.type || '-')}</div>
      <div>说明</div><div>${escapeHtml(node.description || '-')}</div>
      <div>关联</div><div>${node.linkedReference ? `${escapeHtml(node.linkKind || '-')}: ${escapeHtml(node.linkedReference)}` : '-'}</div>
      <div>可读</div><div>${node.readable ? '是' : '否'}</div>
      <div>可写</div><div>${node.writable ? '是' : '否'}</div>
      <div>可控</div><div>${node.controllable ? '是' : '否'}</div>
    </div>
    <div class="property-actions">
      ${renderPropertyActions(node)}
    </div>
  `;
}

function renderEvents() {
  const backendEvents = state.workspace?.events || [];
  const merged = [
    ...state.events,
    ...backendEvents.map((event) => ({
      time: event.time,
      level: event.level,
      source: event.source,
      message: event.message
    }))
  ].slice(0, 180);

  dom.eventLog.innerHTML = merged.map((event) => `
    <div class="event-line event-level-${escapeHtml(event.level)}">
      <span>${escapeHtml(event.time)}</span>
      <span>${escapeHtml(event.level)}</span>
      <span>${escapeHtml(event.source)}</span>
      <span>${escapeHtml(event.message)}</span>
    </div>
  `).join('');
}

function contextMenuItems(node) {
  const items = [];
  if (!node) return items;

  if (node.kind === 'DS') {
    items.push({ label: '打开数据集页', action: 'dataset:open-node' });
    items.push({ label: '读取数据集成员和值', action: 'dataset:read-node' });
    items.push({ separator: true });
  } else if (node.kind === 'URCB' || node.kind === 'BRCB') {
    items.push({ label: '打开报告控制页', action: 'report:open-node' });
    if (node.linkedReference) items.push({ label: '跳转关联数据集', action: 'node:jump-link' });
    items.push({ label: '启用报告', action: 'report:enable-node' });
    items.push({ label: '停用报告', action: 'report:disable-node' });
    items.push({ separator: true });
  } else if (node.kind === 'FCDA' || node.kind === 'LINK') {
    if (node.linkedReference) items.push({ label: '跳转关联对象', action: 'node:jump-link' });
    items.push({ label: '读取当前引用', action: 'object:read-node' });
    items.push({ separator: true });
  } else if (node.kind === 'DO' || node.kind === 'DA') {
    items.push({ label: '读取对象', action: 'object:read-node' });
    if (node.writable) items.push({ label: '写入对象', action: 'object:write' });
    items.push({ label: '加入监视', action: 'watch:add' });
    items.push({ separator: true });
  } else if (node.kind === 'LN') {
    items.push({ label: '枚举报告控制块', action: 'report:list-node' });
    items.push({ separator: true });
  }

  items.push({ label: '复制引用', action: 'node:copy' });
  return items;
}

function showContextMenu(x, y, node) {
  const items = contextMenuItems(node);
  if (!dom.contextMenu || items.length === 0) return;
  state.contextNode = node;
  dom.contextMenu.innerHTML = items.map((item) => {
    if (item.separator) return '<div class="context-separator"></div>';
    return `<button data-action="${escapeHtml(item.action)}" data-ref="${escapeHtml(node.reference || '')}" data-linked-ref="${escapeHtml(node.linkedReference || '')}" data-kind="${escapeHtml(node.kind || '')}">${escapeHtml(item.label)}</button>`;
  }).join('');
  dom.contextMenu.hidden = false;
  const rect = dom.contextMenu.getBoundingClientRect();
  const left = Math.min(x, window.innerWidth - rect.width - 8);
  const top = Math.min(y, window.innerHeight - rect.height - 8);
  dom.contextMenu.style.left = `${Math.max(4, left)}px`;
  dom.contextMenu.style.top = `${Math.max(4, top)}px`;
}

function hideContextMenu() {
  if (!dom.contextMenu) return;
  dom.contextMenu.hidden = true;
  state.contextNode = null;
}

function renderStatus() {
  const connection = state.workspace?.connection || {};
  const model = state.workspace?.model || {};
  const traffic = state.workspace?.traffic || {};
  dom.connLamp.classList.toggle('on', Boolean(connection.connected));
  dom.connState.textContent = connection.state || 'DISCONNECTED';
  dom.connTarget.textContent = `${connection.host || '127.0.0.1'}:${connection.port || 102}${connection.mock ? ' / MOCK' : ''}`;
  if (!state.busy) dom.statusText.textContent = connection.message || 'Ready';
  dom.modelStats.textContent = `LD ${model.logicalDevices || 0} / LN ${model.logicalNodes || 0} / DO ${model.dataObjects || 0} / DA ${model.dataAttributes || 0}`;
  dom.trafficStats.textContent = `REQ ${traffic.requests || 0} / RSP ${traffic.responses || 0} / RPT ${traffic.reports || 0}`;
}

function tableRows(items, columns) {
  if (!items || items.length === 0) {
    return `<tr><td colspan="${columns.length}">无数据</td></tr>`;
  }
  return items.map((item) => `
    <tr>
      ${columns.map((col) => `<td>${escapeHtml(col.value(item))}</td>`).join('')}
    </tr>
  `).join('');
}

function renderTable(items, columns) {
  return `
    <table class="table">
      <thead><tr>${columns.map((col) => `<th>${escapeHtml(col.title)}</th>`).join('')}</tr></thead>
      <tbody>${tableRows(items, columns)}</tbody>
    </table>
  `;
}

function renderDataSetResult() {
  const data = state.datasetSnapshot;
  if (!data) return '<div class="empty-note">从左侧数据集列表选择一个数据集后，会在这里打开点位巡检表。</div>';
  const points = data.points || [];
  if (points.length === 0) return '<div class="empty-note">该数据集没有返回点位。请确认设备支持 GetDataSetValue，或检查数据集成员配置。</div>';

  return `
    <div class="dataset-inspector-header">
      <div>
        <strong>${escapeHtml(data.reference || '-')}</strong>
        <span>${points.length} 个点位 / ${data.deletable ? '动态数据集' : '固定数据集'}</span>
      </div>
      <button data-action="dataset:read">刷新当前数据集</button>
    </div>
    <table class="table dataset-point-table">
      <thead>
        <tr>
          <th></th>
          <th>数据对象引用</th>
          <th>dU 描述</th>
          <th>值</th>
          <th>质量</th>
          <th>时间</th>
          <th>已测试</th>
        </tr>
      </thead>
      <tbody>
        ${points.map((point) => renderDataSetPointRow(data.reference, point)).join('')}
      </tbody>
    </table>
  `;
}

function renderDataSetPointRow(dataSetReference, point) {
  const objectReference = point.objectReference || '';
  const value = point.value || {};
  const expanded = isPointExpanded(dataSetReference, objectReference);
  const tested = isPointTested(dataSetReference, objectReference);
  return `
    <tr class="point-row ${tested ? 'tested' : ''}">
      <td><button class="icon-button" data-action="dataset:toggle-point" data-object-ref="${escapeHtml(objectReference)}" title="展开属性">${expanded ? '-' : '+'}</button></td>
      <td class="mono-cell">${escapeHtml(objectReference || point.memberReference || '-')}</td>
      <td>${escapeHtml(point.du || '-')}</td>
      <td>${escapeHtml(value.value || value.error || '-')}</td>
      <td>${escapeHtml(point.quality || value.quality || '-')}</td>
      <td>${escapeHtml(point.timestamp || value.timestamp || '-')}</td>
      <td><label class="test-check"><input type="checkbox" data-action="dataset:toggle-tested" data-object-ref="${escapeHtml(objectReference)}" ${tested ? 'checked' : ''} />已测</label></td>
    </tr>
    ${expanded ? renderPointAttributes(dataSetReference, point) : ''}
  `;
}

function renderPointAttributes(dataSetReference, point) {
  const inspection = pointInspection(dataSetReference, point.objectReference);
  const attributes = inspection?.attributes || [];
  return `
    <tr class="point-attributes">
      <td></td>
      <td colspan="6">
        ${renderTable(attributes, [
          { title: '属性引用', value: (item) => item.reference },
          { title: 'FC', value: (item) => item.fc },
          { title: '类型', value: (item) => item.type },
          { title: '值', value: (item) => item.value || item.error || '' },
          { title: '质量', value: (item) => item.quality || '' },
          { title: '时间', value: (item) => item.timestamp || '' }
        ])}
      </td>
    </tr>
  `;
}

function renderReportBlocks() {
  const blockRows = state.reportBlocks || [];
  if (blockRows.length === 0) return '<div class="empty-note">尚未读取该逻辑节点下的报告控制块参数。选择 RCB 后会自动读取当前值。</div>';
  return renderTable(blockRows, [
    { title: '引用', value: (item) => item.reference },
    { title: 'RptID', value: (item) => item.rptId },
    { title: '数据集', value: (item) => item.dataSet },
    { title: '类型', value: (item) => item.buffered ? 'BRCB' : 'URCB' },
    { title: '使能', value: (item) => item.enabled ? '是' : '否' },
    { title: 'BufTm', value: (item) => item.bufTimeMs },
    { title: 'IntgPd', value: (item) => item.integrityPeriodMs }
  ]);
}

function renderReportParameters(block) {
  if (!block) {
    return '<div class="empty-note">从左侧报告控制块列表选择一个 RCB。右键模型树中的 RCB 也可以直接打开。</div>';
  }
  return `
    <div class="kv report-param-grid">
      <div>RCB 引用</div><div class="mono-cell">${escapeHtml(block.reference || '-')}</div>
      <div>RptID</div><div>${escapeHtml(block.rptId || '-')}</div>
      <div>DatSet</div><div class="mono-cell">${escapeHtml(block.dataSet || '-')}</div>
      <div>类型</div><div>${block.buffered ? 'BRCB' : 'URCB'}</div>
      <div>RptEna</div><div>${block.enabled ? '已启用' : '未启用'}</div>
      <div>ConfRev / SqNum</div><div>${escapeHtml(block.confRev || 0)} / ${escapeHtml(block.sqNum || 0)}</div>
      <div>BufTm / IntgPd</div><div>${escapeHtml(block.bufTimeMs || 0)} ms / ${escapeHtml(block.integrityPeriodMs || 0)} ms</div>
      <div>TrgOps / OptFlds</div><div>${escapeHtml(block.triggerOptions || '-')} / ${escapeHtml(block.optionalFields || '-')}</div>
      <div>GI / PurgeBuf</div><div>${block.gi ? 'true' : 'false'} / ${block.purgeBuf ? 'true' : 'false'}</div>
      <div>Resv / ResvTms</div><div>${block.reservation ? 'true' : 'false'} / ${escapeHtml(block.reservationTimeS || 0)} s</div>
      <div>Owner</div><div>${escapeHtml(block.owner || '-')}</div>
    </div>
    <div class="command-row top-gap">
      <button data-action="report:configure" ${block.reference ? '' : 'disabled'}>参数设置并启用</button>
      <button data-action="report:disable" ${block.reference ? '' : 'disabled'}>停用报告</button>
      <button data-action="report:read-dataset" ${block.dataSet ? '' : 'disabled'}>打开关联数据集</button>
    </div>
  `;
}

function renderFileResult() {
  return `
    ${renderTable(state.fileEntries || [], [
      { title: '名称', value: (item) => item.name },
      { title: '路径', value: (item) => item.path },
      { title: '类型', value: (item) => item.directory ? '目录' : '文件' },
      { title: '大小', value: (item) => item.size },
      { title: '修改时间', value: (item) => item.modified }
    ])}
    ${state.fileContent ? `<pre class="text-preview">${escapeHtml(state.fileContent.textPreview || '')}</pre>` : ''}
  `;
}

function renderLogResult() {
  return renderTable(state.logEntries || [], [
    { title: '时间', value: (item) => item.time },
    { title: '引用', value: (item) => item.reference },
    { title: '原因', value: (item) => item.reason },
    { title: '值数量', value: (item) => item.values?.length || 0 }
  ]);
}

function renderSettingResult() {
  const data = state.settingState;
  if (!data) return '<div class="empty-note">尚未读取定值组状态。</div>';

  return `<div class="kv">
    <div>逻辑节点</div><div>${escapeHtml(data.logicalNode)}</div>
    <div>活动组</div><div>${escapeHtml(data.activeGroup)}</div>
    <div>编辑组</div><div>${escapeHtml(data.editableGroup)}</div>
    <div>组数量</div><div>${escapeHtml(data.count)}</div>
  </div>`;
}

function renderPages() {
  const active = state.activeModule;
  dom.pageHost.innerHTML = modules.map((mod) => `
    <section class="page ${active === mod.id ? 'active' : ''}" data-page="${mod.id}">
      ${renderPage(mod.id)}
    </section>
  `).join('');
}

function renderPage(id) {
  switch (id) {
    case 'model':
      return renderModelPage();
    case 'data':
      return renderDataPage();
    case 'datasets':
      return renderDataSetPage();
    case 'reports':
      return renderReportsPage();
    case 'control':
      return renderControlPage();
    case 'files':
      return renderFilesPage();
    case 'settings':
      return renderSettingsPage();
    case 'traffic':
      return renderTrafficPage();
    default:
      return '';
  }
}

function renderModelPage() {
  const rows = flattenTree(state.workspace?.model?.roots || []);
  const connected = Boolean(state.workspace?.connection?.connected);
  return `
    <div class="page-layout two">
      <div class="section">
        <div class="section-header"><span>在线模型目录</span><button data-action="model:refresh">刷新</button></div>
        <div class="section-body">
          ${renderTable(rows, [
            { title: '层级', value: (row) => `${'  '.repeat(row.level)}${row.node.kind}` },
            { title: '名称', value: (row) => row.node.name },
            { title: '引用', value: (row) => row.node.reference },
            { title: 'FC', value: (row) => row.node.fc || '' }
          ])}
        </div>
      </div>
      <div class="section">
        <div class="section-header"><span>对象详情</span><button data-action="object:read">读取选中</button></div>
        <div class="section-body">
          <div class="kv">
            <div>当前对象</div><div>${escapeHtml(state.selectedNode?.reference || '-')}</div>
            <div>状态</div><div>${connected ? '已连接，可读取在线模型。' : '未连接，不显示示例模型；点击“连接”读取真实 IED，或点击“模拟”进入离线调试。'}</div>
            <div>说明</div><div>模型浏览使用 libiec61850 在线枚举 LD、LN、DO、DA。大型 IED 读取模型时会显示进度提示。</div>
          </div>
        </div>
      </div>
    </div>
  `;
}

function renderDataPage() {
  return `
    <div class="page-layout two">
      <div class="section">
        <div class="section-header"><span>读取与写入</span><button data-action="object:read">读取</button></div>
        <div class="section-body">
          <div class="grid-form">
            <label>对象引用</label><input id="pageReadRef" value="${escapeHtml(state.selectedNode?.reference || fallbackMock('DemoIEDLD0/MMXU1.TotW.mag.f'))}" placeholder="连接后从模型树选择，或手动输入引用" />
            <label>FC</label><input id="pageReadFc" value="${escapeHtml(state.selectedNode?.fc || fallbackMock('MX'))}" />
          </div>
          <div class="command-row top-gap">
            <button data-action="object:read-manual">读取引用</button>
            <button data-action="object:write">写入选中</button>
            <button data-action="watch:add">加入监视</button>
          </div>
          <div class="kv">
            <div>质量</div><div>连接真实设备时由设备返回值填充；模拟站模式下由 Mock 适配器填充。</div>
            <div>时标</div><div>后端统一记录读取时间，设备时标解析可在 MmsValue 解析层继续细化。</div>
          </div>
        </div>
      </div>
      <div class="section">
        <div class="section-header"><span>最近读取</span><button data-action="object:clear-values">清空</button></div>
        <div class="section-body">
          ${renderTable(state.readValues, [
            { title: '时间', value: (item) => item.timestamp },
            { title: '引用', value: (item) => item.reference },
            { title: 'FC', value: (item) => item.fc },
            { title: '类型', value: (item) => item.type },
            { title: '值', value: (item) => item.value || item.error }
          ])}
        </div>
      </div>
    </div>
  `;
}

function renderDataSetPage() {
  const sets = dataSetNodes();
  const activeReference = state.datasetReference || sets[0]?.reference || '';
  return `
    <div class="page-layout two">
      <div class="section">
        <div class="section-header"><span>数据集列表</span><button data-action="model:refresh">刷新模型</button></div>
        <div class="section-body">
          ${sets.length === 0 ? '<div class="empty-note">当前模型中未发现数据集。请先连接服务端并刷新在线模型。</div>' : `
          <div class="dataset-list">
            ${sets.map((node) => `
              <button class="dataset-list-item ${activeReference === node.reference ? 'active' : ''}" data-action="dataset:select" data-ref="${escapeHtml(node.reference)}">
                <span>${escapeHtml(node.name)}</span>
                <span>${escapeHtml(node.reference)}</span>
                <span>${(node.children || []).length} 个成员</span>
              </button>
            `).join('')}
          </div>
          `}
          <div class="command-row top-gap">
            <button data-action="dataset:read" ${activeReference ? '' : 'disabled'}>打开/刷新选中数据集</button>
            <button data-action="dataset:mark-all-tested" ${state.datasetSnapshot?.reference ? '' : 'disabled'}>全部标为已测</button>
            <button data-action="dataset:clear-tested" ${state.datasetSnapshot?.reference ? '' : 'disabled'}>清除本数据集标记</button>
          </div>
          <div class="kv result-summary">
            <div>当前数据集</div><div>${escapeHtml(activeReference || '-')}</div>
            <div>使用方式</div><div>从模型读取到的数据集列表中选择，不需要手工填写引用。dU 来源于数据对象的 dU 属性。</div>
          </div>
          <input id="datasetRef" type="hidden" value="${escapeHtml(activeReference)}" />
        </div>
      </div>
      <div class="section dataset-workspace">
        <div class="section-header"><span>点位巡检</span><button data-action="dataset:read" ${activeReference ? '' : 'disabled'}>刷新值</button></div>
        <div class="section-body">
          <div id="datasetResult">${renderDataSetResult()}</div>
        </div>
      </div>
    </div>
  `;
}

function renderReportsPage() {
  const reports = state.workspace?.recentReports || [];
  const nodes = reportNodes();
  const activeReference = state.selectedReportReference || nodes[0]?.reference || '';
  const block = selectedReportBlock();
  const logicalNode = block?.reference ? logicalNodeFromReference(block.reference) : (state.reportLogicalNode || selectedLogicalNode() || (state.workspace?.connection?.mock ? 'DemoIEDLD0/LLN0' : ''));
  const dataSetReady = block?.dataSet && state.datasetSnapshot?.reference === block.dataSet;
  return `
    <div class="page-layout two">
      <div class="section">
        <div class="section-header"><span>报告控制块列表</span><button data-action="model:refresh">刷新模型</button></div>
        <div class="section-body">
          ${nodes.length === 0 ? '<div class="empty-note">当前模型中未发现报告控制块。请先连接服务端并刷新在线模型。</div>' : `
          <div class="dataset-list report-list">
            ${nodes.map((node) => `
              <button class="dataset-list-item ${activeReference === node.reference ? 'active' : ''}" data-action="report:select" data-ref="${escapeHtml(node.reference)}" data-linked-ref="${escapeHtml(node.linkedReference || '')}" data-kind="${escapeHtml(node.kind || '')}">
                <span>${escapeHtml(node.name)}</span>
                <span>${escapeHtml(node.reference)}</span>
                <span>${escapeHtml(node.kind)} ${node.linkedReference ? '-> DS' : ''}</span>
              </button>
            `).join('')}
          </div>
          `}
          <div class="command-row top-gap">
            <button data-action="report:list" ${logicalNode ? '' : 'disabled'}>读取当前值</button>
            <button data-action="report:configure" ${block?.reference ? '' : 'disabled'}>设置并启用</button>
            <button data-action="report:disable" ${block?.reference ? '' : 'disabled'}>停用</button>
          </div>
          <input id="reportLn" type="hidden" value="${escapeHtml(logicalNode || '')}" />
          <div class="kv result-summary">
            <div>当前 RCB</div><div class="mono-cell">${escapeHtml(block?.reference || activeReference || '-')}</div>
            <div>关联数据集</div><div class="mono-cell">${escapeHtml(block?.dataSet || '-')}</div>
          </div>
          <div id="reportBlocks" class="top-gap">${renderReportBlocks()}</div>
        </div>
      </div>
      <div class="section report-detail">
        <div class="section-header"><span>报告详情与关联数据集</span><button data-action="report:read-dataset" ${block?.dataSet ? '' : 'disabled'}>刷新点表</button></div>
        <div class="section-body">
          ${renderReportParameters(block)}
          <div class="subsection-title">关联数据集点位</div>
          ${dataSetReady ? renderDataSetResult() : '<div class="empty-note">选择 RCB 后会自动读取其 DatSet 成员和值；启用后，服务端报告上送会自动刷新这里的值。</div>'}
          <div class="subsection-title">最近报告</div>
          ${renderTable(reports, [
            { title: '时间', value: (item) => item.time },
            { title: 'RCB', value: (item) => item.rcbReference },
            { title: '数据集', value: (item) => item.dataSet },
            { title: '原因', value: (item) => item.reason },
            { title: '值数量', value: (item) => item.values?.length || 0 }
          ])}
        </div>
      </div>
    </div>
  `;
}

function renderControlPage() {
  return `
    <div class="page-layout two">
      <div class="section">
        <div class="section-header"><span>控制操作</span><button data-action="control:operate">执行</button></div>
        <div class="section-body">
          <div class="grid-form">
            <label>控制对象</label><input id="controlRef" value="${escapeHtml(state.selectedNode?.reference || (state.workspace?.connection?.mock ? 'DemoIEDLD0/CSWI1.Pos' : ''))}" placeholder="选择可控对象或输入引用" />
            <label>控制模型</label>
            <select id="controlModel">
              <option value="direct">direct</option>
              <option value="sbo">select-before-operate</option>
              <option value="enhanced-sbo">enhanced-sbo</option>
            </select>
            <label>值</label><input id="controlValue" value="true" />
          </div>
          <div class="command-row top-gap">
            <button data-action="control:operate">安全执行</button>
          </div>
        </div>
      </div>
      <div class="section">
        <div class="section-header"><span>安全边界</span></div>
        <div class="section-body">
          <div class="kv">
            <div>二次确认</div><div>真实产品应增加操作票、权限、设备闭锁状态校验。</div>
            <div>SBO 流程</div><div>Select、Operate、Cancel 需要按 ctlModel 和设备返回状态细化。</div>
            <div>当前状态</div><div>后端接口已预留，Mock 模式记录操作事件。</div>
          </div>
        </div>
      </div>
    </div>
  `;
}

function renderFilesPage() {
  return `
    <div class="page-layout two">
      <div class="section">
        <div class="section-header"><span>文件服务</span><button data-action="file:list">刷新</button></div>
        <div class="section-body">
          <div class="grid-form">
            <label>目录</label><input id="fileDir" value="/" />
          </div>
          <div class="command-row top-gap">
            <button data-action="file:list">列目录</button>
            <button data-action="file:read">读取文件</button>
            <button data-action="file:delete">删除文件</button>
          </div>
          <div id="fileResult">${renderFileResult()}</div>
        </div>
      </div>
      <div class="section">
        <div class="section-header"><span>日志查询</span><button data-action="log:query">查询</button></div>
        <div class="section-body">
          <div class="grid-form">
            <label>逻辑节点</label><input id="logLn" value="${state.workspace?.connection?.mock ? 'DemoIEDLD0/LLN0' : ''}" placeholder="例如 LD0/LLN0" />
            <label>日志引用</label><input id="logRef" value="${state.workspace?.connection?.mock ? 'DemoIEDLD0/LLN0.EventLog' : ''}" placeholder="例如 LD0/LLN0.EventLog" />
          </div>
          <div class="command-row top-gap">
            <button data-action="log:query">查询日志</button>
          </div>
          <div id="logResult">${renderLogResult()}</div>
        </div>
      </div>
    </div>
  `;
}

function renderSettingsPage() {
  return `
    <div class="page-layout two">
      <div class="section">
        <div class="section-header"><span>定值组</span><button data-action="settings:read">读取</button></div>
        <div class="section-body">
          <div class="grid-form">
            <label>逻辑节点</label><input id="settingLn" value="${escapeHtml(selectedLogicalNode() || (state.workspace?.connection?.mock ? 'DemoIEDLD0/LLN0' : ''))}" placeholder="例如 LD0/LLN0" />
            <label>活动组</label><input id="settingGroup" value="1" />
          </div>
          <div class="command-row top-gap">
            <button data-action="settings:read">读取状态</button>
            <button data-action="settings:set">切换活动组</button>
          </div>
          <div id="settingResult">${renderSettingResult()}</div>
        </div>
      </div>
      <div class="section">
        <div class="section-header"><span>工程规则</span></div>
        <div class="section-body">
          <div class="kv">
            <div>读写边界</div><div>SG 是当前活动组，SE 是可编辑组，切换前需确认设备允许在线修改。</div>
            <div>审计</div><div>真实产品应记录操作人、修改前后值、审批单号和设备响应。</div>
          </div>
        </div>
      </div>
    </div>
  `;
}

function renderTrafficPage() {
  const traffic = state.workspace?.traffic || {};
  return `
    <div class="page-layout two">
      <div class="section">
        <div class="section-header"><span>通信统计</span><button data-action="traffic:refresh">刷新</button></div>
        <div class="section-body">
          <div class="kv">
            <div>MMS 请求</div><div>${traffic.requests || 0}</div>
            <div>MMS 响应</div><div>${traffic.responses || 0}</div>
            <div>报告</div><div>${traffic.reports || 0}</div>
            <div>GOOSE 帧</div><div>${traffic.gooseFrames || 0}</div>
            <div>SV 帧</div><div>${traffic.sampledValueFrames || 0}</div>
            <div>平均延迟</div><div>${traffic.averageLatencyMs || 0} ms</div>
          </div>
        </div>
      </div>
      <div class="section">
        <div class="section-header"><span>GOOSE / SV 订阅</span></div>
        <div class="section-body">
          <div class="grid-form">
            <label>AppID / SvID</label><input id="subId" value="${fallbackMock('DemoGoose')}" placeholder="输入 GOOSE AppID 或 SV ID" />
            <label>网卡</label><input id="subIf" value="Ethernet" />
          </div>
          <div class="command-row top-gap">
            <button data-action="goose:subscribe">订阅 GOOSE</button>
            <button data-action="sv:subscribe">订阅 SV</button>
          </div>
        </div>
      </div>
    </div>
  `;
}

function selectedLogicalNode() {
  const ref = state.selectedNode?.reference || '';
  const slash = ref.indexOf('/');
  if (slash < 0) return '';
  const dot = ref.indexOf('.', slash);
  return dot > 0 ? ref.slice(0, dot) : ref;
}

async function connect(mock = false) {
  const host = mock ? 'MockBay' : dom.hostInput.value.trim();
  const port = Number.parseInt(dom.portInput.value, 10) || 102;
  const response = await withBusy(
    mock ? '连接模拟站' : '连接 IED',
    mock ? '正在加载模拟站模型...' : `正在连接 ${host}:${port}，并读取在线模型...`,
    () => api.call('Connect', {
      host,
      port,
      useTls: false,
      username: '',
      password: '',
      mock,
      timeoutMs: 5000
    })
  );
  logEvent(response.ok ? 'INFO' : 'ERROR', '连接', response.message || '连接命令完成');
  await refreshWorkspace();
}

async function disconnect() {
  const response = await withBusy('断开连接', '正在断开 IED 连接...', () => api.call('Disconnect', { reason: '用户断开' }));
  logEvent(response.ok ? 'INFO' : 'WARN', '连接', response.message || '连接已断开');
  await refreshWorkspace();
}

async function refreshModel() {
  const response = await withBusy('读取在线模型', '正在枚举 LD、LN、DO、DA，请等待设备响应...', () => api.call('RefreshModel', { force: true }));
  if (response?.roots) {
    state.workspace = {
      ...(state.workspace || {}),
      model: response
    };
    state.modelSignature = modelSignature(response);
    state.pagesDirty = true;
  }
  logEvent('INFO', '模型', '模型刷新完成');
  renderAll();
}

async function readSelected(manual = false) {
  const refInput = document.getElementById('pageReadRef');
  const fcInput = document.getElementById('pageReadFc');
  const reference = manual ? refInput?.value : state.selectedNode?.reference;
  const fc = manual ? fcInput?.value : state.selectedNode?.fc;
  await readObjectReference(reference, fc);
}

async function readObjectReference(reference, fc) {
  if (!reference) {
    logEvent('WARN', '读取', '未选择或输入对象引用');
    return;
  }
  const value = await withBusy('读取数据对象', `正在读取 ${reference}...`, () => api.call('ReadObject', { reference, fc: fc || 'MX' }));
  state.readValues.unshift(value);
  state.readValues = state.readValues.slice(0, 100);
  logEvent(value.error ? 'ERROR' : 'INFO', '读取', `${reference} = ${value.value || value.error}`);
  state.pagesDirty = true;
  renderAll();
}

function setInputValue(id, value) {
  const input = document.getElementById(id);
  if (input) input.value = value || '';
}

function focusDataReference(reference, fc = '') {
  openModule('data', true);
  setInputValue('pageReadRef', reference);
  setInputValue('pageReadFc', fc || 'MX');
}

function openWriteDialog() {
  const node = state.selectedNode;
  dom.writeRef.value = node?.reference || document.getElementById('pageReadRef')?.value || '';
  dom.writeFc.value = node?.fc || document.getElementById('pageReadFc')?.value || 'MX';
  dom.writeValue.value = '';
  dom.writeDialog.showModal();
}

async function writeObject() {
  const response = await withBusy('写入数据对象', `正在写入 ${dom.writeRef.value.trim()}...`, () => api.call('WriteObject', {
    reference: dom.writeRef.value.trim(),
    fc: dom.writeFc.value.trim() || 'MX',
    type: dom.writeType.value,
    value: dom.writeValue.value
  }));
  logEvent(response.ok ? 'INFO' : 'ERROR', '写入', response.message || '写入完成');
  await refreshWorkspace();
}

async function readDataset() {
  const ref = document.getElementById('datasetRef')?.value || fallbackMock('DemoIEDLD0/LLN0.Events');
  await readDatasetReference(ref);
}

function focusDatasetReference(reference) {
  state.datasetReference = reference || '';
  openModule('datasets', true);
  setInputValue('datasetRef', reference);
}

async function readDatasetReference(ref) {
  await loadDataSetSnapshot(ref, true);
}

async function loadDataSetSnapshot(ref, activate = true) {
  if (!ref) {
    logEvent('WARN', '数据集', '请输入数据集引用');
    return;
  }
  state.datasetReference = ref;
  if (activate) focusDatasetReference(ref);
  else setInputValue('datasetRef', ref);
  const data = await withBusy('读取数据集', `正在读取 ${ref} 的成员和值...`, () => api.call('ReadDataSet', { reference: ref }));
  state.datasetSnapshot = data;
  logEvent('INFO', '数据集', `读取 ${ref}`);
  state.pagesDirty = true;
  renderAll();
}

async function selectDataSet(reference) {
  if (!reference) return;
  state.datasetReference = reference;
  await readDatasetReference(reference);
}

function markCurrentDataSet(tested) {
  const snapshot = state.datasetSnapshot;
  if (!snapshot?.reference) return;
  for (const point of snapshot.points || []) {
    setPointTested(snapshot.reference, point.objectReference, tested);
  }
  renderAll();
}

async function commandReply(method, payload, source) {
  const response = await withBusy(source, '正在等待后端和设备响应...', () => api.call(method, payload));
  logEvent(response.ok ? 'INFO' : 'WARN', source, response.message || `${method} 完成`);
  await refreshWorkspace();
}

async function listReports() {
  const logicalNode = document.getElementById('reportLn')?.value || selectedLogicalNode() || fallbackMock('DemoIEDLD0/LLN0');
  await listReportsForLogicalNode(logicalNode);
}

async function listReportsForLogicalNode(logicalNode) {
  if (!logicalNode) {
    logEvent('WARN', '报告', '请输入逻辑节点引用');
    return;
  }
  state.reportLogicalNode = logicalNode;
  openModule('reports', true);
  setInputValue('reportLn', logicalNode);
  const data = await withBusy('枚举报告控制块', `正在读取 ${logicalNode} 的 URCB/BRCB...`, () => api.call('GetReportControlBlocks', { logicalNode }));
  state.reportBlocks = data.blocks || [];
  if (!state.selectedReportReference && state.reportBlocks[0]?.reference) {
    state.selectedReportReference = state.reportBlocks[0].reference;
  }
  logEvent('INFO', '报告', `枚举 ${logicalNode}，共 ${state.reportBlocks.length} 个 RCB`);
  state.pagesDirty = true;
  renderAll();
}

function focusReportReference(reference, linkedReference = '', kind = '') {
  const logicalNode = logicalNodeFromReference(reference);
  state.reportLogicalNode = logicalNode;
  state.selectedReportReference = reference;
  state.reportBlocks = [{
    reference,
    dataSet: linkedReference,
    buffered: kind === 'BRCB' || reference.includes('.BR.'),
    rptId: '',
    enabled: false,
    bufTimeMs: 100,
    integrityPeriodMs: 1000,
    triggerOptions: '',
    optionalFields: ''
  }];
  openModule('reports', true);
  setInputValue('reportLn', logicalNode);
}

async function selectReport(reference, linkedReference = '', kind = '') {
  if (!reference) return;
  state.selectedReportReference = reference;
  const logicalNode = logicalNodeFromReference(reference);
  state.reportLogicalNode = logicalNode;
  openModule('reports', true);
  const data = await withBusy('读取报告控制块', `正在读取 ${logicalNode} 的 RCB 当前参数...`, () => api.call('GetReportControlBlocks', { logicalNode }));
  state.reportBlocks = data.blocks || [];
  let block = selectedReportBlock();
  if (!block) {
    focusReportReference(reference, linkedReference, kind);
    block = selectedReportBlock();
  }
  if (block?.dataSet) {
    await loadDataSetSnapshot(block.dataSet, false);
  } else {
    state.pagesDirty = true;
    renderAll();
  }
}

function reportPayload(block, enabled) {
  return {
    reference: block.reference || '',
    rptId: block.rptId || '',
    dataSet: block.dataSet || '',
    enabled,
    buffered: Boolean(block.buffered),
    bufTimeMs: Number(block.bufTimeMs || 0),
    integrityPeriodMs: Number(block.integrityPeriodMs || 0),
    triggerOptions: block.triggerOptions || '',
    optionalFields: block.optionalFields || '',
    confRev: Number(block.confRev || 0),
    sqNum: Number(block.sqNum || 0),
    gi: Boolean(block.gi),
    purgeBuf: Boolean(block.purgeBuf),
    reservation: Boolean(block.reservation),
    reservationTimeS: Number(block.reservationTimeS || 0),
    owner: block.owner || ''
  };
}

function openReportDialog(block = selectedReportBlock()) {
  if (!block?.reference) {
    logEvent('WARN', '报告', '请先选择报告控制块');
    return;
  }
  dom.reportRef.value = block.reference || '';
  dom.reportRptId.value = block.rptId || '';
  dom.reportDataSet.value = block.dataSet || '';
  dom.reportEnabled.checked = true;
  dom.reportBuffered.checked = Boolean(block.buffered);
  dom.reportBufTm.value = Number(block.bufTimeMs || 0);
  dom.reportIntgPd.value = Number(block.integrityPeriodMs || 0);
  dom.reportTrgOps.value = block.triggerOptions || '';
  dom.reportOptFlds.value = block.optionalFields || '';
  dom.reportGi.checked = block.gi !== false;
  dom.reportPurgeBuf.checked = Boolean(block.purgeBuf);
  dom.reportReservation.checked = Boolean(block.reservation);
  dom.reportResvTms.value = Number(block.reservationTimeS || 0);
  dom.reportDialog.showModal();
}

async function applyReportDialog() {
  const block = {
    reference: dom.reportRef.value.trim(),
    rptId: dom.reportRptId.value.trim(),
    dataSet: dom.reportDataSet.value.trim(),
    enabled: dom.reportEnabled.checked,
    buffered: dom.reportBuffered.checked,
    bufTimeMs: Number(dom.reportBufTm.value || 0),
    integrityPeriodMs: Number(dom.reportIntgPd.value || 0),
    triggerOptions: dom.reportTrgOps.value.trim(),
    optionalFields: dom.reportOptFlds.value.trim(),
    gi: dom.reportGi.checked,
    purgeBuf: dom.reportPurgeBuf.checked,
    reservation: dom.reportReservation.checked,
    reservationTimeS: Number(dom.reportResvTms.value || 0)
  };
  await commandReply('SetReportControlBlock', reportPayload(block, block.enabled), '报告');
  if (block.dataSet) await loadDataSetSnapshot(block.dataSet, false);
  await listReportsForLogicalNode(logicalNodeFromReference(block.reference));
}

async function setReportReference(reference, linkedReference, kind, enabled) {
  if (!reference) {
    logEvent('WARN', '报告', '未选择报告控制块');
    return;
  }
  await selectReport(reference, linkedReference, kind);
  const block = selectedReportBlock() || {
    reference,
    dataSet: linkedReference || '',
    buffered: kind === 'BRCB' || reference.includes('.BR.'),
    bufTimeMs: 100,
    integrityPeriodMs: 1000,
    triggerOptions: '19',
    optionalFields: '31',
    gi: true
  };
  if (enabled) openReportDialog(block);
  else await commandReply('SetReportControlBlock', reportPayload(block, false), '报告');
}

function jumpToReference(reference) {
  if (!reference) {
    logEvent('WARN', '跳转', '没有可跳转的关联引用');
    return;
  }
  const normalized = String(reference).replaceAll('$', '.');
  const target = findNodeFlexible(normalized);
  if (!target) {
    logEvent('WARN', '跳转', `当前模型树中未找到 ${normalized}`);
    if (normalized.includes('/')) focusDataReference(normalized);
    return;
  }

  state.selectedNode = target;
  renderModelTree();
  renderProperties();

  if (target.kind === 'DS') {
    focusDatasetReference(target.reference);
  } else if (target.kind === 'URCB' || target.kind === 'BRCB') {
    focusReportReference(target.reference, target.linkedReference, target.kind);
  } else {
    focusDataReference(preferredReference(target), target.fc || '');
  }
  logEvent('INFO', '跳转', `已定位 ${target.reference}`);
}

async function copyText(text) {
  if (!text) return;
  try {
    await navigator.clipboard.writeText(text);
    logEvent('INFO', '剪贴板', `已复制 ${text}`);
  } catch {
    logEvent('WARN', '剪贴板', `无法访问剪贴板：${text}`);
  }
}

async function listFiles() {
  const directory = document.getElementById('fileDir')?.value || '/';
  const data = await withBusy('读取文件目录', `正在读取目录 ${directory}...`, () => api.call('GetFiles', { directory }));
  state.fileEntries = data.entries || [];
  state.fileContent = null;
  logEvent('INFO', '文件', `列目录 ${directory}`);
  state.pagesDirty = true;
  renderAll();
}

async function queryLogs() {
  const logicalNode = document.getElementById('logLn')?.value || fallbackMock('DemoIEDLD0/LLN0');
  const logReference = document.getElementById('logRef')?.value || '';
  if (!logicalNode && !logReference) {
    logEvent('WARN', '日志', '请输入逻辑节点或日志引用');
    return;
  }
  const data = await withBusy('查询日志', `正在查询 ${logReference || logicalNode}...`, () => api.call('GetLogs', { logicalNode, logReference, since: '' }));
  state.logEntries = data.entries || [];
  logEvent('INFO', '日志', `查询 ${logReference || logicalNode}`);
  state.pagesDirty = true;
  renderAll();
}

async function readSettings() {
  const logicalNode = document.getElementById('settingLn')?.value || fallbackMock('DemoIEDLD0/LLN0');
  if (!logicalNode) {
    logEvent('WARN', '定值组', '请输入逻辑节点引用');
    return;
  }
  const data = await withBusy('读取定值组', `正在读取 ${logicalNode} 的 SGCB...`, () => api.call('GetSettingGroups', { logicalNode }));
  state.settingState = data;
  logEvent('INFO', '定值组', `读取 ${logicalNode}`);
  state.pagesDirty = true;
  renderAll();
}

async function refreshWorkspace() {
  const data = await api.getWorkspace();
  applyWorkspace(data, true);
  renderAll();
}

function applyWorkspace(workspace, force = false) {
  const previousConnection = state.workspace?.connection || {};
  const previousSignature = state.modelSignature;
  state.workspace = workspace;
  const liveChanged = applyLiveValuesToSnapshot(workspace?.liveValues || []);
  const nextSignature = modelSignature(workspace?.model);
  const connectionChanged = previousConnection.connected !== workspace?.connection?.connected ||
    previousConnection.mock !== workspace?.connection?.mock ||
    previousConnection.host !== workspace?.connection?.host ||
    previousConnection.port !== workspace?.connection?.port;

  if (force || previousSignature !== nextSignature || connectionChanged || liveChanged) {
    state.modelSignature = nextSignature;
    state.pagesDirty = true;
  }
  if (!findNodeFlexible(state.datasetReference)) {
    state.datasetReference = dataSetNodes()[0]?.reference || '';
    state.datasetSnapshot = null;
  }
}

function togglePane(pane) {
  dom.workspace.classList.toggle(`hidden-pane-${pane}`);
}

function resetLayout() {
  dom.workspace.classList.remove('hidden-pane-left', 'hidden-pane-right', 'hidden-pane-bottom');
  document.documentElement.style.setProperty('--left-pane', '300px');
  document.documentElement.style.setProperty('--right-pane', '320px');
  document.documentElement.style.setProperty('--bottom-pane', '190px');
}

async function detachCurrent() {
  const mod = moduleById(state.activeModule);
  await api.detachModule(mod.id, mod.title);
}

async function handleAction(action) {
  const type = action?.type || action;
  switch (type) {
    case 'connect:open':
      dom.dialogHost.value = dom.hostInput.value;
      dom.dialogPort.value = dom.portInput.value;
      dom.dialogMock.checked = false;
      dom.connectDialog.showModal();
      break;
    case 'connect:mock':
      await connect(true);
      break;
    case 'connect:disconnect':
      await disconnect();
      break;
    case 'model:refresh':
      await refreshModel();
      break;
    case 'module:open':
      openModule(action.moduleId);
      break;
    case 'module:detach':
      await detachCurrent();
      break;
    case 'pane:toggle':
      togglePane(action.pane);
      break;
    case 'layout:reset':
      resetLayout();
      break;
    case 'node:copy':
      await copyText(action.reference || state.selectedNode?.reference || state.contextNode?.reference);
      break;
    case 'node:jump-link':
      jumpToReference(action.linkedReference || state.selectedNode?.linkedReference || state.contextNode?.linkedReference);
      break;
    case 'object:read':
      await readSelected(false);
      break;
    case 'object:read-manual':
      await readSelected(true);
      break;
    case 'object:read-node':
      await readObjectReference(action.linkedReference || action.reference || preferredReference(state.selectedNode), action.fc || state.selectedNode?.fc || '');
      break;
    case 'object:write':
      openWriteDialog();
      break;
    case 'object:clear-values':
      state.readValues = [];
      renderAll();
      break;
    case 'watch:add':
      logEvent('INFO', '监视', state.selectedNode ? `加入监视 ${state.selectedNode.reference}` : '未选择对象');
      break;
    case 'dataset:read':
      await readDataset();
      break;
    case 'dataset:select':
      await selectDataSet(action.reference || '');
      break;
    case 'dataset:open-node':
      focusDatasetReference(action.reference || state.selectedNode?.reference || state.contextNode?.reference || '');
      break;
    case 'dataset:read-node':
      await readDatasetReference(action.reference || state.selectedNode?.reference || state.contextNode?.reference || '');
      break;
    case 'dataset:toggle-point':
      await togglePointExpanded(action.objectReference || '');
      break;
    case 'dataset:toggle-tested':
      setPointTested(state.datasetSnapshot?.reference || state.datasetReference || '', action.objectReference || '', action.checked);
      renderAll();
      break;
    case 'dataset:mark-all-tested':
      markCurrentDataSet(true);
      break;
    case 'dataset:clear-tested':
      markCurrentDataSet(false);
      break;
    case 'dataset:create':
      await commandReply('CreateDataSet', { reference: document.getElementById('datasetRef')?.value || '', members: [] }, '数据集');
      break;
    case 'dataset:delete':
      await commandReply('DeleteDataSet', { reference: document.getElementById('datasetRef')?.value || '' }, '数据集');
      break;
    case 'report:list':
      await listReports();
      break;
    case 'report:list-node':
      await listReportsForLogicalNode(action.reference || state.selectedNode?.reference || state.contextNode?.reference || '');
      break;
    case 'report:select':
      await selectReport(action.reference || '', action.linkedReference || '', action.kind || '');
      break;
    case 'report:open-node':
      await selectReport(action.reference || state.selectedNode?.reference || state.contextNode?.reference || '', action.linkedReference || state.selectedNode?.linkedReference || state.contextNode?.linkedReference || '', action.kind || state.selectedNode?.kind || state.contextNode?.kind || '');
      break;
    case 'report:enable-node':
      await setReportReference(action.reference || state.selectedNode?.reference || state.contextNode?.reference || '', action.linkedReference || state.selectedNode?.linkedReference || state.contextNode?.linkedReference || '', action.kind || state.selectedNode?.kind || state.contextNode?.kind || '', true);
      break;
    case 'report:disable-node':
      await setReportReference(action.reference || state.selectedNode?.reference || state.contextNode?.reference || '', action.linkedReference || state.selectedNode?.linkedReference || state.contextNode?.linkedReference || '', action.kind || state.selectedNode?.kind || state.contextNode?.kind || '', false);
      break;
    case 'report:configure':
    case 'report:enable':
      openReportDialog(selectedReportBlock());
      break;
    case 'report:apply-dialog':
      await applyReportDialog();
      break;
    case 'report:read-dataset':
      if (selectedReportBlock()?.dataSet) await loadDataSetSnapshot(selectedReportBlock().dataSet, false);
      break;
    case 'report:disable':
      const block = state.reportBlocks[0] || {};
      const selectedBlock = selectedReportBlock() || block;
      const reportReference = selectedBlock.reference || fallbackMock('DemoIEDLD0/LLN0.RP.EventsRCB01');
      if (!reportReference) {
        logEvent('WARN', '报告', '请先枚举或输入报告控制块引用');
        break;
      }
      await commandReply('SetReportControlBlock', reportPayload(selectedBlock, false), '报告');
      await listReportsForLogicalNode(logicalNodeFromReference(reportReference));
      break;
    case 'control:operate':
      await commandReply('OperateControl', {
        reference: document.getElementById('controlRef')?.value || '',
        ctlModel: document.getElementById('controlModel')?.value || 'direct',
        value: document.getElementById('controlValue')?.value || '',
        selectBeforeOperate: document.getElementById('controlModel')?.value !== 'direct',
        operateTimeoutMs: 5000
      }, '控制');
      break;
    case 'file:list':
      await listFiles();
      break;
    case 'file:read':
      state.fileContent = await withBusy('读取文件', `正在读取 ${document.getElementById('fileDir')?.value || '/'}...`, () => api.call('ReadFile', { path: document.getElementById('fileDir')?.value || '/' }));
      logEvent('INFO', '文件', `读取 ${state.fileContent.path || ''}`);
      state.pagesDirty = true;
      renderAll();
      break;
    case 'file:delete':
      await commandReply('DeleteFile', { path: document.getElementById('fileDir')?.value || '/' }, '文件');
      break;
    case 'log:query':
      await queryLogs();
      break;
    case 'settings:read':
      await readSettings();
      break;
    case 'settings:set':
      await commandReply('SetActiveSettingGroup', {
        logicalNode: document.getElementById('settingLn')?.value || '',
        activeGroup: Number.parseInt(document.getElementById('settingGroup')?.value || '1', 10)
      }, '定值组');
      break;
    case 'goose:subscribe':
      await commandReply('SubscribeGoose', {
        appId: document.getElementById('subId')?.value || '',
        interfaceName: document.getElementById('subIf')?.value || '',
        destinationMac: ''
      }, 'GOOSE');
      break;
    case 'sv:subscribe':
      await commandReply('SubscribeSampledValues', {
        svId: document.getElementById('subId')?.value || '',
        interfaceName: document.getElementById('subIf')?.value || ''
      }, 'SV');
      break;
    case 'traffic:refresh':
      await refreshWorkspace();
      break;
    case 'project:new':
    case 'project:open':
    case 'project:save':
      logEvent('INFO', '工程', '工程文件管理接口已预留，可扩展为保存连接、布局、监视表和报告订阅。');
      break;
    default:
      logEvent('WARN', 'UI', `未知命令 ${type}`);
  }
}

function bindEvents() {
  document.addEventListener('click', async (event) => {
    const actionEl = event.target.closest('[data-action]');
    const menuEl = event.target.closest('#contextMenu');
    const moduleEl = event.target.closest('[data-module]');
    const tabEl = event.target.closest('[data-tab]');
    const closeEl = event.target.closest('[data-close-tab]');
    const nodeEl = event.target.closest('.tree-node');

    if (actionEl) {
      hideContextMenu();
      await handleAction({
        type: actionEl.dataset.action,
        pane: actionEl.dataset.pane,
        reference: actionEl.dataset.ref,
        linkedReference: actionEl.dataset.linkedRef,
        kind: actionEl.dataset.kind,
        fc: actionEl.dataset.fc,
        objectReference: actionEl.dataset.objectRef,
        checked: Boolean(actionEl.checked)
      });
      return;
    }
    if (!menuEl) hideContextMenu();
    if (closeEl) {
      closeModule(closeEl.dataset.closeTab);
      return;
    }
    if (moduleEl) {
      openModule(moduleEl.dataset.module);
      return;
    }
    if (tabEl) {
      openModule(tabEl.dataset.tab);
      return;
    }
    if (nodeEl) {
      state.selectedNode = findNode(nodeEl.dataset.ref);
      renderModelTree();
      renderProperties();
      if (state.selectedNode?.kind === 'DO' || state.selectedNode?.kind === 'DA' || state.selectedNode?.kind === 'FCDA') openModule('data', true);
    }
  });

  document.addEventListener('contextmenu', (event) => {
    const nodeEl = event.target.closest('.tree-node');
    if (!nodeEl) {
      hideContextMenu();
      return;
    }
    event.preventDefault();
    const node = findNode(nodeEl.dataset.ref);
    if (!node) return;
    state.selectedNode = node;
    renderModelTree();
    renderProperties();
    showContextMenu(event.clientX, event.clientY, node);
  });

  window.addEventListener('blur', hideContextMenu);

  dom.treeFilter.addEventListener('input', renderModelTree);

  document.getElementById('dialogConnect').addEventListener('click', async (event) => {
    event.preventDefault();
    dom.hostInput.value = dom.dialogHost.value;
    dom.portInput.value = dom.dialogPort.value;
    dom.connectDialog.close();
    await connect(dom.dialogMock.checked);
  });

  document.getElementById('dialogWrite').addEventListener('click', async (event) => {
    event.preventDefault();
    dom.writeDialog.close();
    await writeObject();
  });

  document.getElementById('dialogReportApply').addEventListener('click', async (event) => {
    event.preventDefault();
    dom.reportDialog.close();
    await applyReportDialog();
  });

  api.onMenuAction((action) => handleAction(action));
  api.onWorkspace((workspace) => {
    applyWorkspace(workspace);
    renderLight();
  });
  api.onBackendLog((line) => logEvent(line.level || 'INFO', '后端', line.text));
}

function bindSplitters() {
  let active = null;
  document.querySelectorAll('.splitter').forEach((splitter) => {
    splitter.addEventListener('pointerdown', (event) => {
      active = {
        name: splitter.dataset.splitter,
        startX: event.clientX,
        startY: event.clientY,
        left: Number.parseInt(getComputedStyle(document.documentElement).getPropertyValue('--left-pane'), 10),
        right: Number.parseInt(getComputedStyle(document.documentElement).getPropertyValue('--right-pane'), 10),
        bottom: Number.parseInt(getComputedStyle(document.documentElement).getPropertyValue('--bottom-pane'), 10)
      };
      splitter.setPointerCapture(event.pointerId);
    });
  });

  document.addEventListener('pointermove', (event) => {
    if (!active) return;
    if (active.name === 'left') {
      const next = Math.max(220, Math.min(520, active.left + event.clientX - active.startX));
      document.documentElement.style.setProperty('--left-pane', `${next}px`);
    }
    if (active.name === 'right') {
      const next = Math.max(240, Math.min(560, active.right - (event.clientX - active.startX)));
      document.documentElement.style.setProperty('--right-pane', `${next}px`);
    }
    if (active.name === 'bottom') {
      const next = Math.max(110, Math.min(420, active.bottom - (event.clientY - active.startY)));
      document.documentElement.style.setProperty('--bottom-pane', `${next}px`);
    }
  });

  document.addEventListener('pointerup', () => {
    active = null;
  });
}

function renderAll() {
  renderStatus();
  renderModuleList();
  renderTabs();
  renderModelTree();
  renderProperties();
  renderPages();
  renderEvents();
  state.pagesDirty = false;
}

function renderLight() {
  renderStatus();
  renderEvents();
  if (state.pagesDirty) {
    renderModelTree();
    renderProperties();
    renderPages();
    state.pagesDirty = false;
  }
}

async function init() {
  loadPointTested();
  state.openTabs = detached ? [moduleById(state.activeModule)] : modules.slice(0, 3);
  bindEvents();
  bindSplitters();
  renderAll();
  await refreshWorkspace();
}

init().catch((error) => {
  logEvent('ERROR', '启动', error.message || String(error));
});
