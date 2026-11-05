import { PacketRepository, visibleRange } from "./packet-repository.mjs";
import { escape, tree, formatHex } from "./protocol-view.js";
import { installAiPanel } from "./ai/panel.mjs";
const $ = (selector) => document.querySelector(selector);
const api = window.capture;
const state = {
  total: 0,
  fileTotal: null,
  selected: null,
  detail: null,
  filter: "",
  generation: 0,
  playing: false,
  playEpoch: 0,
  capturing: false,
  offset: 0,
  capabilities: null,
  mappingEnabled: false,
  mappings: [],
  references: new Set(),
};
const ROW = 27,
  PAGE = 200;
const repository = new PacketRepository({
  request: (args) => api.command("packets", args),
});
function mappingMode(enabled) {
  state.mappingEnabled = enabled;
  $("#replay-mode").textContent = enabled
    ? "本机测量值注入 · 不重发遥控 / 定值写入"
    : "纯离线 · 不重发遥控 / 定值写入";
  $("#mode").textContent = enabled
    ? `本机测量值注入 · ${state.mappings.length} 项映射`
    : "离线分析 · 不发送网络数据";
}
function status(message, error = false) {
  $("#status").textContent = message;
  $("#status").title = message;
  $("#status").classList.toggle("error", error);
}
async function run(fn) {
  try {
    return await fn();
  } catch (error) {
    pause();
    status(error.message, true);
    return null;
  }
}
function time(packet) {
  return Number(
    packet?.relative_time ?? packet?.time_relative ?? packet?.relativeTime ?? 0,
  );
}
function frame(packet) {
  return Number(packet?.frame ?? packet?.number ?? 0);
}
function summary(packet) {
  return [
    frame(packet),
    time(packet),
    packet.source,
    packet.destination,
    packet.protocol,
    packet.info,
  ].join("\t");
}
function packetAt(index) {
  return repository.get(index);
}
async function page(index) {
  const packets = await repository.load(index);
  state.total = repository.total;
  $("#count").textContent = `${state.total.toLocaleString()} 条`;
  $("#timeline").max = String(Math.max(0, state.total - 1));
  return packets;
}
async function renderPackets() {
  const viewport = $("#packet-scroll");
  const { begin, end } = visibleRange({
    scrollTop: viewport.scrollTop,
    height: viewport.clientHeight,
    total: state.total || PAGE,
  });
  state.offset = begin;
  await page(begin);
  if (end > begin) await page(end - 1);
  if (state.offset !== begin) return;
  let html = `<tr class="spacer-row"><td colspan="7"></td></tr>`;
  for (let index = begin; index < Math.min(end, state.total); index++) {
    const packet = packetAt(index);
    if (!packet) continue;
    html += `<tr data-frame="${frame(packet)}" data-index="${index}" class="${state.selected?.frame === frame(packet) ? "selected" : ""}"><td>${frame(packet)}</td><td>${time(packet).toFixed(6)}</td><td title="${escape(packet.source)}">${escape(packet.source)}</td><td title="${escape(packet.destination)}">${escape(packet.destination)}</td><td>${escape(packet.protocol)}</td><td>${escape(packet.length)}</td><td title="${escape(packet.info)}">${escape(packet.info)}</td></tr>`;
  }
  if (!state.total)
    html += `<tr><td colspan="7" class="empty">${escape(emptyMessage())}</td></tr>`;
  html += `<tr class="spacer-row"><td colspan="7"></td></tr>`;
  $("#packet-rows").innerHTML = html;
  const spacers = document.querySelectorAll("#packet-rows .spacer-row td");
  spacers[0].style.height = `${begin * ROW}px`;
  spacers[1].style.height = `${Math.max(0, state.total - end) * ROW}px`;
}
function emptyMessage() {
  if (state.fileTotal === 0)
    return "文件有效，但没有捕获到任何报文。请检查网卡和捕获过滤器；本机 127.0.0.1 通信请选择 Npcap Loopback。";
  return state.filter
    ? "没有匹配显示过滤器的报文，请清除或调整显示过滤器。"
    : "尚未加载报文，请打开抓包文件或开始捕获。";
}
function pause() {
  state.playing = false;
  state.playEpoch++;
  $("#play").textContent = "▶ 播放";
}
async function refresh() {
  pause();
  status("正在建立报文列表索引… 大文件可能需要数十秒，可使用“取消分析”。");
  state.generation++;
  repository.reset(state.filter);
  state.total = 0;
  state.selected = null;
  state.detail = null;
  $("#packet-scroll").scrollTop = 0;
  $("#protocol-tree").innerHTML = '<p class="empty">选择报文查看解析结果。</p>';
  $("#hex").textContent = "";
  $("#correlation").textContent = "";
  $("#values").innerHTML = "";
  await page(0);
  await renderPackets();
  if (state.total) await select(0);
  if (!state.total)
    $("#protocol-tree").innerHTML =
      `<p class="empty">${escape(emptyMessage())}</p>`;
  status(
    state.total
      ? `已加载 ${state.total.toLocaleString()} 条报文`
      : emptyMessage(),
  );
}
function showValues(detail, packet) {
  const values = detail.values || detail.decoded_values || [];
  for (const value of values)
    if (
      value.reference &&
      value.replayable &&
      value.complete &&
      ["ST", "MX"].includes(value.fc) &&
      !Array.isArray(value.value)
    )
      state.references.add(value.reference);
  $("#values").innerHTML = values.length
    ? values
        .map(
          (value) =>
            `<tr><td title="${escape(value.reference || value.name)}">${escape(value.reference || value.label || value.name || "未解析引用")}</td><td>${escape(value.type || "未知")}</td><td title="${escape(value.value)}">${escape(value.value == null ? "未知" : typeof value.value === "object" ? JSON.stringify(value.value) : value.value)}</td><td>${escape(value.quality ?? "未提供")}</td><td>${escape(value.capture_time || value.time || (value.relative_time !== undefined ? `${value.relative_time} s` : packet.time || packet.timestamp || time(packet)))}</td><td>${escape(value.ied_time || value.timestamp || "未提供")}</td><td>${escape(value.source || detail.service || "报文解析")}</td></tr>`,
        )
        .join("")
    : '<tr><td colspan="7" class="empty">当前报文没有可确定的数据值；写请求不代表设备已接受或实际状态。</td></tr>';
}
async function snapshot() {
  if (!state.selected) return;
  status("正在重建已观察数据状态…");
  const selected = state.selected;
  const result = await api.command("replay", {
    mode: "snapshot",
    through_frame: selected.frame,
  });
  if (state.selected !== selected) return;
  showValues({ values: result.values }, selected);
  $("#values-mode").textContent = `累计状态 · 截至帧 ${selected.frame}`;
  status(`重建 ${result.values.length} 个已观察数据值（未知/不完整值不补零）`);
}
async function select(index, { scroll = false } = {}) {
  if (index < 0 || index >= state.total) return;
  const generation = state.generation;
  await page(index);
  if (generation !== state.generation) return;
  const packet = packetAt(index);
  if (!packet) return;
  state.selected = { ...packet, frame: frame(packet), index };
  const selected = state.selected;
  if (scroll) {
    $("#packet-scroll").scrollTop = Math.max(
      0,
      index * ROW - $("#packet-scroll").clientHeight / 2,
    );
    await renderPackets();
  }
  for (const row of document.querySelectorAll("[data-frame]"))
    row.classList.toggle(
      "selected",
      Number(row.dataset.frame) === selected.frame,
    );
  $("#timeline").value = String(index);
  $("#replay-position").textContent = `${index + 1} / ${state.total}`;
  $("#replay-time").textContent = `${time(packet).toFixed(3)} s`;
  status(`解析第 ${selected.frame} 帧…`);
  const detail = await api.command("detail", { frame: selected.frame });
  if (state.selected !== selected) return;
  state.detail = detail;
  $("#values-mode").textContent = "当前帧";
  $("#protocol-tree").innerHTML = tree(
    detail.tree || detail.layers || detail.protocols || detail,
    "报文",
  );
  $("#hex").textContent = formatHex(detail.hex || detail.bytes || detail.raw);
  const c =
    detail.correlation && Object.keys(detail.correlation).length
      ? detail.correlation
      : null;
  $("#correlation").textContent = c
    ? `请求帧 ${c.request_frame ?? "未知"} → 响应帧 ${c.response_frame ?? "未知"}${c.latency_ms !== undefined ? " · " + Number(c.latency_ms).toFixed(3) + " ms" : ""}`
    : detail.warning || "";
  const warnings = detail.warnings || [];
  if (warnings.length)
    $("#correlation").textContent +=
      ($("#correlation").textContent ? " · " : "") + warnings.join(" · ");
  $("#correlation").title = warnings.join("\n");
  showValues(detail, packet);
  status(
    `第 ${selected.frame} 帧 · ${packet.protocol || ""} · ${packet.info || ""}`,
  );
}
async function play() {
  if (state.playing) {
    pause();
    return;
  }
  if (!state.total) return;
  state.playing = true;
  const epoch = ++state.playEpoch;
  $("#play").textContent = "Ⅱ 暂停";
  status("准备已观察数据事件时间线…");
  const events = new PacketRepository({
    pageSize: 200,
    maxPages: 8,
    request: async (args) => {
      const result = await api.command("replay", {
        offset: args.offset,
        limit: args.limit,
      });
      return { packets: result.events, total: result.total };
    },
  });
  await events.load(0);
  if (!events.total) {
    pause();
    status("捕获文件没有可回放的已解析数据值");
    return;
  }
  let packetIndex = state.selected?.index ?? 0;
  if (packetIndex === state.total - 1) packetIndex = 0;
  await page(packetIndex);
  const startingFrame = frame(packetAt(packetIndex));
  let low = 0,
    high = events.total;
  while (low < high) {
    const middle = Math.floor((low + high) / 2);
    await events.load(middle);
    if (frame(events.get(middle)) < startingFrame) low = middle + 1;
    else high = middle;
  }
  let previousTime = time(packetAt(packetIndex)),
    deadline = performance.now();
  for (
    let eventIndex = low;
    eventIndex < events.total && state.playing && state.playEpoch === epoch;
    eventIndex++
  ) {
    await events.load(eventIndex);
    const event = events.get(eventIndex);
    if (!event) break;
    // Scan only cheap indexed summaries. Non-MMS packets are never dissected during replay.
    while (packetIndex < state.total) {
      await page(packetIndex);
      if (frame(packetAt(packetIndex)) >= frame(event)) break;
      packetIndex++;
    }
    if (packetIndex >= state.total) break;
    if (frame(packetAt(packetIndex)) !== frame(event)) continue;
    deadline +=
      (Math.max(0, time(event) - previousTime) * 1000) /
      Number($("#speed").value);
    while (
      state.playing &&
      state.playEpoch === epoch &&
      performance.now() < deadline
    )
      await new Promise((resolve) =>
        setTimeout(resolve, Math.min(50, deadline - performance.now())),
      );
    if (!state.playing || state.playEpoch !== epoch) return;
    await select(packetIndex, { scroll: true });
    if (!state.playing || state.playEpoch !== epoch) return;
    showValues({ values: event.values }, event);
    $("#values-mode").textContent = "已观察数据事件";
    if (state.mappingEnabled)
      await api.command("replay-frame", { frame: frame(event) });
    previousTime = time(event);
    packetIndex++;
  }
  if (state.playEpoch === epoch) {
    pause();
    status("已观察数据事件回放完成");
  }
}
async function filter() {
  state.filter = $("#filter").value.trim();
  try {
    await refresh();
    $("#filter").classList.remove("invalid");
  } catch (error) {
    $("#filter").classList.add("invalid");
    throw error;
  }
}
async function capabilities() {
  const result = await api.command("capabilities");
  state.capabilities = result;
  $("#capabilities").textContent = [result.version, result.tshark_version]
    .filter(Boolean)
    .join(" · ");
  const unavailable =
    result.available === false ||
    result.analysis === false ||
    result.dll_available === false ||
    result.tshark_available === false;
  $("#availability").classList.toggle("error", unavailable);
  $("#availability").textContent = unavailable
    ? result.message ||
      result.reason ||
      "分析组件不可用；请安装可选 DLL 与 Wireshark/TShark。主应用可继续使用。"
    : result.message || "离线分析已就绪。实时捕获需要 Npcap 与可用网络接口。";
  $("#capabilities").textContent =
    `分析 ${result.analysis === false ? "不可用" : "可用"} · 捕获 ${result.capture === false ? "不可用" : "可用"}${result.tshark ? " · TShark" : ""}`;
  status("能力检测完成");
}
async function captureOptions() {
  const result = await api.command("interfaces");
  const interfaces = Array.isArray(result) ? result : result.interfaces || [];
  const previous = $("#interfaces").value;
  $("#interfaces").innerHTML =
    '<option value="">请选择实际通信网卡…</option>' +
    interfaces
      .map(
        (item) =>
          `<option value="${escape(item.id ?? item.name ?? item.index)}">${escape(item.description || item.display_name || item.name || item.id)}</option>`,
      )
      .join("");
  if (
    interfaces.some(
      (item) => String(item.id ?? item.name ?? item.index) === previous,
    )
  )
    $("#interfaces").value = previous;
  if (!interfaces.length)
    throw new Error("未发现捕获接口；请检查 Npcap / dumpcap 安装与接口权限。");
  $("#capture-dialog").showModal();
}
async function startCapture() {
  if (state.capturing) return;
  if (!$("#capture-output").value || !$("#interfaces").value) {
    await captureOptions();
    return;
  }
  const result = await api.command("capture-start", {
    mms_ports: $("#mms-ports")
      .value.split(",")
      .filter((item) => item.trim())
      .map(Number),
    interface: $("#interfaces").value,
    filter: $("#capture-filter").value.trim(),
    output: $("#capture-output").value,
  });
  state.capturing = true;
  $("#start").disabled = true;
  $("#stop").disabled = false;
  $("#mode").textContent =
    "● 正在捕获 · " + $("#interfaces").selectedOptions[0].textContent;
  $("#capture-dialog").close();
  status(result.message || "正在捕获到 " + $("#capture-output").value);
}
async function stopCapture() {
  if (!state.capturing) return;
  aiPanel.invalidate();
  const result = await api.command("capture-stop");
  state.capturing = false;
  state.fileTotal = result.total;
  state.filter = "";
  $("#filter").value = "";
  mappingMode(false);
  $("#start").disabled = false;
  $("#stop").disabled = true;
  $("#mode").textContent = "离线分析 · 不发送网络数据";
  if (result.file || result.output)
    $("#file-label").textContent = result.file || result.output;
  await refresh();
}
async function sessions() {
  status("正在汇总 TCP / MMS 会话…");
  const result = await api.command("sessions");
  const items = Array.isArray(result) ? result : result.sessions || [];
  $("#sessions-content").innerHTML = items.length
    ? "<table><thead><tr><th>TCP 会话</th><th>源</th><th>目标</th><th>报文数</th><th>业务</th></tr></thead><tbody>" +
      items
        .map(
          (s) =>
            `<tr><td><button data-stream="${escape(s.stream ?? s.id)}">${escape(s.stream ?? s.id)}</button></td><td>${escape(s.source || s.client)}</td><td>${escape(s.destination || s.server)}</td><td>${escape(s.packets ?? s.count)}</td><td>${escape(s.services || s.info || "")}</td></tr>`,
        )
        .join("") +
      "</tbody></table>"
    : '<p class="empty">没有可解析的 TCP 会话。</p>';
  $("#sessions-dialog").showModal();
}
async function locate(frameNumber) {
  pause();
  $("#filter").value = `frame.number == ${Number(frameNumber)}`;
  await filter();
}
function mappingList() {
  $("#mapping-list").innerHTML = state.mappings
    .map(
      (mapping, index) =>
        `<div class="mapping-row"><span>${escape(mapping.source)} → ${escape(mapping.target)}</span><button type="button" data-remove-mapping="${index}">删除</button></div>`,
    )
    .join("");
}
async function mapping() {
  pause();
  const catalog = await api.command("replay-catalog");
  if (catalog.unavailable) throw new Error("主应用未提供数据回放接口");
  if (!catalog.running) throw new Error("请先启动主应用 MMS 服务");
  if (catalog.simulation)
    throw new Error("请先停止波形仿真，以避免同时修改服务值");
  $("#mapping-target").innerHTML = catalog.points
    .map(
      (point) =>
        `<option value="${escape(point.reference)}">${escape(point.reference)} = ${escape(point.value)}</option>`,
    )
    .join("");
  $("#decoded-references").innerHTML = [...state.references]
    .map((reference) => `<option value="${escape(reference)}"></option>`)
    .join("");
  mappingList();
  $("#mapping-dialog").showModal();
}
async function action(input) {
  const name = typeof input === "string" ? input : input.name;
  if (name === "ai-answer-locate") return locate(input.frame);
  if (name === "ai-answer-filter") {
    $("#filter").value = input.filter;
    return action("filter");
  }
  if (input?.frame && state.selected?.frame !== input.frame) {
    const row = document.querySelector(`[data-frame="${Number(input.frame)}"]`);
    if (row) await select(Number(row.dataset.index));
  }
  if (name.startsWith("ai-")) return aiPanel.action(name);
  switch (name) {
    case "open": {
      aiPanel.invalidate();
      pause();
      status(
        "正在打开抓包文件并建立索引… 大文件可能需要数十秒，可使用“取消分析”。",
      );
      const ports = $("#mms-ports")
        .value.split(",")
        .filter((item) => item.trim())
        .map(Number);
      if (
        !ports.length ||
        ports.some(
          (port) => !Number.isInteger(port) || port < 1 || port > 65535,
        )
      )
        throw new Error("MMS 端口必须为 1 至 65535 的整数，以逗号分隔");
      const result = await api.command("open-dialog", { mms_ports: ports });
      if (result) {
        state.fileTotal = result.total;
        mappingMode(false);
        state.references.clear();
        state.mappings = [];
        $("#mode").textContent = "报文离线分析 · AI 发送需单独授权";
        $("#file-label").textContent = result.file;
        state.filter = "";
        $("#filter").value = "";
        await refresh();
      } else status("已取消打开文件");
      break;
    }
    case "filter":
      await filter();
      break;
    case "retry":
      await capabilities();
      break;
    case "cancel":
      pause();
      await api.command("cancel");
      status("已请求取消分析");
      break;
    case "capture-options":
      await captureOptions();
      break;
    case "capture-start":
      await startCapture();
      break;
    case "capture-stop":
      await stopCapture();
      break;
    case "output": {
      const result = await api.command("choose-output");
      if (result) $("#capture-output").value = result;
      break;
    }
    case "sessions":
      await sessions();
      break;
    case "help":
      await api.command("help");
      break;
    case "play":
      await play();
      break;
    case "rewind":
      pause();
      await select(0, { scroll: true });
      break;
    case "next":
      pause();
      await select(
        Math.min(state.total - 1, (state.selected?.index ?? -1) + 1),
        { scroll: true },
      );
      if (state.mappingEnabled && state.selected)
        await api.command("replay-frame", { frame: state.selected.frame });
      break;
    case "snapshot":
      pause();
      await snapshot();
      break;
    case "mapping":
      await mapping();
      break;
    case "mapping-disable":
      pause();
      await api.command("replay-disable");
      mappingMode(false);
      $("#mode").textContent = "离线分析 · 不发送网络数据";
      status("已停用主应用数据值回放");
      break;
    case "mapping-add": {
      const source = $("#mapping-source").value.trim(),
        target = $("#mapping-target").value;
      if (!source || !target) throw new Error("请选择完整的来源与目标");
      if (
        state.mappings.some(
          (item) => item.source === source || item.target === target,
        )
      )
        throw new Error("来源与目标均不允许重复映射");
      state.mappings.push({ source, target });
      mappingList();
      break;
    }
    case "locate":
      pause();
      if (state.selected) await select(state.selected.index, { scroll: true });
      break;
    case "detail":
      if (state.selected) await select(state.selected.index);
      break;
    case "expand":
    case "collapse":
      document
        .querySelectorAll("#protocol-tree details")
        .forEach((element) => (element.open = name === "expand"));
      break;
    case "goto":
      $("#goto-dialog").showModal();
      $("#goto-frame").focus();
      break;
    case "copy-summary":
      if (state.selected)
        await api.command("copy", { text: summary(state.selected) });
      break;
    case "copy-hex":
      await api.command("copy", { text: $("#hex").textContent });
      break;
    case "filter-session": {
      const stream = state.detail?.stream ?? state.selected?.stream;
      if (stream === undefined || stream === null)
        throw new Error("此报文没有 TCP 会话编号");
      $("#filter").value = `tcp.stream eq ${Number(stream)}`;
      await filter();
      break;
    }
    case "correlation": {
      const correlation = state.detail?.correlation;
      const linked =
        correlation?.request_frame === state.selected?.frame
          ? correlation?.response_frame
          : correlation?.request_frame;
      if (!linked) throw new Error("当前报文没有已匹配的请求 / 响应");
      await locate(linked);
      break;
    }
    default:
      if (name.startsWith("filter-")) {
        $("#filter").value =
          {
            all: "",
            mms: "mms",
            goose: "goose",
            sv: "sv",
            expert: "_ws.expert",
          }[name.slice(7)] ?? "";
        await filter();
      }
  }
}
document.addEventListener("click", (event) => {
  const button = event.target.closest("[data-action]");
  if (button) {
    run(() => action(button.dataset.action));
    return;
  }
  const close = event.target.closest("[data-close]");
  if (close) {
    $("#" + close.dataset.close).close();
    return;
  }
  const remove = event.target.closest("[data-remove-mapping]");
  if (remove) {
    state.mappings.splice(Number(remove.dataset.removeMapping), 1);
    mappingList();
    return;
  }
  const stream = event.target.closest("[data-stream]");
  if (stream) {
    $("#sessions-dialog").close();
    $("#filter").value = `tcp.stream eq ${Number(stream.dataset.stream)}`;
    run(filter);
    return;
  }
  const row = event.target.closest("[data-frame]");
  if (row) {
    pause();
    run(() => select(Number(row.dataset.index)));
  }
});
$("#packet-scroll").addEventListener("contextmenu", (event) => {
  const row = event.target.closest("[data-frame]");
  if (row) {
    event.preventDefault();
    run(() => api.context(Number(row.dataset.frame)));
  }
});
let scrollFrame = 0;
$("#packet-scroll").addEventListener("scroll", () => {
  if (scrollFrame) return;
  scrollFrame = requestAnimationFrame(() => {
    scrollFrame = 0;
    run(renderPackets);
  });
});
new ResizeObserver(() => {
  if (state.total) run(renderPackets);
}).observe($("#packet-scroll"));
$("#filter").addEventListener("keydown", (event) => {
  if (event.key === "Enter") run(filter);
});
$("#preset").addEventListener("change", () => {
  $("#filter").value = $("#preset").value;
  run(filter);
});
$("#timeline").addEventListener("input", () => {
  pause();
  run(async () => {
    await select(Number($("#timeline").value), { scroll: true });
    await snapshot();
  });
});
$("#speed").addEventListener("change", () => {
  if (state.playing) {
    pause();
    run(play);
  }
});
$("#capture-form").addEventListener("submit", (event) => {
  event.preventDefault();
  run(startCapture);
});
$("#goto-form").addEventListener("submit", (event) => {
  event.preventDefault();
  $("#goto-dialog").close();
  run(() => locate($("#goto-frame").value));
});
$("#mapping-form").addEventListener("submit", (event) => {
  event.preventDefault();
  run(async () => {
    const result = await api.command("replay-enable", {
      mappings: state.mappings,
    });
    mappingMode(result.enabled);
    if (result.enabled) {
      $("#mapping-dialog").close();
      $("#mode").textContent =
        "数据值回放已启用 · " + state.mappings.length + " 项映射";
      status("播放 / 步进将更新主应用测量值");
    }
  });
});
$("#packet-scroll").addEventListener("keydown", (event) => {
  if (["ArrowDown", "ArrowUp"].includes(event.key)) {
    event.preventDefault();
    pause();
    run(() =>
      select(
        Math.max(
          0,
          Math.min(
            state.total - 1,
            (state.selected?.index ?? 0) + (event.key === "ArrowDown" ? 1 : -1),
          ),
        ),
        { scroll: true },
      ),
    );
  }
});
const aiPanel = installAiPanel({
  api,
  context: () => ({
    frame: state.selected?.frame,
    values: state.detail?.values,
  }),
  locate,
  applyFilter: async (filter) => {
    $("#filter").value = filter;
    await action("filter");
  },
});
api.onAction((value) => run(() => action(value)));
api.onFailure((message) => {
  aiPanel.invalidate();
  pause();
  mappingMode(false);
  state.capturing = false;
  $("#stop").disabled = true;
  $("#start").disabled = false;
  $("#availability").textContent =
    message + "。主应用不受影响，可通过帮助菜单重新检测。";
  $("#availability").classList.add("error");
  status(message, true);
});
let capturePolling = false;
setInterval(() => {
  if (!state.capturing || capturePolling) return;
  capturePolling = true;
  run(async () => {
    const current = await api.command("capture-status");
    if (!current.capturing) {
      await stopCapture();
      status("捕获已结束并载入分析（文件大小/时长上限或捕获进程停止）");
    }
  }).finally(() => (capturePolling = false));
}, 2000);
run(capabilities).then((result) => {
  if (result === null && !state.capabilities) {
    $("#availability").textContent = $("#status").textContent;
    $("#availability").classList.add("error");
  }
});
