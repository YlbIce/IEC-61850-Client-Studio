const { app, BrowserWindow, Menu, ipcMain, dialog } = require('electron');
const { spawn } = require('child_process');
const path = require('path');
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');

const isPackaged = app.isPackaged;
const appRoot = path.resolve(__dirname, '..', '..');
const protoPath = path.join(appRoot, 'proto', 'iec61850studio.proto');
const backendExe = isPackaged
  ? path.join(process.resourcesPath, 'backend', 'bin', 'iec61850-client-backend.exe')
  : path.join(appRoot, 'backend', 'bin', 'iec61850-client-backend.exe');
const grpcAddress = '127.0.0.1:48650';

let backendProcess = null;
let grpcClient = null;
let workspaceStream = null;
let quitting = false;
const windows = new Set();

function loadGrpcClient() {
  const packageDefinition = protoLoader.loadSync(protoPath, {
    keepCase: false,
    longs: String,
    enums: String,
    defaults: true,
    oneofs: true
  });
  const proto = grpc.loadPackageDefinition(packageDefinition).iec61850studio;
  grpcClient = new proto.Iec61850Studio(grpcAddress, grpc.credentials.createInsecure());
}

function startBackend() {
  if (backendProcess) return;
  backendProcess = spawn(backendExe, [], {
    cwd: path.dirname(backendExe),
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe']
  });

  backendProcess.stdout.on('data', (data) => broadcast('backend:log', { level: 'INFO', text: data.toString('utf8').trim() }));
  backendProcess.stderr.on('data', (data) => broadcast('backend:log', { level: 'ERROR', text: data.toString('utf8').trim() }));
  backendProcess.on('exit', (code) => {
    backendProcess = null;
    if (!quitting) {
      broadcast('backend:log', { level: 'ERROR', text: `后端进程已退出，代码 ${code ?? 'unknown'}` });
    }
  });
}

function stopBackend() {
  if (workspaceStream) {
    workspaceStream.cancel();
    workspaceStream = null;
  }
  if (backendProcess && !backendProcess.killed) {
    backendProcess.kill();
  }
}

function waitForGrpcReady(timeoutMs = 6000) {
  return new Promise((resolve) => {
    const deadline = Date.now() + timeoutMs;
    const tryOnce = () => {
      grpcClient.GetServerInfo({}, (error) => {
        if (!error) {
          resolve(true);
          return;
        }
        if (Date.now() >= deadline) {
          resolve(false);
          return;
        }
        setTimeout(tryOnce, 200);
      });
    };
    tryOnce();
  });
}

function broadcast(channel, payload) {
  for (const win of windows) {
    if (!win.isDestroyed()) {
      win.webContents.send(channel, payload);
    }
  }
}

function sendToFocused(channel, payload) {
  const win = BrowserWindow.getFocusedWindow();
  if (win && !win.isDestroyed()) {
    win.webContents.send(channel, payload);
  }
}

function createWindow(options = {}) {
  const query = new URLSearchParams();
  if (options.moduleId) query.set('module', options.moduleId);
  if (options.detached) query.set('detached', '1');

  const win = new BrowserWindow({
    width: options.detached ? 1180 : 1440,
    height: options.detached ? 760 : 900,
    minWidth: 980,
    minHeight: 650,
    title: options.detached ? `IEC 61850 - ${options.title ?? options.moduleId}` : 'IEC 61850 Client Studio',
    backgroundColor: '#e7eaee',
    show: false,
    webPreferences: {
      preload: path.join(appRoot, 'src', 'preload', 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false
    }
  });

  const htmlPath = path.join(appRoot, 'src', 'renderer', 'index.html');
  win.loadFile(htmlPath, { query: Object.fromEntries(query) });
  win.once('ready-to-show', () => win.show());
  win.on('closed', () => windows.delete(win));
  windows.add(win);
  return win;
}

function buildMenu() {
  return Menu.buildFromTemplate([
    {
      label: '文件',
      submenu: [
        { label: '新建工程', accelerator: 'Ctrl+N', click: () => sendToFocused('menu:action', { type: 'project:new' }) },
        { label: '打开工程...', accelerator: 'Ctrl+O', click: () => sendToFocused('menu:action', { type: 'project:open' }) },
        { label: '保存工程', accelerator: 'Ctrl+S', click: () => sendToFocused('menu:action', { type: 'project:save' }) },
        { type: 'separator' },
        { label: '退出', role: 'quit' }
      ]
    },
    {
      label: '连接',
      submenu: [
        { label: '连接 IED...', accelerator: 'F5', click: () => sendToFocused('menu:action', { type: 'connect:open' }) },
        { label: '连接模拟站', accelerator: 'Ctrl+F5', click: () => sendToFocused('menu:action', { type: 'connect:mock' }) },
        { label: '断开连接', accelerator: 'Shift+F5', click: () => sendToFocused('menu:action', { type: 'connect:disconnect' }) },
        { type: 'separator' },
        { label: '刷新在线模型', accelerator: 'F6', click: () => sendToFocused('menu:action', { type: 'model:refresh' }) }
      ]
    },
    {
      label: '视图',
      submenu: [
        { label: '数据模型浏览器', accelerator: 'Ctrl+1', click: () => sendToFocused('menu:action', { type: 'module:open', moduleId: 'model' }) },
        { label: '数据对象查看', accelerator: 'Ctrl+2', click: () => sendToFocused('menu:action', { type: 'module:open', moduleId: 'data' }) },
        { label: '数据集', accelerator: 'Ctrl+3', click: () => sendToFocused('menu:action', { type: 'module:open', moduleId: 'datasets' }) },
        { label: '报告', accelerator: 'Ctrl+4', click: () => sendToFocused('menu:action', { type: 'module:open', moduleId: 'reports' }) },
        { label: '控制', accelerator: 'Ctrl+5', click: () => sendToFocused('menu:action', { type: 'module:open', moduleId: 'control' }) },
        { label: '文件与日志', accelerator: 'Ctrl+6', click: () => sendToFocused('menu:action', { type: 'module:open', moduleId: 'files' }) },
        { label: '定值组', accelerator: 'Ctrl+7', click: () => sendToFocused('menu:action', { type: 'module:open', moduleId: 'settings' }) },
        { label: 'GOOSE / SV', accelerator: 'Ctrl+8', click: () => sendToFocused('menu:action', { type: 'module:open', moduleId: 'traffic' }) },
        { type: 'separator' },
        { label: '当前页面独立窗口', accelerator: 'Ctrl+Shift+D', click: () => sendToFocused('menu:action', { type: 'module:detach' }) },
        { label: '重置布局', accelerator: 'Ctrl+Shift+R', click: () => sendToFocused('menu:action', { type: 'layout:reset' }) },
        { label: '隐藏/显示左侧工程树', click: () => sendToFocused('menu:action', { type: 'pane:toggle', pane: 'left' }) },
        { label: '隐藏/显示右侧属性', click: () => sendToFocused('menu:action', { type: 'pane:toggle', pane: 'right' }) },
        { label: '隐藏/显示底部事件', click: () => sendToFocused('menu:action', { type: 'pane:toggle', pane: 'bottom' }) }
      ]
    },
    {
      label: '工具',
      submenu: [
        { label: '读取选中对象', accelerator: 'Ctrl+R', click: () => sendToFocused('menu:action', { type: 'object:read' }) },
        { label: '写入选中对象...', click: () => sendToFocused('menu:action', { type: 'object:write' }) },
        { label: '加入监视表', accelerator: 'Insert', click: () => sendToFocused('menu:action', { type: 'watch:add' }) },
        { type: 'separator' },
        { label: '协议通信统计', click: () => sendToFocused('menu:action', { type: 'module:open', moduleId: 'traffic' }) }
      ]
    },
    {
      label: '窗口',
      submenu: [
        { role: 'minimize', label: '最小化' },
        { role: 'togglefullscreen', label: '全屏' },
        { type: 'separator' },
        { label: '新建主窗口', click: () => createWindow() }
      ]
    },
    {
      label: '帮助',
      submenu: [
        {
          label: '关于',
          click: async () => {
            const focused = BrowserWindow.getFocusedWindow();
            await dialog.showMessageBox(focused ?? undefined, {
              type: 'info',
              title: '关于 IEC 61850 Client Studio',
              message: 'IEC 61850 Client Studio',
              detail: 'Electron UI + C++ gRPC 后端 + Protobuf + libiec61850。当前版本提供真实模型浏览、对象读写、数据集读取和完整客户端工作区框架。'
            });
          }
        }
      ]
    }
  ]);
}

function callUnary(method, payload) {
  return new Promise((resolve) => {
    if (!grpcClient || typeof grpcClient[method] !== 'function') {
      resolve({ ok: false, message: `未知后端方法：${method}` });
      return;
    }
    grpcClient[method](payload ?? {}, (error, response) => {
      if (error) {
        resolve({ ok: false, message: error.message, grpcCode: error.code });
        return;
      }
      resolve(response);
    });
  });
}

function startWorkspaceStream() {
  if (!grpcClient || workspaceStream) return;
  workspaceStream = grpcClient.StreamWorkspace({});
  workspaceStream.on('data', (state) => broadcast('workspace:update', state));
  workspaceStream.on('error', (error) => {
    workspaceStream = null;
    if (!quitting && error.code !== grpc.status.CANCELLED) {
      broadcast('backend:log', { level: 'ERROR', text: `工作区流异常：${error.message}` });
      setTimeout(startWorkspaceStream, 1000);
    }
  });
  workspaceStream.on('end', () => {
    workspaceStream = null;
    if (!quitting) setTimeout(startWorkspaceStream, 1000);
  });
}

ipcMain.handle('iec:call', async (_, method, payload) => callUnary(method, payload));
ipcMain.handle('iec:getWorkspace', async () => callUnary('GetWorkspace', {}));
ipcMain.handle('window:detachModule', async (_, moduleId, title) => {
  createWindow({ moduleId, title, detached: true });
  return { ok: true };
});

app.whenReady().then(async () => {
  loadGrpcClient();
  startBackend();
  Menu.setApplicationMenu(buildMenu());
  createWindow();
  const ready = await waitForGrpcReady();
  if (ready) {
    startWorkspaceStream();
  } else {
    broadcast('backend:log', { level: 'ERROR', text: 'gRPC 后端启动超时。请检查 backend/bin 下可执行文件和依赖 DLL。' });
  }
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow();
});

app.on('before-quit', () => {
  quitting = true;
  stopBackend();
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
