const theme = require("../main/theme");
("use strict");
const { BrowserWindow, Menu, ipcMain, dialog, clipboard } = require("electron");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { CaptureClient } = require("./helper-client");
const ai = require("./ai/controller");
const windows = new Map();
let installed = false;

function installIpc() {
  if (installed) return;
  installed = true;
  const owner = (event) => {
    const entry = windows.get(event.sender.id);
    if (
      !entry ||
      event.senderFrame !== event.sender.mainFrame ||
      event.senderFrame.url !== entry.url
    )
      throw new Error("拒绝未授权的报文分析 IPC");
    return entry;
  };
  ipcMain.handle("capture:command", async (event, command, args) => {
    const entry = owner(event),
      { window: win, client } = entry;
    if (typeof command === "string" && command.startsWith("ai-"))
      return ai.command(entry, command, args);
    if (command === "open-dialog") {
      if (entry.capturing) throw new Error("请先停止当前捕获，再打开其他文件");
      const ports = args?.mms_ports || [102];
      if (
        !Array.isArray(ports) ||
        ports.length > 32 ||
        ports.some(
          (port) => !Number.isInteger(port) || port < 1 || port > 65535,
        )
      )
        throw new Error("MMS 解码端口无效");
      const selected = await dialog.showOpenDialog(win, {
        title: "打开抓包文件",
        filters: [
          { name: "Wireshark 抓包文件", extensions: ["pcap", "pcapng", "cap"] },
          { name: "所有文件", extensions: ["*"] },
        ],
        properties: ["openFile"],
      });
      if (selected.canceled) return null;
      ai.invalidate(entry);
      entry.captureFile = null;
      entry.mapping = null;
      entry.ports = ports;
      const opened = await client.request("open", {
        file: selected.filePaths[0],
        mms_ports: ports,
      });
      entry.captureFile = selected.filePaths[0];
      return {
        ...opened,
        file: selected.filePaths[0],
      };
    }
    if (command === "choose-output") {
      const selected = await dialog.showSaveDialog(win, {
        title: "保存实时捕获",
        defaultPath: "capture.pcapng",
        filters: [{ name: "PCAPNG", extensions: ["pcapng"] }],
      });
      return selected.canceled ? null : selected.filePath;
    }
    if (command === "replay-catalog")
      return entry.replay
        ? entry.replay.catalog()
        : { running: false, points: [], unavailable: true };
    if (command === "replay-disable") {
      entry.mapping = null;
      return { enabled: false };
    }
    if (command === "replay-enable") {
      if (!entry.replay) throw new Error("主应用未提供数据回放接口");
      const catalog = await entry.replay.catalog();
      if (!catalog.running) throw new Error("请先启动主应用 MMS 服务");
      const mappings = Array.isArray(args.mappings) ? args.mappings : [];
      if (!mappings.length || mappings.length > 1000)
        throw new Error("请配置 1 至 1000 项显式映射");
      const checked = new Map();
      const targets = new Set();
      for (const mapping of mappings) {
        if (
          typeof mapping.source !== "string" ||
          !mapping.source ||
          !catalog.points.some(
            (point) =>
              point.reference === mapping.target &&
              !["SPC", "DPC"].includes(point.cdc),
          )
        )
          throw new Error("映射目标无效或属于遥控输出");
        if (checked.has(mapping.source) || targets.has(mapping.target))
          throw new Error("来源与目标均不允许重复映射");
        checked.set(mapping.source, mapping.target);
        targets.add(mapping.target);
      }
      const confirmation = await dialog.showMessageBox(win, {
        type: "warning",
        title: "确认数据值回放",
        message: `将捕获数据回放到正在运行的 MMS 服务（${checked.size} 项映射）`,
        detail:
          [...checked]
            .map(([source, target]) => `${source} → ${target}`)
            .join("\n") +
          "\n\n仅写入已解码的数值/布尔值。不会重发原始报文、遥控或定值组操作。关闭窗口或重新导入时自动停用。",
        buttons: ["取消", "启用数据值回放"],
        defaultId: 0,
        cancelId: 0,
        noLink: true,
      });
      entry.mapping = confirmation.response === 1 ? checked : null;
      return { enabled: !!entry.mapping };
    }
    if (command === "replay-frame") {
      if (!entry.mapping || !entry.replay) throw new Error("未启用数据值回放");
      const mapping = entry.mapping;
      if (!Number.isSafeInteger(args.frame) || args.frame < 1)
        throw new Error("无效报文编号");
      // Re-read decoded data in the main process; never trust renderer-supplied values.
      const detail = await client.request("detail", { frame: args.frame });
      if (entry.mapping !== mapping) throw new Error("数据值回放已停用");
      let applied = 0;
      for (const value of detail.values || []) {
        if (
          value.replayable !== true ||
          value.mapping !== "resolved" ||
          value.complete !== true ||
          !["ST", "MX"].includes(value.fc)
        )
          continue;
        if (entry.mapping !== mapping) break;
        const reference = mapping.get(value.reference);
        if (!reference) continue;
        if (value.type === "write-request" || value.source === "write-request")
          continue;
        if (typeof value.value === "string") {
          if (
            !["integer", "unsigned"].includes(value.type) ||
            !/^[-+]?\d+$/.test(value.value) ||
            !Number.isSafeInteger(Number(value.value))
          )
            continue;
        } else if (
          typeof value.value !== "number" &&
          typeof value.value !== "boolean"
        )
          continue;
        const number = Number(value.value);
        if (!Number.isFinite(number)) continue;
        await entry.replay.apply({
          reference,
          value: number,
          quality: Number.isInteger(value.quality) ? value.quality : undefined,
        });
        applied++;
      }
      return { applied };
    }
    if (command === "copy") {
      clipboard.writeText(String(args.text || "").slice(0, 4 * 1024 * 1024));
      return true;
    }
    if (command === "help")
      return dialog.showMessageBox(win, {
        type: "info",
        title: "IEC61850 报文分析",
        message: "独立报文分析与离线回放",
        detail:
          "打开 PCAP / PCAPNG 文件，使用 Wireshark 显示过滤器选择 MMS / GOOSE / SV 报文。选择报文查看协议树、原始字节及业务数据。\n\n捕获过滤器使用 BPF 语法，显示过滤器使用 Wireshark 语法。两者不同。\n\n时间轴回放仅重现捕获文件中的数据和时序，不重发 TCP、遥控或定值写入。TLS 加密流量没有解密密钥时无法还原 MMS。抓包文件可能包含敏感工程数据，请妥善保管。",
      });
    if (
      ![
        "capabilities",
        "interfaces",
        "packets",
        "detail",
        "replay",
        "cancel",
        "capture-start",
        "capture-stop",
        "capture-status",
        "sessions",
      ].includes(command)
    )
      throw new Error("不支持的分析命令");
    if (!args || typeof args !== "object" || Array.isArray(args))
      throw new Error("无效命令参数");
    if (command === "capture-stop") {
      ai.invalidate(entry);
      entry.captureFile = null;
      const result = await client.request(command, args);
      entry.capturing = false;
      entry.mapping = null;
      if (result.file) {
        const opened = await client.request("open", {
          file: result.file,
          mms_ports: entry.ports || [102],
        });
        entry.captureFile = result.file;
        return { ...result, ...opened };
      }
      return result;
    }
    if (command === "capture-start") {
      ai.invalidate(entry);
      entry.captureFile = null;
      const ports = args.mms_ports || [102];
      if (
        !Array.isArray(ports) ||
        ports.length > 32 ||
        ports.some(
          (port) => !Number.isInteger(port) || port < 1 || port > 65535,
        )
      )
        throw new Error("MMS 解码端口无效");
      if (entry.mapping)
        throw new Error("请先停用主应用数据值回放，再开始捕获");
      const result = await client.request(command, args);
      entry.ports = ports;
      entry.capturing = !!result.capturing;
      return result;
    }
    return client.request(command, args);
  });
  ipcMain.handle("capture:context", (event, frame) => {
    const entry = owner(event),
      valid = Number.isSafeInteger(frame) && frame > 0;
    const action = (name) =>
      entry.window.webContents.send("capture:action", { name, frame });
    Menu.buildFromTemplate([
      {
        label: "AI 解释此报文",
        enabled: valid,
        click: () => action("ai-explain"),
      },
      {
        label: "AI 跟踪这次交互",
        enabled: valid,
        click: () => action("ai-walkthrough"),
      },
      { type: "separator" },
      { label: "查看协议树", enabled: valid, click: () => action("detail") },
      {
        label: "定位到回放时间轴",
        enabled: valid,
        click: () => action("locate"),
      },
      { type: "separator" },
      {
        label: "仅查看此 TCP 会话",
        enabled: valid,
        click: () => action("filter-session"),
      },
      {
        label: "定位关联请求 / 响应",
        enabled: valid,
        click: () => action("correlation"),
      },
      { type: "separator" },
      {
        label: "复制报文摘要",
        enabled: valid,
        click: () => action("copy-summary"),
      },
      {
        label: "复制原始字节",
        enabled: valid,
        click: () => action("copy-hex"),
      },
    ]).popup({ window: entry.window });
  });
}

/** Optional standalone window. Parent never acquires the capture DLL or driver. */
async function createCaptureWindow({
  parent,
  root,
  resourcesPath = process.resourcesPath,
  isPackaged = false,
  replay,
} = {}) {
  installIpc();
  for (const entry of windows.values()) {
    if (entry.parent === parent && !entry.window.isDestroyed()) {
      await entry.ready;
      if (entry.window.isDestroyed()) return null;
      entry.window.show();
      entry.window.focus();
      return entry.window;
    }
  }
  const win = new BrowserWindow({
    title: "IEC61850 报文抓取与分析",
    width: 1440,
    height: 920,
    minWidth: 1050,
    minHeight: 650,
    parent,
    show: false,
    backgroundColor: "#edf0f4",
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  const client = new CaptureClient({
    root: root || path.resolve(__dirname, "../.."),
    resourcesPath,
    isPackaged,
  });
  const file = path.join(__dirname, "index.html");
  theme.attach(win, file);
  windows.set(win.webContents.id, {
    window: win,
    parent,
    client,
    replay,
    mapping: null,
    url: pathToFileURL(file).href,
  });
  client.on("failure", (message) => {
    if (!win.isDestroyed()) {
      const entry = windows.get(win.webContents.id);
      if (entry) {
        ai.invalidate(entry);
        entry.captureFile = null;
        entry.mapping = null;
        entry.capturing = false;
      }
      win.webContents.send("capture:failure", message);
    }
  });
  const send = (name) => () => win.webContents.send("capture:action", { name });
  theme.setWindowMenu(
    win,
    Menu.buildFromTemplate([
      {
        label: "文件(&F)",
        submenu: [
          {
            label: "打开抓包文件…",
            accelerator: "CmdOrCtrl+O",
            click: send("open"),
          },
          { type: "separator" },
          {
            label: "关闭分析窗口",
            accelerator: "Alt+F4",
            click: () => win.close(),
          },
        ],
      },
      {
        label: "捕获(&C)",
        submenu: [
          { label: "配置捕获接口…", click: send("capture-options") },
          {
            label: "开始捕获",
            accelerator: "F5",
            click: send("capture-start"),
          },
          {
            label: "停止捕获并分析",
            accelerator: "Shift+F5",
            click: send("capture-stop"),
          },
        ],
      },
      {
        label: "分析(&A)",
        submenu: [
          { label: "全部报文", click: send("filter-all") },
          { label: "MMS", click: send("filter-mms") },
          { label: "GOOSE", click: send("filter-goose") },
          { label: "采样值 SV", click: send("filter-sv") },
          { label: "专家信息 / 异常", click: send("filter-expert") },
          { type: "separator" },
          { label: "刷新会话统计", click: send("sessions") },
          {
            label: "定位报文…",
            accelerator: "CmdOrCtrl+G",
            click: send("goto"),
          },
        ],
      },
      {
        label: "回放(&R)",
        submenu: [
          { label: "播放 / 暂停", accelerator: "F6", click: send("play") },
          { label: "停止并回到起点", click: send("rewind") },
          { label: "下一条报文", accelerator: "F7", click: send("next") },
          { type: "separator" },
          {
            label: "配置主应用值映射…",
            enabled: !!replay,
            click: send("mapping"),
          },
          { label: "停用主应用值回放", click: send("mapping-disable") },
        ],
      },
      {
        label: "视图(&V)",
        submenu: [
          theme.menu(),
          { label: "展开协议树", click: send("expand") },
          { label: "折叠协议树", click: send("collapse") },
          { role: "togglefullscreen", label: "全屏" },
          { role: "resetZoom", label: "实际大小" },
          { role: "zoomIn", label: "放大" },
          { role: "zoomOut", label: "缩小" },
        ],
      },
      {
        label: "AI 助手",
        submenu: [
          {
            label: "报文学习与诊断",
            accelerator: "CmdOrCtrl+J",
            click: send("ai-toggle"),
          },
          { label: "对话历史…", click: send("ai-history") },
          { label: "解释所选报文", click: send("ai-explain") },
          { label: "跟踪一次交互", click: send("ai-walkthrough") },
          { label: "DeepSeek API 设置…", click: send("ai-settings") },
        ],
      },
      {
        label: "帮助(&H)",
        submenu: [
          { label: "使用说明与边界", accelerator: "F1", click: send("help") },
          { label: "重新检测可选模块", click: send("retry") },
        ],
      },
    ]),
  );
  win.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  win.webContents.on("will-navigate", (event) => event.preventDefault());
  // Cache identity before Electron destroys webContents.
  const id = win.webContents.id;
  win.on("closed", () => {
    const entry = windows.get(id);
    if (entry) {
      ai.invalidate(entry);
      ai.close(entry);
    }
    windows.delete(id);
    client.close();
  });
  win.once("ready-to-show", () => win.show());
  const entry = windows.get(id);
  entry.ready = win.loadFile(file);
  try {
    await entry.ready;
    return win.isDestroyed() ? null : win;
  } catch (error) {
    if (win.isDestroyed()) return null; // User closed the window during loading.
    win.destroy(); // Also closes the helper and removes the window registry entry.
    throw new Error(
      `报文分析页面加载失败，请完整解压便携版后重试。${error.message}`,
    );
  }
}
module.exports = { createCaptureWindow };
