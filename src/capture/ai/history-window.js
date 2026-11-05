const theme = require("../../main/theme");
("use strict");
const { app, BrowserWindow, Menu, ipcMain, dialog } = require("electron");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { HistoryStore } = require("./history-store");
const answers = require("./answer-window");
let store,
  installed = false;
const owners = new Map();
const active = new Set();
function history() {
  return (store ||= new HistoryStore(
    path.join(app.getPath("userData"), "ai-history"),
  ));
}
function install() {
  if (installed) return;
  installed = true;
  ipcMain.handle("capture:history", async (event, action, args = {}) => {
    const s = owners.get(event.sender.id);
    if (
      !s ||
      event.senderFrame !== event.sender.mainFrame ||
      event.senderFrame.url !== s.url
    )
      throw Error("拒绝未授权的历史窗口操作");
    if (action === "list") return history().list(args);
    if (action === "open") {
      const record = await history().read(args.id);
      await answers.show(
        s.entry,
        record,
        record.id === s.entry.aiResult?.id ? s.entry.aiGeneration || 0 : -1,
      );
      return;
    }
    if (action === "delete") {
      if (active.has(args.id))
        throw Error("本次问答正在运行，请完成或取消后再删除");
      const record = await history().read(args.id);
      const choice = await dialog.showMessageBox(s.win, {
        type: "question",
        title: "删除对话记录",
        message: "删除这次问答及保存的报文上下文？",
        detail: record.question,
        buttons: ["取消", "删除"],
        defaultId: 0,
        cancelId: 0,
      });
      if (choice.response === 1) {
        await history().remove(record.id);
        if (s.entry.aiAnswer?.result.id === record.id) answers.close(s.entry);
        if (s.entry.aiResult?.id === record.id) s.entry.aiResult = null;
        return { deleted: true };
      }
      return { deleted: false };
    }
    throw Error("不支持的历史操作");
  });
}
async function show(entry) {
  install();
  if (entry.aiHistory && !entry.aiHistory.isDestroyed()) {
    entry.aiHistory.webContents.send("capture:history-update");
    entry.aiHistory.show();
    entry.aiHistory.focus();
    return;
  }
  const file = path.join(__dirname, "history.html");
  const win = new BrowserWindow({
    title: "AI 对话历史",
    width: 1200,
    height: 820,
    minWidth: 850,
    minHeight: 600,
    show: false,
    webPreferences: {
      preload: path.join(__dirname, "history-preload.js"),
      sandbox: true,
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  theme.attach(win, file);
  entry.aiHistory = win;
  const id = win.webContents.id;
  owners.set(id, { win, entry, url: pathToFileURL(file).href });
  theme.setWindowMenu(
    win,
    Menu.buildFromTemplate([
      { label: "窗口", submenu: [{ role: "close" }] },
      {
        label: "视图",
        submenu: [
          theme.menu(),
          { role: "zoomIn" },
          { role: "zoomOut" },
          { role: "resetZoom" },
        ],
      },
    ]),
  );
  win.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  win.webContents.on("will-navigate", (e) => e.preventDefault());
  win.on("closed", () => {
    owners.delete(id);
    if (entry.aiHistory === win) entry.aiHistory = null;
  });
  try {
    await win.loadFile(file);
    if (!win.isDestroyed()) win.show();
  } catch (e) {
    if (!win.isDestroyed()) {
      win.destroy();
      throw e;
    }
  }
}
function changed() {
  for (const s of owners.values())
    if (!s.win.isDestroyed()) s.win.webContents.send("capture:history-update");
}
function close(entry) {
  if (entry.aiHistory && !entry.aiHistory.isDestroyed())
    entry.aiHistory.destroy();
}
module.exports = { history, show, changed, close, active };
