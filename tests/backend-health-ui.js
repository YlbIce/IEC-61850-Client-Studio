const {app,BrowserWindow,dialog,Menu}=require('electron');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');app.setPath('userData',path.join(root,'.tmp','health-ui-'+Date.now()));
const health=require('../src/main/backend-health'),inspect=health.inspectBackend;
let missing=true,ownedChild;const dialogs=[];
health.inspectBackend=async exe=>missing?{ok:false,exe,checked:[],issues:[{kind:'runtime',file:'VCRUNTIME140.dll',message:'运行库缺失（测试注入）'}]}:inspect(exe);
const cp=require('node:child_process'),spawn=cp.spawn;
cp.spawn=function(exe,...args){const child=spawn.call(this,exe,...args);if(exe.endsWith('iec61850-client-backend.exe'))ownedChild=child;return child;};
dialog.showMessageBox=async(_win,options)=>{dialogs.push(options);return {response:!missing&&options.buttons.includes('重新检查并启动')?1:0};};
require('../src/main/main');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function wait(fn){for(let i=0;i<150;i++){try{const result=await fn();if(result)return result;}catch{}await delay(100);}throw Error('health UI timeout');}
app.whenReady().then(async()=>{
 let result;
 try{
  const win=await wait(()=>BrowserWindow.getAllWindows().find(w=>w.webContents.getURL().endsWith('/renderer/index.html')));
  const js=code=>win.webContents.executeJavaScript(code);
  await wait(()=>dialogs.length);assert(dialogs[0].buttons.includes('下载微软 x64 运行库'));assert(dialogs[0].detail.includes(health.VC_URL));assert.equal(ownedChild,undefined);
  await wait(()=>js('typeof window.iec61850?.call === "function"'));
  await assert.rejects(js('window.iec61850.call("RefreshModel",{})'),/后端未就绪/);
  await js('document.querySelector("[data-action=\\"model:refresh\\"]").click()');await delay(200);
  assert.equal(await js('document.body.innerText.includes("模型刷新完成")'),false);
  missing=false;
  const help=Menu.getApplicationMenu().items.find(i=>i.label==='帮助');
  await help.submenu.items.find(i=>i.label==='检查后端环境…').click();
  await wait(()=>js('window.iec61850.getWorkspace().then(r=>!!r.server)'));
  assert(ownedChild);const before=dialogs.length;
  // Suppress retry only for the following intentionally terminated test child.
  dialog.showMessageBox=async(_win,options)=>{dialogs.push(options);return {response:0};};
  ownedChild.kill();await wait(()=>dialogs.length>before);await delay(500);
  assert.equal(dialogs.length,before+1);await assert.rejects(js('window.iec61850.getWorkspace()'),/后端未就绪/);
  const report=JSON.parse(fs.readFileSync(path.join(app.getPath('userData'),'backend-diagnostic.json')));assert.equal(report.ok,false);
  result={passed:true,missingRuntimeDialog:true,noSpawnWhenMissing:true,retryRealBackend:true,unexpectedExitDialog:true,noFalseRefreshSuccess:true};
 }catch(error){result={passed:false,error:error.stack};}
 finally{fs.writeFileSync(path.join(root,'backend-health-ui-result.json'),JSON.stringify(result));for(const win of BrowserWindow.getAllWindows())win.destroy();app.exit(result.passed?0:1);}
});
