const {spawn}=require('node:child_process'),path=require('node:path'),fs=require('node:fs');
const root=path.resolve(__dirname,'..'),env={...process.env};delete env.ELECTRON_RUN_AS_NODE;
fs.writeFileSync(path.join(root,'client-ui-result.json'),JSON.stringify({passed:false,error:'Test did not finish'}));
const child=spawn(require('electron'),[path.join(root,'tests/client-ui.js')],{cwd:root,env,stdio:'ignore',windowsHide:true});
child.on('error',error=>{console.error(error);process.exitCode=1;});
child.on('exit',code=>{const result=JSON.parse(fs.readFileSync(path.join(root,'client-ui-result.json'),'utf8'));console.log(JSON.stringify(result));process.exitCode=code||(!result.passed?1:0);});
