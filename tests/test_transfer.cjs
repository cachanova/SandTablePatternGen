const {test, beforeEach} = require('node:test');
const assert = require('node:assert/strict');
const {getEventListeners} = require('node:events');
const png = new Blob([new Uint8Array([137,80,78,71,13,10,26,10,0])], {type:'image/png'});
let requests, responder;
class XHR {
    constructor() { this.upload={}; }
    open(method,url) { this.method=method;this.url=url; }
    send(body) { this.body=body;requests.push(this);queueMicrotask(()=>responder(this)); }
    abort() { this.onabort?.(); }
    reply(status,value) { this.status=status;this.response=value;this.responseText=typeof value==='string'?value:'';this.onload(); }
}
global.XMLHttpRequest=XHR;
const transfer=require('../static/transfer.js');
beforeEach(()=>{ requests=[];responder=x=>x.reply(200,x.method==='GET'?png:'{"success":true}'); });
const options=()=>({origin:'http://table',name:'Spiral8',result:{thr:'0 0\n6.28 1\n',png:'/preview.png',thumb:'/thumb.png'},completed:new Set(),signal:new AbortController().signal,onState(){}});
test('uses correct filenames, thumbnail query, and sequential acknowledged writes',async()=>{
    const opts=options(),states=[];opts.onState=(...s)=>states.push(s);
    assert.deepEqual(await transfer.upload(opts),[]);
    const posts=requests.filter(x=>x.method==='POST');
    assert.deepEqual(posts.map(x=>x.url),['http://table/api/files/upload','http://table/api/files/upload','http://table/api/files/upload?thumbnail=1']);
    assert.deepEqual(posts.map(x=>x.body.get('file').name),['Spiral8.thr','Spiral8.png','Spiral8.png']);
    assert.deepEqual([...opts.completed],['thr','png','thumb']);
    assert.equal(requests[0].method,'GET');assert.equal(requests[1].method,'GET');
});
test('failed PNG is partial success; thumbnail waits and retry skips THR',async()=>{
    const opts=options();responder=x=>x.reply(x.method==='POST'&&x.body.get('file').name.endsWith('.png')?507:200,x.method==='GET'?png:'{"success":true}');
    const failures=await transfer.upload(opts);assert.equal(failures.length,2);
    assert.deepEqual([...opts.completed],['thr']);assert.equal(requests.filter(x=>x.method==='POST').length,2);
    // A stale thumbnail receipt must not survive a retried full image.
    opts.completed.add('thumb');requests=[];responder=x=>x.reply(200,x.method==='GET'?png:'{"success":true}');
    assert.deepEqual(await transfer.upload(opts),[]);
    assert.deepEqual(requests.filter(x=>x.method==='POST').map(x=>x.url),['http://table/api/files/upload','http://table/api/files/upload?thumbnail=1']);
});
test('preview retrieval failure cannot report complete success',async()=>{
    const opts=options();responder=x=>x.reply(x.method==='GET'?500:200,x.method==='GET'?new Blob(['Renderer failed']):'{"success":true}');
    assert.equal((await transfer.upload(opts)).length,2);assert.ok(opts.completed.has('thr'));assert.equal(requests.filter(x=>x.method==='POST').length,1);
});
test('requires positive saved acknowledgement',async()=>{
    responder=x=>x.reply(200,x.method==='GET'?png:'{}');
    const opts=options();await assert.rejects(transfer.upload(opts),/acknowledge/);assert.equal(opts.completed.size,0);
});
test('plain-text errors, invalid JSON, and explicit failure retain useful messages',async()=>{
    for(const [status,body,expected] of [[400,'Invalid THR coordinate at line 2',/line 2/],[200,'not JSON',/invalid JSON/],[200,'{"success":false,"message":"Disk full"}',/Disk full/]]) {
        responder=x=>x.reply(status,body);await assert.rejects(transfer.request('/process'),expected);
    }
});
test('timeout does not claim saved and abort removes listeners',async()=>{
    responder=x=>x.ontimeout();const controller=new AbortController();
    await assert.rejects(transfer.request('/save',{body:new FormData(),signal:controller.signal,label:'Save preview'}),/outcome is unconfirmed/);
    assert.equal(getEventListeners(controller.signal,'abort').length,0);
    responder=()=>{};const pending=transfer.request('/save',{body:new FormData(),signal:controller.signal});controller.abort();
    await assert.rejects(pending,{name:'AbortError'});assert.equal(getEventListeners(controller.signal,'abort').length,0);
    await assert.rejects(transfer.request('/save',{signal:controller.signal}),{name:'AbortError'});
});
test('names, address, sizes and non-PNG assets are validated',async()=>{
    for(const name of ['../x','a/b','a\\b','x'.repeat(77),'å']) assert.throws(()=>transfer.patternBase(name));
    for(const origin of ['','http://table/path','http://u:p@table','ftp://table']) assert.throws(()=>transfer.tableOrigin(origin));
    assert.equal(transfer.tableOrigin('table.local'),'http://table.local');
    const opts=options();opts.result.thr='';await assert.rejects(transfer.upload(opts),/nonempty/);assert.equal(requests.length,0);
    opts.result.thr='0 0';responder=x=>x.reply(200,x.method==='GET'?new Blob(['not a png']):'{"success":true}');
    assert.match((await transfer.upload(opts))[0],/not a PNG/);
});
