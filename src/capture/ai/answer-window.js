const theme = require("../../main/theme");
("use strict");
const { BrowserWindow, Menu, ipcMain, clipboard, dialog } = require("electron");
const path = require("node:path");
const fs = require("node:fs/promises");
const { pathToFileURL } = require("node:url");
const markdown = require("markdown-it")({
  html: false,
  linkify: false,
  breaks: true,
});
markdown.disable(["link", "image"]);
const owners = new Map();
let installed = false;
function report(r) {
  return `# AI 分析与学习记录\n\n问题：${r.question}\n\n${r.answer}\n\n## 证据事实\n\n${r.facts.map((f) => `- ${f.text}（帧 ${f.frames.join(", ")}）`).join("\n")}\n\n## 限制与覆盖范围\n\n${[...r.limitations, r.coverage].map((x) => "- " + x).join("\n")}\n\n## 下一步\n\n${r.next_checks.map((x) => "- " + x).join("\n")}\n\n捕获文件：${r.captureFile || "未选择"}\n\n模型：${r.model}\n\n生成时间：${r.at}\n\n## 保存的报文上下文\n\n~~~json\n${JSON.stringify(r.context || {}, null, 2)}\n~~~\n`;
}
function current(s) {
  return (
    !s.entry.window.isDestroyed() &&
    s.generation === (s.entry.aiGeneration || 0)
  );
}
function payload(s) {
  return {
    ...s.result,
    html: markdown.render(s.result.answer),
    current: current(s),
  };
}
function install() {
  if (installed) return;
  installed = true;
  ipcMain.handle("capture:answer", async (event, action, value) => {
    const s = owners.get(event.sender.id);
    if (
      !s ||
      event.senderFrame !== event.sender.mainFrame ||
      event.senderFrame.url !== s.url
    )
      throw Error("拒绝未授权的答案窗口操作");
    if (action === "read") return payload(s);
    if (action === "copy") {
      clipboard.writeText(report(s.result));
      return "已复制 Markdown";
    }
    if (action === "export") {
      const selected = await dialog.showSaveDialog(s.win, {
        defaultPath: "AI分析记录.md",
        filters: [{ name: "Markdown", extensions: ["md"] }],
      });
      if (!selected.canceled)
        await fs.writeFile(selected.filePath, report(s.result), "utf8");
      return selected.canceled ? "已取消导出" : "已导出";
    }
    if (action === "owner") {
      s.entry.window.show();
      s.entry.window.focus();
      return;
    }
    if (!current(s)) throw Error("捕获文件已切换，请在对应文件中重新分析");
    if (action === "locate") {
      if (
        !Number.isInteger(value) ||
        !s.result.facts.some((f) => f.frames.includes(value))
      )
        throw Error("帧号不在本答案的证据中");
      s.entry.window.webContents.send("capture:action", {
        name: "ai-answer-locate",
        frame: value,
      });
    } else if (action === "filter" && s.result.filter) {
      s.entry.window.webContents.send("capture:action", {
        name: "ai-answer-filter",
        filter: s.result.filter,
      });
    } else throw Error("不支持的答案操作");
    s.entry.window.show();
    s.entry.window.focus();
  });
}
async function show(
  entry,
  result = entry.aiResult,
  generation = entry.aiGeneration || 0,
) {
  if (!result) throw Error("没有可显示的 AI 结果");
  install();
  let s = entry.aiAnswer;
  if (s && !s.win.isDestroyed()) {
    s.result = result;
    s.generation = generation;
    await s.ready;
    if (!s.win.isDestroyed()) {
      s.win.webContents.send("capture:answer-update", payload(s));
      s.win.show();
      s.win.focus();
    }
    return;
  }
  const file = path.join(__dirname, "answer.html");
  const win = new BrowserWindow({
    title: "AI 分析与学习答案",
    width: 1280,
    height: 900,
    minWidth: 800,
    minHeight: 600,
    show: false,
    backgroundColor: "#f4f6fa",
    webPreferences: {
      preload: path.join(__dirname, "answer-preload.js"),
      sandbox: true,
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  s = { win, entry, result, generation, url: pathToFileURL(file).href };
  theme.attach(win, file);
  entry.aiAnswer = s;
  const id = win.webContents.id;
  owners.set(id, s);
  win.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  win.webContents.on("will-navigate", (event) => event.preventDefault());
  theme.setWindowMenu(
    win,
    Menu.buildFromTemplate([
      {
        label: "窗口",
        submenu: [
          {
            label: "返回报文分析",
            click: () => {
              entry.window.show();
              entry.window.focus();
            },
          },
          { role: "close" },
        ],
      },
      { label: "编辑", submenu: [{ role: "copy" }, { role: "selectAll" }] },
      {
        label: "视图",
        submenu: [
          theme.menu(),
          { role: "zoomIn" },
          { role: "zoomOut" },
          { role: "resetZoom" },
          { role: "togglefullscreen" },
        ],
      },
    ]),
  );
  win.on("closed", () => {
    owners.delete(id);
    if (entry.aiAnswer === s) entry.aiAnswer = null;
  });
  s.ready = win.loadFile(file);
  try {
    await s.ready;
    if (!win.isDestroyed()) win.show();
  } catch (e) {
    if (!win.isDestroyed()) {
      win.destroy();
      throw e;
    }
  }
}
function invalidate(entry) {
  const s = entry.aiAnswer;
  if (s && !s.win.isDestroyed())
    s.win.webContents.send("capture:answer-update", payload(s));
}
function close(entry) {
  if (entry.aiAnswer && !entry.aiAnswer.win.isDestroyed())
    entry.aiAnswer.win.destroy();
}
module.exports = { show, invalidate, close, report };
