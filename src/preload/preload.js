const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('iec61850', {
  capture: () => ipcRenderer.invoke('window:capture'),
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
  onBackendStatus: (callback) => {
    const listener=(_,status)=>callback(status);ipcRenderer.on('backend:status',listener);
    return ()=>ipcRenderer.removeListener('backend:status',listener);
  },
  onBackendLog: (callback) => {
    const listener = (_, payload) => callback(payload);
    ipcRenderer.on('backend:log', listener);
    return () => ipcRenderer.removeListener('backend:log', listener);
  }
});

contextBridge.exposeInMainWorld('appearance',{
 initial:ipcRenderer.sendSync('appearance:read'),set:value=>ipcRenderer.invoke('appearance:set',value),onChange:callback=>ipcRenderer.on('appearance:changed',(_event,value)=>callback(value))
});
