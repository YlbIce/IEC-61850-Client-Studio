const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {inspectBackend,readPe,failureReport,explain,VC_URL}=require('../src/main/backend-health');
function pe(imports=[],machine=0x8664){
 const b=Buffer.alloc(4096);b.writeUInt16LE(0x5a4d);b.writeUInt32LE(128,60);b.writeUInt32LE(0x4550,128);
 b.writeUInt16LE(machine,132);b.writeUInt16LE(1,134);b.writeUInt16LE(240,148);b.writeUInt16LE(0x20b,152);
 b.writeUInt32LE(4096,272);b.writeUInt32LE(4096,404);b.writeUInt32LE(3584,408);b.writeUInt32LE(512,412);
 imports.forEach((name,i)=>{b.writeUInt32LE(4608+i*128,512+i*20+12);b.write(name+'\0',1024+i*128);});return b;
}
test('PE inspection distinguishes missing runtime, transitive vendor DLL, architecture and corrupt file',async()=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'studio-health-'));
 try{
  const exe=path.join(dir,'backend.exe'),systemDir=path.join(dir,'system');await fs.mkdir(systemDir);
  await fs.writeFile(exe,pe(['vendor.dll','VCRUNTIME140.dll','api-ms-win-core-test.dll']));
  await fs.writeFile(path.join(dir,'vendor.dll'),pe(['nested.dll']));
  let r=await inspectBackend(exe,{systemDir});assert(r.issues.some(i=>i.kind==='runtime'));assert(r.issues.some(i=>i.file==='nested.dll'));assert(explain(r).includes(VC_URL));
  await fs.writeFile(path.join(dir,'nested.dll'),pe());await fs.writeFile(path.join(systemDir,'VCRUNTIME140.dll'),pe());
  r=await inspectBackend(exe,{systemDir});assert(r.ok);assert.equal(r.checked.length,4);
  await fs.writeFile(path.join(dir,'nested.dll'),pe([],0x14c));assert((await inspectBackend(exe,{systemDir})).issues.some(i=>i.kind==='architecture'));
  await fs.writeFile(exe,Buffer.from('broken'));assert.equal((await inspectBackend(exe,{systemDir})).issues[0].kind,'invalid-file');
  assert.equal((await inspectBackend(path.join(dir,'absent.exe'),{systemDir})).issues[0].kind,'missing-file');
 }finally{await fs.rm(dir,{recursive:true,force:true});}
});
test('loader exit codes do not misclassify access violations as missing VC runtime',()=>{
 assert.equal(failureReport('test.exe',{code:-1073741515}).exitHex,'0xC0000135');
 assert.equal(failureReport('test.exe',{code:0xc0000139}).issues[0].kind,'loader-runtime');
 assert.equal(failureReport('test.exe',{code:0xc0000005}).issues[0].kind,'process');
 assert.match(explain(failureReport('test.exe',{code:0xc0000005})),/并非已确认/);
 assert.throws(()=>readPe(Buffer.alloc(10)));
});
test('real bundled backend imports are available on this development machine',async()=>{
 const report=await inspectBackend(path.resolve(__dirname,'../backend/bin/iec61850-client-backend.exe'));
 assert(report.ok,JSON.stringify(report.issues));assert(report.checked.length>5);
});
