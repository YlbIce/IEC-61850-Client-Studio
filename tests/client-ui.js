const {app,BrowserWindow,dialog}=require('electron');
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');app.setPath('userData',path.join(root,'.tmp/client-ui-profile'));
app.commandLine.appendSwitch('force-device-scale-factor','1');
require('../src/main/main');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const wait=async fn=>{for(let i=0;i<300;i++){try{const result=await fn();if(result)return result;}catch{}await delay(100);}throw Error('Client UI timeout');};
app.whenReady().then(async()=>{
 let service;
 try{
  const main=await wait(()=>BrowserWindow.getAllWindows().find(w=>w.webContents.getURL().endsWith('/renderer/index.html')));
  const js=(win,code)=>win.webContents.executeJavaScript(code).catch(error=>{throw new Error(code+"\n"+error.message);});
  await wait(()=>js(main,'window.iec61850.getWorkspace().then(r=>r.server?.name)'));
  assert.equal(await js(main,'document.querySelectorAll(".tab").length'),8);
  await js(main,'document.querySelector("[data-action=\\"connect:mock\\"]").click()');
  await wait(()=>js(main,'window.iec61850.getWorkspace().then(r=>r.connection?.connected&&r.model?.roots?.length>0)'));
  await wait(()=>js(main,'document.querySelectorAll(".tree-node").length>5'));
  for(const mode of ['light','dark']){
   await js(main,`window.appearance.set('${mode}')`);await delay(120);
   for(const id of ['model','data','datasets','reports','control','files','settings','traffic']){
    await js(main,`document.querySelector('[data-tab="${id}"]').click()`);
    await wait(()=>js(main,`!!document.querySelector('.tab.active[data-tab="${id}"]')`));
    await wait(()=>js(main,'document.querySelector("#busyOverlay").hidden'));
    await delay(120);
   }
   await js(main,'document.querySelector("[data-tab=model]").click()');
   await wait(()=>js(main,'!!document.querySelector(".tab.active[data-tab=model]")'));await delay(200);
   await fs.mkdir(path.join(root,'docs/screenshots'),{recursive:true});
   await fs.writeFile(path.join(root,`docs/screenshots/client-${mode}.png`),(await main.webContents.capturePage()).toPNG());
  }
  await js(main,'window.iec61850.detachModule("data","数据对象")');
  const detached=await wait(()=>BrowserWindow.getAllWindows().find(w=>w.webContents.getURL().includes('detached=1')));
  await wait(()=>js(detached,'document.documentElement.dataset.theme==="dark"'));
  await js(detached,'window.appearance.set("light")');await wait(()=>js(main,'document.documentElement.dataset.theme==="light"'));detached.destroy();
  await assert.rejects(js(main,'window.iec61850.call("constructor",{})'),/不允许/);
  await js(main,'window.iec61850.capture()');
  const capture=await wait(()=>BrowserWindow.getAllWindows().find(w=>w.webContents.getURL().endsWith('/capture/index.html')));
  const capability=await js(capture,'window.capture.command("capabilities")');assert(capability.analysis);
  dialog.showOpenDialog=async()=>({canceled:false,filePaths:[path.join(root,'docs/blog/examples/sample-mms.pcapng')]});
  await js(capture,'document.querySelector("#mms-ports").value="18122";document.querySelector("[data-action=open]").click()');
  await wait(()=>js(capture,'document.querySelectorAll("tr[data-frame]").length>0'));
  assert.equal((await js(capture,'window.capture.command("ai-lessons")')).length,4);
  await js(capture,'window.capture.command("ai-history")');
  assert(await wait(()=>BrowserWindow.getAllWindows().find(w=>w.webContents.getURL().endsWith('/ai/history.html'))));
  const {createCaptureWindow}=require('../src/capture/window');
  const missing=await createCaptureWindow({root:path.join(root,'.tmp/no-addon')});
  await assert.rejects(js(missing,'window.capture.command("capabilities")'),/未安装可选/);
  assert((await js(main,'window.iec61850.getWorkspace()')).connection.connected);
  missing.destroy();
  await js(main,'window.iec61850.call("Disconnect",{})');
  const serviceRoot=path.resolve(root,'../Iec61850ServiceStudio');
  const {Backend}=require(path.join(serviceRoot,'src/main/backend'));
  service=new Backend(serviceRoot,path.join(serviceRoot,'backend/bin/iec61850-service-backend.exe'));
  service.on('failure',()=>{});await service.start();
  const project=require(path.join(serviceRoot,'src/shared/project')).defaultProject();project.port=18132;
  const fileRoot=path.join(root,'.tmp/mms-file-regression');
  await fs.mkdir(path.join(fileRoot,'cfg'),{recursive:true});
  await fs.writeFile(path.join(fileRoot,'cfg/61850.INI'),'nested MMS file test — 中文');
  await fs.writeFile(path.join(fileRoot,'readme.txt'),'root MMS file test');
  await fs.writeFile(path.join(fileRoot,'empty'),'');
  project.file_service=true;project.file_root=fileRoot.replaceAll('\\','/')+'/';
  await service.call('Apply',project);await service.call('Start');
  const connected=await js(main,'window.iec61850.call("Connect",{host:"127.0.0.1",port:18132,mock:false,timeoutMs:5000})');assert(connected.ok,connected.message);
  const value=await js(main,'window.iec61850.call("ReadObject",{reference:"STUDIOLD0/GGIO1.AnIn1.mag.f",fc:"MX"})');assert.equal(Number(value.value),220);
  main.show(); main.focus();
  await js(main, 'refreshWorkspace().then(()=>selectDataSet("STUDIOLD0/LLN0.Measurements"))');
  await wait(()=>js(main,'document.querySelector("#datasetResult").textContent.includes("A 相电压")'));
  await service.call('Update',{reference:'STUDIOLD0/GGIO1.AnIn1',value:237.5,quality:4096});
  await wait(()=>js(main,'document.querySelector("#datasetResult").textContent.includes("237.500000") && document.querySelector("#datasetResult").textContent.includes("4096")'));
  await js(main,'togglePointExpanded("STUDIOLD0/GGIO1.AnIn1")');
  await wait(()=>js(main,'document.querySelector("#datasetResult").textContent.includes("AnIn1.mag.f")'));
  const menuTiming = await js(main, `(() => {
    const before=document.querySelector('#modelTree').firstChild;
    const row=document.querySelector('.page.active [data-context-kind="DO"]');
    const start=performance.now();row.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:350,clientY:240}));
    if (document.querySelector('#contextMenu').hidden) throw Error('Missing DO menu');
    if (before!==document.querySelector('#modelTree').firstChild) throw Error('Right click rebuilt tree');
    if (!document.querySelector('#contextMenu [data-action="watch:add"]')) throw Error('Missing watch');
    return performance.now()-start;
  })()`);
  assert(menuTiming<200, 'Context menu too slow: '+menuTiming);
  await js(main, `document.querySelector('#contextMenu [data-action="object:read-node"]').click()`);
  await wait(()=>js(main,'state.readValues[0]?.reference==="STUDIOLD0/GGIO1.AnIn1" && !state.busy'));
  await js(main, `document.querySelector('.page.active [data-context-kind="DA"][data-ref="STUDIOLD0/GGIO1.AnIn1.dU"]').dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:350,clientY:300}))`);
  assert.equal(await js(main, `document.querySelector('#contextMenu [data-action="watch:add"]').dataset.fc`),'DC');
  await js(main, `document.querySelector('#contextMenu [data-action="watch:add"]').click()`);
  await wait(()=>js(main,'state.readValues[0]?.reference==="STUDIOLD0/GGIO1.AnIn1.dU" && !state.busy'));
  assert.equal(await js(main,'state.readValues[0].fc'),'DC');
  assert((await js(main,'state.readValues[0].value')).includes('A 相电压'));
  for (const mode of ['light','dark']) {
    await js(main, `window.appearance.set('${mode}')`); await delay(250);
    await fs.writeFile(path.join(root,`docs/screenshots/client-datasets-${mode}.png`),(await main.webContents.capturePage()).toPNG());
  }
  await service.call('Update',{reference:'STUDIOLD0/GGIO1.AnIn1',value:246.75,quality:0});
  await wait(()=>js(main,'document.querySelector(".point-attributes").textContent.includes("246.750000")'));
  await js(main,'readObjectReference("STUDIOLD0/GGIO1.AnIn1.mag.f","MX")');
  await js(main,'openModule("data",true)');
  await service.call('Update',{reference:'STUDIOLD0/GGIO1.AnIn1',value:248.125,quality:0});
  await wait(()=>js(main,'document.querySelector("#pageHost").textContent.includes("248.125000")'));
  await service.call('Simulation',{reference:'STUDIOLD0/GGIO1.AnIn1',waveform:'sine',base:230,amplitude:5,period:2});
  await service.call('Simulation',{enabled:true});
  await delay(1200);
  const changed=await js(main,'window.iec61850.call("ReadObject",{reference:"STUDIOLD0/GGIO1.AnIn1.mag.f",fc:"MX"})');
  assert.notEqual(Number(changed.value),237.5);
  const listing=await js(main,'window.iec61850.call("GetFiles",{directory:"/"})');
  assert(listing.entries.some(e=>e.path==='cfg/61850.INI'));
  assert.equal(listing.entries.find(e=>e.path==='empty').directory,false);
  const sub=await js(main,'window.iec61850.call("GetFiles",{directory:"cfg"})');
  assert(sub.entries.some(e=>e.path==='cfg/61850.INI'));
  const missingFile=await js(main,'window.iec61850.call("ReadFile",{path:"missing.txt"})');
  assert(missingFile.textPreview.includes('IED_ERROR_22'));
  const emptyFile=await js(main,'window.iec61850.call("ReadFile",{path:"empty"})');assert.equal(emptyFile.textPreview,'');
  await js(main,'openModule("files",true);document.querySelector("#fileDir").value="cfg";listFiles()');
  await js(main, `document.querySelector('[data-action="file:open"][data-ref="cfg/61850.INI"]').click()`);
  await wait(()=>js(main,'document.querySelector(".text-preview")?.textContent.includes("nested MMS file test")'));
  assert.equal(await js(main,'document.querySelector("#fileDir").value'),'cfg');
  assert.equal(await js(main,'document.querySelector("#filePath").value'),'cfg/61850.INI');
  await fs.writeFile(path.join(root,'docs/screenshots/client-files-dark.png'),(await main.webContents.capturePage()).toPNG());
  const largeMenuTiming=await js(main, `(() => {
    const saved=state.workspace.model, selected=state.selectedNode;
    try {
      hideContextMenu();openModule('model',true);
      state.workspace.model={roots:Array.from({length:22000},(_,i)=>({name:'Value'+i,reference:'PERFLD/GGIO1.Value'+i,kind:'DA',fc:'MX',children:[]}))};
      renderModelTree();
      const tree=document.querySelector('#modelTree'), first=tree.firstChild, row=tree.querySelector('.tree-node:last-child');
      tree.getBoundingClientRect();
      const start=performance.now();row.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:280,clientY:240}));
      const elapsed=performance.now()-start;
      if (tree.firstChild!==first || document.querySelector('#contextMenu').hidden) throw Error('Large tree menu invalid');
      return elapsed;
    } finally {hideContextMenu();state.workspace.model=saved;state.selectedNode=selected;renderModelTree();}
  })()`);
  assert(largeMenuTiming<200,'Large tree menu too slow: '+largeMenuTiming);
  await js(main,'window.iec61850.call("Disconnect",{})');await service.close();service=null;
  await fs.writeFile(path.join(root,'client-ui-result.json'),JSON.stringify({passed:true,menuTimingMs:menuTiming,largeTreeNodes:22000,largeMenuTimingMs:largeMenuTiming,datasetContextActions:true,themes:true,detached:true,optionalCapture:true,offlinePcap:true,realMmsRead:value.value,ai:'offline courses/history; full mock-provider test in test:ai-ui'}));
  for(const win of BrowserWindow.getAllWindows())win.destroy();app.quit();
 }catch(error){await service?.close().catch(()=>{});await fs.writeFile(path.join(root,'client-ui-result.json'),JSON.stringify({passed:false,error:error.stack}));app.quit();}
});
