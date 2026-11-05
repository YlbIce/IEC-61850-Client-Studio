"use strict";
const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("capture", {
  command: (command, args = {}) =>
    ipcRenderer.invoke("capture:command", command, args),
  context: (frame) => ipcRenderer.invoke("capture:context", frame),
  onAiProgress: (callback) => {
    const listener = (_event, value) => callback(value);
    ipcRenderer.on("capture:ai-progress", listener);
    return () => ipcRenderer.removeListener("capture:ai-progress", listener);
  },
  onAction: (callback) => {
    const listener = (_event, value) => callback(value);
    ipcRenderer.on("capture:action", listener);
    return () => ipcRenderer.removeListener("capture:action", listener);
  },
  onFailure: (callback) => {
    const listener = (_event, value) => callback(value);
    ipcRenderer.on("capture:failure", listener);
    return () => ipcRenderer.removeListener("capture:failure", listener);
  },
});

contextBridge.exposeInMainWorld("appearance", {
  initial: ipcRenderer.sendSync("appearance:read"),
  set: (value) => ipcRenderer.invoke("appearance:set", value),
  onChange: (callback) =>
    ipcRenderer.on("appearance:changed", (_event, value) => callback(value)),
});
