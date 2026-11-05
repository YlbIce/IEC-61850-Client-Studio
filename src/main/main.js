const theme = require('./theme');
const {pathToFileURL}=require('node:url');
for(const stream of [process.stdout,process.stderr])stream.on('error',()=>{});
const { app, BrowserWindow, Menu, ipcMain, dialog, shell } = require('electron');
const { spawn } = require('child_process');
const fs = require('node:fs/promises');
const {inspectBackend,failureReport,explain,VC_URL}=require('./backend-health');
const path = require('path');
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');

const isPackaged = app.isPackaged;
const appRoot = path.resolve(__dirname, '..', '..');
const protoPath = path.join(appRoot, 'proto', 'iec61850studio.proto');
const backendExe = isPackaged
  ? path.join(process.resourcesPath, 'backend', 'bin', 'iec61850-client-backend.exe')
  : path.join(appRoot, 'backend', 'bin', 'iec61850-client-backend.exe');
let grpcAddress;

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

let backendReady=false, startupTask=null, lastHealth=null, healthDialogOpen=false, failureReported=false;
const healthFile=()=>path.join(app.getPath('userData'),'backend-diagnostic.json');
async function saveHealth(report){try{await fs.mkdir(app.getPath('userData'),{recursive:true});await fs.writeFile(healthFile(),JSON.stringify(report,null,2));}catch(error){broadcast('backend:log',{level:'ERROR',text:'诊断报告保存失败：'+error.message});}}
async function showBackendHealth(report=lastHealth){
 if(healthDialogOpen||quitting)return;
 if(!report)report=await inspectBackend(backendExe);
 healthDialogOpen=true;
 const actions=report.ok?['关闭']:['关闭','重新检查并启动','打开后端目录','查看诊断报告'];
 const runtime=report.issues.some(i=>['runtime','loader-runtime'].includes(i.kind));
 if(runtime)actions.push('下载微软 x64 运行库');
 let choice;
 try{await saveHealth(report);choice=await dialog.showMessageBox(BrowserWindow.getFocusedWindow()||[...windows][0],{
  type:report.ok?'info':'error',title:'C++ 后端环境检查',message:report.ok?'后端依赖检查通过':'本机通信后端不可用',
  detail:report.ok?`已检查 ${report.checked.length} 个文件。后端状态：${backendReady?'已就绪':'尚未就绪'}。`:explain(report)+'\n\n诊断报告：'+healthFile(),buttons:actions,defaultId:0,cancelId:0,noLink:true
 });}finally{healthDialogOpen=false;}
 const action=actions[choice?.response];
 if(action==='重新检查并启动')await startBackend();
 else if(action==='下载微软 x64 运行库')await shell.openExternal(VC_URL);
 else if(action==='打开后端目录')await shell.openPath(path.dirname(backendExe));
 else if(action==='查看诊断报告')await shell.openPath(healthFile());
}
function failBackend(report){
 if(quitting||failureReported)return;
 failureReported=true;backendReady=false;lastHealth=report;
 if(workspaceStream){const stream=workspaceStream;workspaceStream=null;stream.cancel();}
 broadcast('backend:status',{ready:false,message:'本机通信后端不可用，请打开“帮助 → 检查后端环境”'});
 broadcast('backend:log',{level:'ERROR',text:explain(report)});
 void saveHealth(report);void showBackendHealth(report).catch(error=>broadcast('backend:log',{level:'ERROR',text:error.message}));
}
function startBackend(){
 if(startupTask)return startupTask;if(backendReady)return Promise.resolve(true);
 startupTask=(async()=>{
  failureReported=false;
  try{
   const report=await inspectBackend(backendExe);lastHealth=report;
   if(!report.ok){failBackend(report);return false;}
   if(quitting)return false;
   let stderr='';
   const child=spawn(backendExe,['--listen',grpcAddress],{cwd:path.dirname(backendExe),windowsHide:true,stdio:['ignore','pipe','pipe']});
   backendProcess=child;
   child.stdout.on('data',data=>broadcast('backend:log',{level:'INFO',text:data.toString('utf8').trim()}));
   child.stderr.on('data',data=>{stderr=(stderr+data.toString('utf8')).slice(-8000);broadcast('backend:log',{level:'ERROR',text:data.toString('utf8').trim()});});
   child.on('error',error=>{if(backendProcess===child){backendProcess=null;failBackend(failureReport(backendExe,{error,stderr}));}});
   child.on('exit',code=>{if(backendProcess===child){backendProcess=null;failBackend(failureReport(backendExe,{code,stderr}));}});
   const ready=await waitForGrpcReady(10000);
   if(!ready||backendProcess!==child){
    if(backendProcess===child){backendProcess=null;child.kill();}
    failBackend(failureReport(backendExe,{stderr,message:'进程未能在 10 秒内建立本机 gRPC 通道。依赖文件检查通过；请检查后端输出、安全软件和本机端口占用。'}));return false;
   }
   backendReady=true;lastHealth={...report,ready:true};await saveHealth(lastHealth);
   broadcast('backend:status',{ready:true,message:'本机通信后端已就绪'});startWorkspaceStream();return true;
  }catch(error){failBackend(failureReport(backendExe,{error}));return false;}
 })().finally(()=>{startupTask=null;});return startupTask;
}

function stopBackend() {
  backendReady=false;
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
      if(quitting||!backendProcess||failureReported){resolve(false);return;}
      grpcClient.GetServerInfo({}, {deadline:new Date(Date.now()+700)}, (error) => {
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
    icon:path.join(appRoot,'build/icon.ico'),
    backgroundColor: '#e7eaee',
    show: false,
    webPreferences: {
      preload: path.join(appRoot, 'src', 'preload', 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true
    }
  });

  const htmlPath = path.join(appRoot, 'src', 'renderer', 'index.html');
  win.studioUrl=pathToFileURL(htmlPath).href+(query.size?'?'+query.toString():'');
  theme.attach(win,htmlPath,query.size?'?'+query.toString():'');
  win.webContents.setWindowOpenHandler(()=>({action:'deny'}));
  win.webContents.on('will-navigate',event=>event.preventDefault());
  win.loadFile(htmlPath, { query: Object.fromEntries(query) }).catch(error=>dialog.showErrorBox('窗口加载失败',error.message));
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
      submenu: [theme.menu(),
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
        { label: '报文捕获与 AI 分析…', accelerator:'Ctrl+Shift+P', click:()=>openCapture(BrowserWindow.getFocusedWindow()).catch(e=>dialog.showErrorBox('报文分析',e.message)) },
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
        {label:'检查后端环境…',click:async()=>{const report=await inspectBackend(backendExe);await showBackendHealth(report.ok&&!backendReady?(lastHealth?.ok===false?lastHealth:failureReport(backendExe,{message:'依赖文件齐全，但后端尚未就绪，可重新检查并启动。'})):report);}},
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

async function callUnary(method,payload){
 if(startupTask)await startupTask;
 if(!backendReady)throw Error('本机 C++ 通信后端未就绪，请打开“帮助 → 检查后端环境”。');
 if(!grpcClient||typeof grpcClient[method]!=='function')throw Error(`未知后端方法：${method}`);
 return new Promise((resolve,reject)=>grpcClient[method](payload??{},{deadline:new Date(Date.now()+30000)},(error,response)=>{
  if(error){reject(Error(`本机后端请求失败：${error.message}`));return;}resolve(response);
 }));
}

function startWorkspaceStream() {
  if (!backendReady || quitting || !grpcClient || workspaceStream) return;
  const stream = grpcClient.StreamWorkspace({});
  workspaceStream = stream;
  stream.on('data', (state) => { if(workspaceStream===stream)broadcast('workspace:update', state); });
  stream.on('error', (error) => {
    if(workspaceStream!==stream)return;
    workspaceStream = null;
    if (!quitting && error.code !== grpc.status.CANCELLED) {
      broadcast('backend:log', { level: 'ERROR', text: `工作区流异常：${error.message}` });
      setTimeout(startWorkspaceStream, 1000);
    }
  });
  stream.on('end', () => {
    if(workspaceStream!==stream)return;
    workspaceStream = null;
    if (!quitting) setTimeout(startWorkspaceStream, 1000);
  });
}

const methods = new Set([...require('node:fs').readFileSync(protoPath,'utf8').matchAll(/rpc\s+(\w+)\s*\(/g)].map(match=>match[1]));
methods.delete('StreamWorkspace');methods.delete('StreamReports');
function owner(event){const win=BrowserWindow.fromWebContents(event.sender);if(!windows.has(win)||event.senderFrame!==event.sender.mainFrame||event.senderFrame.url!==win.studioUrl)throw Error('拒绝未授权的客户端操作');return win;}
async function openCapture(win){if(!windows.has(win))win=[...windows].find(w=>!w.isDestroyed());if(!win)throw Error('没有客户端主窗口');return require('../capture/window').createCaptureWindow({parent:win,root:appRoot,isPackaged:app.isPackaged,resourcesPath:process.resourcesPath});}
ipcMain.handle('iec:call', async (event, method, payload) => {owner(event);if(!methods.has(method))throw Error('不允许的后端方法');return callUnary(method,payload);});
ipcMain.handle('iec:getWorkspace', async event => {owner(event);return callUnary('GetWorkspace', {});});
ipcMain.handle('window:capture',async event=>{await openCapture(owner(event));return {ok:true};});
ipcMain.handle('window:detachModule', async (event, moduleId, title) => {
  owner(event);
  if(!['model','data','datasets','reports','control','files','settings','traffic'].includes(moduleId))throw Error('页面无效');
  createWindow({ moduleId, title:String(title||moduleId).slice(0,80), detached: true });
  return { ok: true };
});
async function backendAddress(){return new Promise((resolve,reject)=>{const server=require('node:net').createServer();server.on('error',reject);server.listen(0,'127.0.0.1',()=>{const address='127.0.0.1:'+server.address().port;server.close(()=>resolve(address));});});}

app.whenReady().then(async () => {
  grpcAddress=await backendAddress();
  loadGrpcClient();
  theme.setApplicationMenu(buildMenu());
  createWindow();
  await startBackend();
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
