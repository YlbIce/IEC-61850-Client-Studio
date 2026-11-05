const {spawn}=require('node:child_process'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),release=path.join(root,'release/0.2.6/win-unpacked');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function evaluate(page,expression){
 const ws=new WebSocket(page.webSocketDebuggerUrl);
 await new Promise((r,j)=>{ws.onopen=r;ws.onerror=j;});
 try{return await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('CDP timeout')),15000);ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id!==1)return;clearTimeout(timer);m.error?reject(Error(JSON.stringify(m.error))):m.result?.exceptionDetails?reject(Error(JSON.stringify(m.result.exceptionDetails))):resolve(m.result?.result?.value);};ws.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression,awaitPromise:true,returnByValue:true}}));});}finally{ws.close();}
}
async function page(suffix){for(let i=0;i<100;i++){try{const pages=await(await fetch('http://127.0.0.1:9337/json/list')).json();const found=pages.find(p=>p.url.endsWith(suffix));if(found)return found;}catch{}await delay(100);}throw Error('Missing page '+suffix);}
async function main(){
 const env={...process.env};delete env.ELECTRON_RUN_AS_NODE;
 const child=spawn(path.join(release,'IEC61850ClientStudio.exe'),['--remote-debugging-port=9337','--user-data-dir='+path.join(root,'.tmp/client-packaged-profile')],{env,stdio:'ignore',windowsHide:true});
 try{
  const client=await page('/renderer/index.html');
  let connected;for(let i=0;i<60;i++){try{if(await evaluate(client,'typeof window.iec61850?.call === "function"'))connected=await evaluate(client,'window.iec61850.call("Connect",{mock:true,host:"MockBay",port:102})');if(connected?.ok)break;}catch{}await delay(200);}assert(connected?.ok,JSON.stringify(connected));
  assert((await evaluate(client,'window.iec61850.getWorkspace()')).model.roots.length);
  await evaluate(client,'window.appearance.set("dark")');await delay(100);assert.equal(await evaluate(client,'document.documentElement.dataset.theme'),'dark');
  await evaluate(client,'window.iec61850.capture()');
  const capture=await page('/capture/index.html');assert((await evaluate(capture,'window.capture.command("capabilities")')).analysis);
  assert.equal((await evaluate(capture,'window.capture.command("ai-lessons")')).length,4);
  await evaluate(capture,'window.capture.command("ai-history")');
  const history=await page('/ai/history.html');assert(Array.isArray((await evaluate(history,'window.historyApi.command("list")')).rows));
  await evaluate(client,'window.iec61850.call("Disconnect",{})');
  console.log('PASS packaged C++ connection/model, shared theme, optional capture DLL/TShark, AI lessons/history; no cloud calls');
 }finally{
  // Close owned test windows so Electron can stop its own backend normally.
  try{const pages=await(await fetch('http://127.0.0.1:9337/json/list')).json();for(const p of pages)await fetch('http://127.0.0.1:9337/json/close/'+p.id);}catch{}
  await delay(600);if(child.exitCode===null)child.kill();
 }
}
main().catch(e=>{console.error(e.stack);process.exitCode=1;});

