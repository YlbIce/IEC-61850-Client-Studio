const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("answer", {
  command: (action, value) =>
    ipcRenderer.invoke("capture:answer", action, value),
  onUpdate: (callback) =>
    ipcRenderer.on("capture:answer-update", (_event, result) =>
      callback(result),
    ),
});

contextBridge.exposeInMainWorld("appearance", {
  initial: ipcRenderer.sendSync("appearance:read"),
  set: (value) => ipcRenderer.invoke("appearance:set", value),
  onChange: (callback) =>
    ipcRenderer.on("appearance:changed", (_event, value) => callback(value)),
});
