'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const VC_URL='https://aka.ms/vc14/vc_redist.x64.exe';
const VC_DOC='https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist';
function readPe(b){
 if(b.length<64||b.readUInt16LE(0)!==0x5a4d)throw Error('不是有效 Windows 可执行文件');
 const pe=b.readUInt32LE(60);if(pe+24>b.length||b.readUInt32LE(pe)!==0x4550)throw Error('PE 文件头损坏');
 const machine=b.readUInt16LE(pe+4),count=b.readUInt16LE(pe+6),size=b.readUInt16LE(pe+20),opt=pe+24;
 if(opt+size>b.length)throw Error('PE 可选头损坏');
 const magic=b.readUInt16LE(opt);if(![0x10b,0x20b].includes(magic))throw Error('PE 格式不受支持');
 const sections=[];for(let i=0;i<count;i++){const s=opt+size+i*40;if(s+40>b.length)throw Error('PE 节表损坏');sections.push({va:b.readUInt32LE(s+12),size:b.readUInt32LE(s+16),raw:b.readUInt32LE(s+20)});}
 const offset=rva=>{const s=sections.find(s=>rva>=s.va&&rva<s.va+s.size);if(!s)throw Error('PE 导入地址无效');const n=s.raw+rva-s.va;if(n>=b.length)throw Error('PE 导入表损坏');return n;};
 const dd=opt+(magic===0x20b?112:96);if(dd+16>opt+size)throw Error('PE 数据目录损坏');
 const rva=b.readUInt32LE(dd+8),imports=[];
 if(rva){let pos=offset(rva);for(let i=0;i<4096;i++,pos+=20){if(pos+20>b.length)throw Error('PE 导入描述损坏');const name=b.readUInt32LE(pos+12);if(!name)break;const start=offset(name),end=b.indexOf(0,start);if(end<start||end-start>255)throw Error('DLL 名称无效');const dll=b.toString('ascii',start,end);if(!/^[\w.+-]+\.dll$/i.test(dll))throw Error('DLL 名称无效');imports.push(dll);}}
 return {machine,imports};
}
const isRuntime=name=>/^(msvcp|vcruntime|concrt|vcomp)140[^/]*\.dll$/i.test(name);
async function inspectBackend(exe,{systemDir=path.join(process.env.SystemRoot||'C:\\Windows','System32')}={}){
 const dir=path.dirname(exe),issues=[],checked=[],visited=new Set();
 async function scan(file,recurse){if(visited.has(file.toLowerCase()))return;visited.add(file.toLowerCase());let pe;
 try{pe=readPe(await fs.readFile(file));}catch(e){issues.push({kind:e.code==='ENOENT'?'missing-file':'invalid-file',file,message:e.code==='ENOENT'?'文件缺失':e.message});return;}
 checked.push(file);if(pe.machine!==0x8664){issues.push({kind:'architecture',file,message:'需要 x64 文件，检测到不匹配的架构'});return;}
 if(!recurse)return;
 for(const name of pe.imports){if(/^(api-ms-|ext-ms-)/i.test(name))continue;
 const local=path.join(dir,name),system=path.join(systemDir,name);
 let resolved;for(const candidate of [local,system]){try{await fs.access(candidate);resolved=candidate;break;}catch{}}
 if(!resolved){issues.push({kind:isRuntime(name)?'runtime':'missing-dll',file:name,message:`${path.basename(file)} 需要 ${name}`});continue;}
 if(resolved===local)await scan(resolved,true);else if(isRuntime(name))await scan(resolved,false);
 }
 }
 await scan(exe,true);
 return {ok:issues.length===0,exe,checked,issues,at:new Date().toISOString()};
}
function failureReport(exe,{code,error,stderr='',message}={}){
 const hex=typeof code==='number'?'0x'+(code>>>0).toString(16).toUpperCase():'';
 const reason=hex==='0xC0000135'?'Windows 加载器找不到所需 DLL':hex==='0xC000007B'?'可执行文件或 DLL 架构不匹配/损坏':hex==='0xC0000139'?'DLL 入口点不存在，运行库或配套 DLL 版本可能不兼容':hex==='0xC0000142'?'DLL 初始化失败':hex==='0xC0000005'?'后端发生访问冲突，并非已确认的依赖缺失':message||error?.message||'后端意外退出';
 return {ok:false,exe,issues:[{kind:hex==='0xC0000135'||hex==='0xC0000139'?'loader-runtime':'process',file:exe,message:reason}],exitCode:code,exitHex:hex,stderr:stderr.slice(-8000),at:new Date().toISOString()};
}
function explain(report){
 const runtime=report.issues.some(i=>['runtime','loader-runtime'].includes(i.kind));
 const steps=runtime?`安装或修复 Microsoft Visual C++ v14 Redistributable（x64），安装完成后点击“重新检查并启动”。仅安装 x86 版本不够。\n微软说明：${VC_DOC}\n官方下载：${VC_URL}`:'请重新完整解压/安装本应用，保持 resources/backend/bin 内 EXE 与 DLL 配套；不要只复制 EXE，也不要从第三方 DLL 网站单独下载文件。';
 return `${report.issues.map(i=>`${i.file}：${i.message}`).join('\n')}\n\n${steps}\n\n若文件齐全仍无法启动，请检查 Windows 安全中心的拦截/隔离记录，并将诊断报告提供给开发者。此错误涉及本机通信后端，不是远端 IED 的 IP。${report.exitHex?'\n退出码：'+report.exitHex:''}${report.stderr?'\n\n后端输出：\n'+report.stderr:''}`;
}
module.exports={readPe,inspectBackend,failureReport,explain,VC_URL,VC_DOC};
