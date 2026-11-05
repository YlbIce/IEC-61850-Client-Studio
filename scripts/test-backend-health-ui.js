const {spawn}=require('node:child_process'),path=require('node:path'),fs=require('node:fs');
const root=path.resolve(__dirname,'..'),env={...process.env};delete env.ELECTRON_RUN_AS_NODE;
const resultFile=path.join(root,'backend-health-ui-result.json');fs.writeFileSync(resultFile,JSON.stringify({passed:false,error:'Test did not finish'}));
const child=spawn(require('electron'),[path.join(root,'tests/backend-health-ui.js')],{cwd:root,env,stdio:'ignore',windowsHide:true});
const timer=setTimeout(()=>{child.kill();process.exitCode=1;},60000);
child.on('error',error=>{clearTimeout(timer);console.error(error);process.exitCode=1;});
child.on('exit',code=>{clearTimeout(timer);const result=JSON.parse(fs.readFileSync(resultFile));console.log(result);process.exitCode=code||(!result.passed?1:0);});
