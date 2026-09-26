import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url), api=require('../../src/web/etl-p2p.js');
const t=api.createTransport()._transport;
function channel(opts={}) { return {label:'etl-nxac-1',ordered:true,maxRetransmits:null,maxPacketLifeTime:null,readyState:'open',bufferedAmount:0,sent:[],send(x){this.sent.push(x);},close(){this.readyState='closed';if(this.onclose)this.onclose();},...opts}; }
let p=t.ensurePeer(42,'first',1),d=channel();t.attachDataChannel(p,d);d.onopen();assert.deepEqual(t.receiveNxAC(),{peer:1,ready:true});
let b=new Uint8Array([1,2]);assert(t.sendNxAC(1,b));b[0]=9;assert.equal(d.sent[0][0],1);
d.bufferedAmount=262144;assert.equal(t.sendNxAC(1,b),false);d.bufferedAmount=0;
d.onmessage({data:new Uint8Array([3,4]).buffer});assert.deepEqual([...t.receiveNxAC().data],[3,4]);
assert.equal(t.sendNxAC(1,new Uint8Array(21920)),false);
let stale=d.onmessage;t.destroyPeer(p);p=t.ensurePeer(43,'second',1);d=channel();t.attachDataChannel(p,d);d.onopen();stale({data:new Uint8Array([7]).buffer});
assert.deepEqual(t.receiveNxAC(),{peer:1,closed:true});assert.deepEqual(t.receiveNxAC(),{peer:1,ready:true});assert.equal(t.receiveNxAC(),null);
for(let i=0;i<50;i++)d.onmessage({data:new Uint8Array(21919).buffer});
assert.equal(d.readyState,'closed');assert.deepEqual(t.receiveNxAC(),{peer:1,closed:true});assert.equal(t.receiveNxAC(),null);assert.equal(t.nxacBytes,0);
let bad=channel({ordered:false});t.attachDataChannel(p,bad);assert.equal(bad.readyState,'closed');bad=channel({label:'unknown'});t.attachDataChannel(p,bad);assert.equal(bad.readyState,'closed');
assert.equal(t.sendNxAC(1,b),false);console.log('NxAC reliable transport PASS: isolation, bounds, backpressure, stale generation, overflow close, labels');

// A normal close drains received bytes before EOF; destroy still invalidates.
p=t.ensurePeer(50,'drain',2);d=channel();t.attachDataChannel(p,d);d.onopen();t.receiveNxAC();
d.onmessage({data:new Uint8Array([8,9]).buffer});d.onclose();
assert.deepEqual([...t.receiveNxAC().data],[8,9]);assert.deepEqual(t.receiveNxAC(),{peer:2,closed:true});
p=t.ensurePeer(51,'control',3);d=channel();t.attachDataChannel(p,d);d.onopen();t.receiveNxAC();
const close=new TextEncoder().encode('nxac1 close 7');d.bufferedAmount=262144;
assert(t.sendNxAC(3,close));assert.equal(d.sent.length,1);
d.bufferedAmount=266240;assert.equal(t.sendNxAC(3,close),false);assert.equal(d.readyState,'closed');assert.deepEqual(t.receiveNxAC(),{peer:3,closed:true});
console.log('NxAC normal FIFO EOF and bounded terminal-control reserve PASS');

// Lobby-only reliable mode is pinned before any RTC channel can upgrade it.
const r=api.createTransport()._transport;
r.dispatchControl({t:'welcome',peer:99,nxacRelay:1});
r.ws={readyState:1,bufferedAmount:0,sent:[],send(x){this.sent.push(JSON.parse(x));}};
let rp=r.ensurePeer(77,'relay',1),rtc=channel();r.attachDataChannel(rp,rtc);assert.equal(rtc.readyState,'closed');
const ascii=new TextEncoder().encode('nxac1 ready');assert.equal(r.sendNxAC(1,ascii),false);
r.dispatchControl({t:'nxac',op:'ready',from:77});r.dispatchControl({t:'nxac',op:'ready',from:77});
assert.deepEqual(r.receiveNxAC(),{peer:1,ready:true});assert.equal(r.receiveNxAC(),null);
assert(r.sendNxAC(1,ascii));assert.equal(r.ws.sent[0].data,Buffer.from(ascii).toString('base64'));
r.dispatchControl({t:'nxac',op:'data',from:88,data:'QQ=='});assert.equal(r.receiveNxAC(),null);
r.dispatchControl({t:'nxac',op:'data',from:77,data:'QQ=='});
r.dispatchControl({t:'nxac',op:'close',from:77});assert.deepEqual([...r.receiveNxAC().data],[65]);assert.deepEqual(r.receiveNxAC(),{peer:1,closed:true});
r.dispatchControl({t:'nxac',op:'ready',from:77});assert.equal(r.receiveNxAC(),null);assert.equal(r.sendNxAC(1,ascii),false);
r.destroyPeer(rp);rp=r.ensurePeer(78,'new',1);r.dispatchControl({t:'nxac',op:'ready',from:78});assert.deepEqual(r.receiveNxAC(),{peer:1,ready:true});
r.ws.bufferedAmount=262144;assert.equal(r.sendNxAC(1,ascii),false);assert.equal(rp.nxacClosed,false);
assert(r.sendNxAC(1,close));r.ws.bufferedAmount=0;
r.dispatchControl({t:'nxac',op:'data',from:78,data:'AA=='});assert.deepEqual(r.receiveNxAC(),{peer:1,closed:true});assert.equal(r.nxacBytes,0);
r.destroyPeer(rp);rp=r.ensurePeer(79,'overflow',1);r.dispatchControl({t:'nxac',op:'ready',from:79});r.receiveNxAC();
for(let i=0;i<513;i++)r.dispatchControl({t:'nxac',op:'data',from:79,data:'QQ=='});
assert.deepEqual(r.receiveNxAC(),{peer:1,closed:true});assert.equal(r.nxacBytes,0);assert.equal(r.receiveNxAC(),null);
r.destroyPeer(rp);rp=r.ensurePeer(80,'socketloss',1);r.dispatchControl({t:'nxac',op:'ready',from:80});r.receiveNxAC();r.wantReconnect=false;r.onLobbyClosed();assert.deepEqual(r.receiveNxAC(),{peer:1,closed:true});
console.log('NxAC lobby mode PASS: pinning, feature negotiation, FIFO, bounds, backpressure, generation, socket loss');
