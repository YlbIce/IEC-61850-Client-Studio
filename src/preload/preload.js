const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('iec61850', {
  call: (method, payload) => ipcRenderer.invoke('iec:call', method, payload),
  getWorkspace: () => ipcRenderer.invoke('iec:getWorkspace'),
  detachModule: (moduleId, title) => ipcRenderer.invoke('window:detachModule', moduleId, title),
  onWorkspace: (callback) => {
    const listener = (_, payload) => callback(payload);
    ipcRenderer.on('workspace:update', listener);
    return () => ipcRenderer.removeListener('workspace:update', listener);
  },
  onMenuAction: (callback) => {
    const listener = (_, payload) => callback(payload);
    ipcRenderer.on('menu:action', listener);
    return () => ipcRenderer.removeListener('menu:action', listener);
  },
  onBackendLog: (callback) => {
    const listener = (_, payload) => callback(payload);
    ipcRenderer.on('backend:log', listener);
    return () => ipcRenderer.removeListener('backend:log', listener);
  }
});
