"use strict";
const { app, ipcMain, nativeTheme, Menu, dialog } = require("electron");
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const windows = new Map();
const modes = ["light", "dark", "system"];
let initialized = false,
  applicationMenu,
  queue = Promise.resolve();
function state() {
  initialize();
  return {
    source: nativeTheme.themeSource,
    resolved: nativeTheme.shouldUseDarkColors ? "dark" : "light",
  };
}
function trusted(event) {
  const entry = windows.get(event.sender.id);
  return (
    entry &&
    event.senderFrame === event.sender.mainFrame &&
    event.senderFrame.url === entry.url
  );
}
function initialize() {
  if (initialized) return;
  initialized = true;
  let source = "system";
  try {
    const data = JSON.parse(
      fs.readFileSync(
        path.join(app.getPath("userData"), "appearance.json"),
        "utf8",
      ),
    );
    if (modes.includes(data.theme)) source = data.theme;
  } catch {}
  nativeTheme.themeSource = source;
  ipcMain.on("appearance:read", (event) => {
    event.returnValue = trusted(event) ? state() : null;
  });
  ipcMain.handle("appearance:set", (event, value) => {
    if (!trusted(event)) throw Error("拒绝未授权的主题操作");
    return set(value);
  });
  nativeTheme.on("updated", broadcast);
}
function broadcast() {
  const value = state();
  for (const entry of windows.values())
    if (!entry.win.isDestroyed()) {
      entry.win.setBackgroundColor(
        value.resolved === "dark" ? "#151c25" : "#eef1f5",
      );
      entry.win.webContents.send("appearance:changed", value);
    }
  for (const menu of [
    applicationMenu,
    ...[...windows.values()].map((e) => e.menu),
  ])
    if (menu) {
      const item = menu.getMenuItemById("theme-" + value.source);
      if (item) item.checked = true;
    }
}
function set(source) {
  const operation = queue.then(async () => {
    if (!modes.includes(source)) throw Error("主题模式无效");
    const directory = app.getPath("userData"),
      file = path.join(directory, "appearance.json");
    await fs.promises.mkdir(directory, { recursive: true });
    await fs.promises.writeFile(
      file + ".tmp",
      JSON.stringify({ theme: source }),
      "utf8",
    );
    await fs.promises.rename(file + ".tmp", file);
    nativeTheme.themeSource = source;
    broadcast();
    return state();
  });
  queue = operation.catch(() => {});
  return operation;
}
function attach(win, file, search = "") {
  initialize();
  windows.set(win.webContents.id, { win, url: pathToFileURL(file).href + search });
  const id = win.webContents.id;
  win.once("closed", () => windows.delete(id));
  win.setBackgroundColor(state().resolved === "dark" ? "#151c25" : "#eef1f5");
}
function menu() {
  return {
    label: "主题",
    submenu: modes.map((source, i) => ({
      id: "theme-" + source,
      label: ["亮色", "暗色", "跟随系统"][i],
      type: "radio",
      checked: state().source === source,
      click: () =>
        set(source).catch((e) =>
          dialog.showErrorBox("主题保存失败", e.message),
        ),
    })),
  };
}
function setWindowMenu(win, menu) {
  const entry = windows.get(win.webContents.id);
  if (entry) entry.menu = menu;
  win.setMenu(menu);
}
function setApplicationMenu(menu) {
  applicationMenu = menu;
  Menu.setApplicationMenu(menu);
  for (const entry of windows.values())
    if (entry.menu && !entry.win.isDestroyed()) entry.win.setMenu(entry.menu);
}
module.exports = {
  state,
  set,
  attach,
  menu,
  setWindowMenu,
  setApplicationMenu,
};
