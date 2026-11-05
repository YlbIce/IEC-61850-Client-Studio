const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("historyApi", {
  command: (action, args) =>
    ipcRenderer.invoke("capture:history", action, args),
  onUpdate: (callback) =>
    ipcRenderer.on("capture:history-update", () => callback()),
});

contextBridge.exposeInMainWorld("appearance", {
  initial: ipcRenderer.sendSync("appearance:read"),
  set: (value) => ipcRenderer.invoke("appearance:set", value),
  onChange: (callback) =>
    ipcRenderer.on("appearance:changed", (_event, value) => callback(value)),
});
