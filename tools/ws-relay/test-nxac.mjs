/* GPL-3.0-or-later. Offline transport boundary: real relay, UDP and TCP,
 * synthetic game-server status. Does not start ET or claim original-binary runtime parity. */
import assert from 'node:assert/strict';
import dgram from 'node:dgram';
import net from 'node:net';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url), WebSocket=require('ws');
const cleanup=[], results=[];
const check=(value,label)=>{assert.ok(value,label);results.push(label);};
const deadline=(promise,label)=>{let timer;return Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('Timeout: '+label)),5000);})]).finally(()=>clearTimeout(timer));};
const oob=Buffer.from([255,255,255,255]);
const packet=Buffer.from([1,0,0,0,7,0,90]);
let tcpConnections=0,statusRequests=0,udpPackets=0,relayLog='';
async function main(){
 const sockets=[];
 const tcp=net.createServer(s=>{tcpConnections++;sockets.push(s);s.setNoDelay(true);s.on('data',b=>s.write(b));s.on('error',()=>{});});
 tcp.listen(0,'127.0.0.1');await once(tcp,'listening');cleanup.push(()=>{for(const s of sockets)s.destroy();tcp.close();});
 const nport=tcp.address().port;
 const udp=dgram.createSocket('udp4');
 udp.on('message',(msg,peer)=>{
  if(msg.subarray(0,4).equals(oob)){
   const m=/^getstatus ([a-f0-9]{32})\n$/.exec(msg.subarray(4).toString('latin1'));
   if(m){statusRequests++;const response=Buffer.concat([oob,Buffer.from(`statusResponse\n\\challenge\\${m[1]}\\gamename\\nitmod\\nport\\${nport}\n`)]);udp.send(response,peer.port,peer.address);}
  }else{udpPackets++;udp.send(msg,peer.port,peer.address);}
 });
 udp.bind(0,'127.0.0.1');await once(udp,'listening');cleanup.push(()=>udp.close());
 const relay=spawn(process.execPath,[fileURLToPath(new URL('./relay.js',import.meta.url)),'--host','127.0.0.1','--port','0','--no-download-proxy'],{stdio:['ignore','pipe','pipe'],windowsHide:true});
 cleanup.push(()=>relay.kill());
 const port=await deadline(new Promise((resolve,reject)=>{
  relay.on('error',reject);relay.on('exit',c=>reject(new Error('Relay exit '+c+' '+relayLog)));
  relay.stderr.on('data',b=>{relayLog+=b;});
  relay.stdout.on('data',b=>{relayLog+=b;const match=/Listening on.*?:(\d+)/.exec(relayLog);if(match)resolve(Number(match[1]));});
 }),'relay listen');
 async function client(){
  const ws=new WebSocket(`ws://127.0.0.1:${port}/127.0.0.1:${udp.address().port}`), frames=[],waiters=[];
  cleanup.push(()=>ws.terminate());
  ws.on('message',(data,binary)=>{const frame=binary?Buffer.from(data):data.toString();const i=waiters.findIndex(w=>w.pred(frame));if(i>=0)waiters.splice(i,1)[0].resolve(frame);else frames.push(frame);});
  ws.on('error',()=>{});
  const next=(pred,label)=>{const i=frames.findIndex(pred);if(i>=0)return Promise.resolve(frames.splice(i,1)[0]);return deadline(new Promise(resolve=>waiters.push({pred,resolve})),label);};
  await deadline(once(ws,'open'),'ws open');await next(f=>f==='nxac1 ready','capability');
  return{ws,next,frames};
 }
 async function established(){const c=await client();c.ws.send(packet);const b=await c.next(Buffer.isBuffer,'game packet');check(b.equals(packet),'unchanged UDP game datagram');return c;}
 const inactive=await client();inactive.ws.send(`nxac1 open 1 ${nport}`);
 check((await inactive.next(f=>typeof f==='string','inactive refusal')).endsWith('game-session-not-ready'),'no stream before real bidirectional game traffic');check(tcpConnections===0,'no TCP connect for inactive game');inactive.ws.close();
 const c=await established();c.ws.send(`nxac1 open 2 ${nport}`);
 const opened=await c.next(f=>typeof f==='string'&&f.startsWith('nxac1 opened 2 '),'TCP open');
 const actualPort=Number(opened.split(' ')[3]);
 check(sockets.length===1&&sockets[0].remotePort===actualPort,'cnport is actual TCP peer source port');check(statusRequests===1,'nport verified by fresh challenged UDP status');
 const bytes=Buffer.alloc(16384);for(let i=0;i<bytes.length;i++)bytes[i]=i&255;
 const protocol=Buffer.from(`hb ${actualPort}\nift \\fs\\16384\\ext\\jpg\\csm\\00000000000000000000000000000000\\\n`);
 for(const payload of [protocol,bytes,Buffer.from('fl')]){
  c.ws.send(`nxac1 data 2 ${payload.toString('base64')}`);
  let received=Buffer.alloc(0);while(received.length<payload.length){const frame=await c.next(f=>typeof f==='string'&&f.startsWith('nxac1 data 2 '),'TCP bytes');received=Buffer.concat([received,Buffer.from(frame.split(' ')[3],'base64')]);}
  check(received.equals(payload),'byte-identical original transport payload '+payload.length);
 }
 c.ws.send(packet);check((await c.next(Buffer.isBuffer,'UDP alongside TCP')).equals(packet),'game packets remain independent of NxAC controls');
 const closed=once(sockets[0],'close');c.ws.send('nxac1 close 2');await deadline(closed,'explicit TCP close');check(sockets[0].destroyed,'explicit close destroys real TCP stream');c.ws.close();
 const wrong=await established();wrong.ws.send(`nxac1 open 3 ${nport===65535?65534:nport+1}`);
 check((await wrong.next(f=>typeof f==='string','wrong port refusal')).endsWith('unadvertised-port'),'unadvertised TCP port denied');check(tcpConnections===1,'wrong port made no TCP connection');wrong.ws.close();
 const malformed=await established();malformed.ws.send(`nxac1 open 4 ${nport}`);await malformed.next(f=>typeof f==='string'&&f.startsWith('nxac1 opened 4 '),'second open');
 const closed2=once(sockets[1],'close');malformed.ws.send('nxac1 data 4 AB==');
 check((await malformed.next(f=>typeof f==='string','noncanonical base64')).includes('error 4'),'noncanonical data refused');await deadline(closed2,'malformed stream cleanup');check(sockets[1].destroyed,'invalid payload closes stream');malformed.ws.close();
 const reconnect=await established();reconnect.ws.send(`nxac1 open 5 ${nport}`);await reconnect.next(f=>typeof f==='string'&&f.startsWith('nxac1 opened 5 '),'third open');
 const closed3=once(sockets[2],'close');reconnect.ws.terminate();await deadline(closed3,'disconnect cleanup');check(sockets[2].destroyed,'game WebSocket disconnect closes TCP');
 check(udpPackets===5,'NxAC text frames never leak into game UDP');
 console.log(JSON.stringify({pass:true,checks:results.length,results,scope:'Actual relay TCP/UDP/WebSocket, synthetic ET server boundary, no live game'},null,2));
}
try{await main();}catch(e){console.error(e.stack,relayLog);process.exitCode=1;}finally{for(const fn of cleanup.reverse()){try{fn();}catch(_){}}}
