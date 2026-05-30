#pragma once
#include <pgmspace.h>

/**
 * portal_assets.h — Self-contained single-page app for the EspScreen config portal.
 *
 * NO external / CDN requests — captive clients have no internet.
 * Inline CSS + vanilla JS only.
 *
 * Tabs: Mirror | WiFi | Claude | Status | Settings
 *   Mirror  — GET /api/screen, POST /api/touch, GET|POST /api/mirror/config
 *   WiFi    — GET /api/scan, POST /api/wifi {ssid,pass}
 *   Claude  — POST /api/claude/tokens {label,access,refresh,expires_at}
 *   Status  — GET /api/status (polled every 5s)
 */

const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EspScreen</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;min-height:100vh;padding-bottom:20px}
#tb{position:sticky;top:0;z-index:100;background:#0a1120;border-bottom:1px solid #1e293b;padding:0 14px;display:flex;align-items:center;justify-content:space-between;height:46px}
#tb .lg{font-size:.95rem;font-weight:700;color:#38bdf8}
#ss{display:flex;align-items:center;gap:8px;font-size:.74rem;color:#94a3b8}
.sd{width:8px;height:8px;border-radius:50%;display:inline-block;background:#475569}
.sd.ok{background:#22c55e}.sd.wn{background:#f59e0b}
#tn{display:flex;gap:1px;background:#0a1120;border-bottom:1px solid #1e293b;padding:0 14px;overflow-x:auto}
.tb{padding:10px 12px;font-size:.8rem;font-weight:600;color:#64748b;cursor:pointer;border-bottom:2px solid transparent;white-space:nowrap;user-select:none}
.tb:hover{color:#cbd5e1}.tb.a{color:#38bdf8;border-bottom-color:#38bdf8}
.pn{display:none;padding:14px}.pn.a{display:block}
.cd{background:#1e293b;border-radius:10px;padding:14px;margin-bottom:12px}
.cd h2{font-size:.92rem;font-weight:600;color:#cbd5e1;margin-bottom:10px}
label{display:block;font-size:.76rem;color:#94a3b8;margin-bottom:3px;margin-top:9px}
input,select{width:100%;padding:7px 10px;background:#0f172a;border:1px solid #334155;border-radius:6px;color:#e2e8f0;font-size:.86rem}
input:focus,select:focus{outline:none;border-color:#38bdf8}
button{margin-top:9px;padding:8px 15px;border:none;border-radius:6px;font-size:.8rem;font-weight:600;cursor:pointer}
.bp{background:#0ea5e9;color:#fff}.bs{background:#334155;color:#e2e8f0}
.ssid-list{max-height:160px;overflow-y:auto;border:1px solid #334155;border-radius:6px;margin-top:8px}
.si{padding:7px 10px;cursor:pointer;border-bottom:1px solid #1e293b;display:flex;justify-content:space-between;align-items:center;font-size:.82rem}
.si:last-child{border-bottom:none}.si:hover,.si.sel{background:#1e4a6e}
.ri{font-size:.72rem;color:#64748b}
.eb{font-size:.67rem;padding:2px 5px;border-radius:4px;background:#334155;color:#94a3b8}
.mg{margin-top:9px;padding:7px 10px;border-radius:6px;font-size:.79rem}
.mg.ok{background:#064e3b;color:#6ee7b7}.mg.er{background:#450a0a;color:#fca5a5}.mg.in{background:#1e3a5f;color:#93c5fd}
.kv{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #1e293b;font-size:.79rem}
.kv:last-child{border-bottom:none}
.kk{color:#64748b}.kv_{color:#e2e8f0;font-weight:500;text-align:right;max-width:60%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.bok{color:#22c55e}.bwn{color:#fcd34d}.ber{color:#fca5a5}
.tgw{display:flex;align-items:center;gap:8px;font-size:.8rem;color:#94a3b8;margin-top:12px}
.tg{position:relative;width:38px;height:21px;flex-shrink:0}
.tg input{opacity:0;width:0;height:0}
.ts{position:absolute;inset:0;background:#334155;border-radius:11px;cursor:pointer;transition:background .2s}
.ts:before{content:'';position:absolute;width:15px;height:15px;left:3px;top:3px;background:#94a3b8;border-radius:50%;transition:transform .2s}
.tg input:checked+.ts{background:#0ea5e9}
.tg input:checked+.ts:before{transform:translateX(17px);background:#fff}
.sr{margin-top:10px}
.sr .sl{display:flex;justify-content:space-between;font-size:.76rem;color:#94a3b8;margin-bottom:4px}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:4px;background:#334155;border-radius:2px;outline:none;padding:0}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:15px;height:15px;border-radius:50%;background:#0ea5e9;cursor:pointer}
input[type=range]::-moz-range-thumb{width:15px;height:15px;border-radius:50%;background:#0ea5e9;cursor:pointer;border:none}
#mw{position:relative;margin:0 auto 10px;width:240px;line-height:0;cursor:crosshair}
#sc{display:block;width:240px;height:360px;image-rendering:pixelated;image-rendering:crisp-edges;background:#1a1a2e;border-radius:6px;border:2px solid #334155}
#ph{width:240px;height:360px;display:flex;align-items:center;justify-content:center;background:#1a1a2e;border-radius:6px;border:2px solid #334155;color:#475569;font-size:.8rem;text-align:center;padding:14px;line-height:1.5}
.rp{position:absolute;border-radius:50%;width:22px;height:22px;margin:-11px 0 0 -11px;background:rgba(56,189,248,.5);pointer-events:none;animation:rpl .4s ease-out forwards}
@keyframes rpl{to{transform:scale(2.4);opacity:0}}
</style>
</head>
<body>
<div id="tb"><span class="lg">EspScreen</span><div id="ss"><span id="sd" class="sd"></span><span id="si2">&#x2014;</span><span id="sc2" style="color:#64748b">Claude &#x2014;</span></div></div>
<div id="tn">
<div class="tb a" data-t="mirror">Mirror</div>
<div class="tb" data-t="wifi">WiFi</div>
<div class="tb" data-t="claude">Claude</div>
<div class="tb" data-t="status">Status</div>
<div class="tb" data-t="settings">Settings</div>
</div>

<div class="pn a" id="pn-mirror">
<div class="cd">
<h2>&#x1F5A5; Screen Mirror</h2>
<div id="mw"><img id="sc" alt=""><div id="ph" style="display:none">Mirror disabled<br><small style="color:#334155">Enable below</small></div></div>
<div id="m4" class="mg er" style="display:none">Set/enter device secret (401)</div>
<div class="tgw"><label class="tg" style="margin:0"><input type="checkbox" id="me"><span class="ts"></span></label><span>Enable mirror</span></div>
<div class="sr"><div class="sl"><span>Update rate</span><span id="fv">2 fps</span></div><input type="range" id="mf" min="1" max="10" value="2"></div>
<div class="sr"><div class="sl"><span>Resolution</span><span id="rv">64x96</span></div><input type="range" id="mr" min="0" max="3" value="2"></div>
<div id="mm" class="mg in" style="display:none"></div>
</div>
</div>

<div class="pn" id="pn-wifi">
<div class="cd">
<h2>&#x1F4F6; WiFi</h2>
<button class="bs" id="scan-btn" onclick="doScan()">Scan for networks</button>
<div id="ssid-list-wrap" style="display:none"><div class="ssid-list" id="ssid-list"></div></div>
<label for="ssid-input">SSID</label>
<input id="ssid-input" type="text" placeholder="Network name" autocomplete="off">
<label for="pass-input">Password</label>
<input id="pass-input" type="password" placeholder="WiFi password" autocomplete="off">
<button class="bp" onclick="doWifi()">Connect</button>
<div id="wifi-msg" class="mg in" style="display:none"></div>
</div>
</div>

<div class="pn" id="pn-claude">
<div class="cd">
<h2>&#x1F511; Claude Auth</h2>
<label for="c-label">Profile label</label>
<input id="c-label" type="text" placeholder="e.g. Default" value="Default">
<label for="c-access">Access token</label>
<input id="c-access" type="password" placeholder="sk-ant-oat01-&#x2026;">
<label for="c-refresh">Refresh token</label>
<input id="c-refresh" type="password" placeholder="sk-ant-ort01-&#x2026;">
<label for="c-expires">Expires at (Unix seconds, 0=unknown)</label>
<input id="c-expires" type="number" placeholder="0" value="0">
<button class="bp" onclick="doTokens()">Save tokens</button>
<div id="claude-msg" class="mg in" style="display:none"></div>
</div>
</div>

<div class="pn" id="pn-status">
<div class="cd">
<h2>&#x1F4CA; Status <span style="font-size:.7rem;font-weight:400;color:#475569">(auto-refresh 5s)</span></h2>
<div id="status-body"><p style="color:#475569;font-size:.79rem">Loading&#x2026;</p></div>
</div>
</div>

<div class="pn" id="pn-settings">
<div class="cd"><h2>&#x2699; Settings</h2>
<label>Device secret (for write actions in STA mode)</label>
<input id="sec" type="password" placeholder="leave blank if none">
<button class="bs" onclick="saveSec()">Save secret</button>
<div id="secMsg" class="muted" style="font-size:.76rem;color:#64748b;margin-top:5px;min-height:1em"></div>
</div>
<div class="cd">
<h2>&#x1F512; Set device passcode</h2>
<p style="font-size:.76rem;color:#94a3b8;margin-bottom:6px">In AP/setup mode you can set the passcode freely. In STA mode you must first enter the <em>current</em> passcode in the &ldquo;Device secret&rdquo; field above so the request is authorised.</p>
<label>New passcode</label>
<input id="newsec" type="password" placeholder="new passcode (blank = clear)">
<button class="bp" onclick="setDeviceSec()">Set on device</button>
<div id="newsecMsg" class="muted" style="font-size:.76rem;color:#64748b;margin-top:5px;min-height:1em"></div>
</div>
</div>

<script>
function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}
function smg(id,t,c){var e=document.getElementById(id);e.textContent=t;e.className='mg '+c;e.style.display='block';}
function kv(k,v,c){return '<div class="kv"><span class="kk">'+k+'</span><span class="kv_'+(c?' '+c:'')+'">'+v+'</span></div>';}
function hdr(){var h={'Content-Type':'application/json'};var s=localStorage.getItem('espSec');if(s)h['X-EspScreen-Secret']=s;return h;}
function loadSec(){var s=localStorage.getItem('espSec');if(s)document.getElementById('sec').value=s;}
function saveSec(){localStorage.setItem('espSec',document.getElementById('sec').value);document.getElementById('secMsg').textContent='Saved';}
function setDeviceSec(){
var v=document.getElementById('newsec').value;
fetch('/api/security/secret',{method:'POST',headers:hdr(),body:JSON.stringify({secret:v})})
.then(function(r){
if(r.status===401){document.getElementById('newsecMsg').textContent='Unauthorized — enter the CURRENT passcode in the "Device secret" field above first.';return;}
if(!r.ok){document.getElementById('newsecMsg').textContent='Failed ('+r.status+')';return;}
localStorage.setItem('espSec',v);
document.getElementById('sec').value=v;
document.getElementById('newsecMsg').textContent=v?'Passcode set ✓':'Passcode cleared ✓';
}).catch(function(){document.getElementById('newsecMsg').textContent='Network error';});
}
var AT='mirror';
document.querySelectorAll('.tb').forEach(function(t){
t.addEventListener('click',function(){
var id=t.getAttribute('data-t');
document.querySelectorAll('.tb').forEach(function(x){x.classList.remove('a');});
document.querySelectorAll('.pn').forEach(function(x){x.classList.remove('a');});
t.classList.add('a');document.getElementById('pn-'+id).classList.add('a');
var p=AT;AT=id;
if(id==='mirror'&&p!=='mirror')mStart();
if(p==='mirror'&&id!=='mirror')mStop();
});
});
var selSsid=null;
function doScan(){
var b=document.getElementById('scan-btn');
b.textContent='Scanning…';b.disabled=true;
fetch('/api/scan').then(function(r){return r.json();}).then(function(ns){
var l=document.getElementById('ssid-list');l.innerHTML='';
if(!ns||!ns.length){l.innerHTML='<div class="si">No networks found</div>';}
else{ns.forEach(function(n){
var d=document.createElement('div');d.className='si';
d.innerHTML='<span>'+esc(n.ssid)+(n.enc?' <span class="eb">&#x1F512;</span>':'')+'</span><span class="ri">'+n.rssi+' dBm</span>';
d.onclick=function(){selSsid=n.ssid;l.querySelectorAll('.si').forEach(function(i){i.classList.remove('sel');});d.classList.add('sel');document.getElementById('ssid-input').value=n.ssid;document.getElementById('pass-input').focus();};
l.appendChild(d);
});}
document.getElementById('ssid-list-wrap').style.display='block';
}).catch(function(e){smg('wifi-msg','Scan failed: '+e.message,'er');
}).finally(function(){b.textContent='Scan again';b.disabled=false;});
}
function doWifi(){
var s=document.getElementById('ssid-input').value.trim();
var p=document.getElementById('pass-input').value;
if(!s){smg('wifi-msg','Enter an SSID','er');return;}
smg('wifi-msg','Connecting…','in');
fetch('/api/wifi',{method:'POST',headers:hdr(),body:JSON.stringify({ssid:s,pass:p})
}).then(function(r){
if(r.status===401){smg('wifi-msg','Unauthorized — set the Device secret in the Settings tab.','er');return null;}
return r.json();
}).then(function(j){
if(!j)return;
if(j.ok){smg('wifi-msg','Connected! IP: '+(j.ip||'?'),'ok');}
else{smg('wifi-msg','Failed: '+(j.error||'unknown'),'er');}
}).catch(function(e){smg('wifi-msg','Request failed: '+e.message,'er');});
}
function doTokens(){
var lb=document.getElementById('c-label').value.trim();
var ac=document.getElementById('c-access').value.trim();
var rf=document.getElementById('c-refresh').value.trim();
var ex=parseInt(document.getElementById('c-expires').value,10)||0;
if(!lb){smg('claude-msg','Enter a profile label','er');return;}
if(!ac){smg('claude-msg','Enter an access token','er');return;}
if(!rf){smg('claude-msg','Enter a refresh token','er');return;}
smg('claude-msg','Saving…','in');
fetch('/api/claude/tokens',{method:'POST',headers:hdr(),body:JSON.stringify({label:lb,access:ac,refresh:rf,expires_at:ex})
}).then(function(r){
if(r.status===401){smg('claude-msg','Unauthorized — set the Device secret in the Settings tab.','er');return null;}
return r.json();
}).then(function(j){
if(!j)return;
if(j.ok){smg('claude-msg','Tokens saved for profile "'+lb+'"','ok');}
else{smg('claude-msg','Error: '+(j.error||'unknown'),'er');}
}).catch(function(e){smg('claude-msg','Request failed: '+e.message,'er');});
}
function rStat(){
fetch('/api/status').then(function(r){return r.json();}).then(function(j){
var h='';
h+=kv('Device',esc(j.device||'EspScreen'));
h+=kv('Version',esc(j.version||'?'));
h+=kv('Uptime',(j.uptime_s||0)+' s');
var wc=j.wifi&&j.wifi.connected;
h+=kv('WiFi',wc?'Connected ('+esc(j.wifi.ssid||'')+')'  :'Disconnected',wc?'bok':'bwn');
if(wc){h+=kv('IP',esc(j.wifi.ip||''));h+=kv('RSSI',(j.wifi.rssi||0)+' dBm');}
h+=kv('Net mode',esc(j.net_mode||'?'));
h+=kv('Portal IP',esc(j.portal_ip||'?'));
if(j.claude){
h+=kv('Claude profiles',j.claude.profile_count||0);
if(j.claude.profile_count>0){
h+=kv('Active profile',esc(j.claude.active_label||''));
var ex=j.claude.token_expired;
h+=kv('Token',ex?'EXPIRED':'valid',ex?'ber':'bok');
if(j.claude.expires_in_s!==undefined){var s=j.claude.expires_in_s;h+=kv('Expires in',s<0?'past':Math.floor(s/60)+' min',s<0?'ber':'');}
}}
if(j.last_retry_failed){h+=kv('Last connect','FAILED','ber');}
document.getElementById('status-body').innerHTML=h||'<p style="color:#475569">No data</p>';
var dot=document.getElementById('sd');dot.className='sd '+(wc?'ok':'wn');
document.getElementById('si2').textContent=wc?(j.wifi.ip||'?'):'No WiFi';
var ce=document.getElementById('sc2');
if(j.claude&&j.claude.profile_count>0){ce.textContent='Claude '+(j.claude.token_expired?'⚠':'✓');ce.style.color=j.claude.token_expired?'#fcd34d':'#22c55e';}
else{ce.textContent='Claude —';ce.style.color='#64748b';}
}).catch(function(){});
}
rStat();setInterval(rStat,5000);
var mEn=false,mIv=500,mW=64,mH=96,mT=null,mAc=true;
var RP=[[32,48],[48,72],[64,96],[80,120]];
var mfs=document.getElementById('mf'),mrs=document.getElementById('mr'),mec=document.getElementById('me');
var scImg=document.getElementById('sc'),scPh=document.getElementById('ph');
function fLbl(v){return v+' fps';}
function rLbl(i){return RP[i][0]+'x'+RP[i][1];}
mfs.addEventListener('input',function(){document.getElementById('fv').textContent=fLbl(+mfs.value);});
mrs.addEventListener('input',function(){document.getElementById('rv').textContent=rLbl(+mrs.value);});
mfs.addEventListener('change',function(){mCfg({interval_ms:Math.round(1000/+mfs.value)});});
mrs.addEventListener('change',function(){var p=RP[+mrs.value];mCfg({out_width:p[0],out_height:p[1]});});
mec.addEventListener('change',function(){mCfg({enabled:mec.checked});});
function mCfg(patch){
fetch('/api/mirror/config',{method:'POST',headers:hdr(),body:JSON.stringify(patch)
}).then(function(r){
if(r.status===401){var e=document.getElementById('m4');e.textContent='Unauthorized — set the Device secret in the Settings tab.';e.style.display='block';return null;}
document.getElementById('m4').style.display='none';return r.json();
}).then(function(c){
if(!c)return;
mEn=!!c.enabled;mIv=c.interval_ms||500;mW=c.out_width||64;mH=c.out_height||96;
mec.checked=mEn;
var f=Math.round(1000/mIv);mfs.value=f;document.getElementById('fv').textContent=fLbl(f);
var ri=0;for(var i=0;i<RP.length;i++){if(RP[i][0]<=mW)ri=i;}
mrs.value=ri;document.getElementById('rv').textContent=rLbl(ri);
mRP();
}).catch(function(e){smg('mm','Config error: '+e.message,'er');});
}
function mLd(){
fetch('/api/mirror/config').then(function(r){return r.json();}).then(function(c){
mEn=!!c.enabled;mIv=Math.max(100,Math.min(2000,c.interval_ms||500));mW=c.out_width||64;mH=c.out_height||96;
mec.checked=mEn;
var f=Math.max(1,Math.min(10,Math.round(1000/mIv)));mfs.value=f;document.getElementById('fv').textContent=fLbl(f);
var ri=0;for(var i=0;i<RP.length;i++){if(RP[i][0]<=mW)ri=i;}
mrs.value=ri;document.getElementById('rv').textContent=rLbl(ri);
mRP();
}).catch(function(){mRP();});
}
function mRP(){
if(mT){clearInterval(mT);mT=null;}
if(mEn&&mAc){scImg.style.display='block';scPh.style.display='none';mF();mT=setInterval(mF,mIv);}
else{scImg.style.display='none';scPh.style.display='flex';}
}
function mF(){
var s='/api/screen?w='+mW+'&h='+mH+'&t='+Date.now();
var i=new Image();
i.onload=function(){scImg.src=s;scImg.style.borderColor='#334155';scImg.style.display='block';scPh.style.display='none';};
i.onerror=function(){scImg.style.display='none';scPh.style.display='flex';};
i.src=s;
}
function mStart(){mAc=true;mLd();}
function mStop(){mAc=false;if(mT){clearInterval(mT);mT=null;}}
var mWrap=document.getElementById('mw');
function mTch(cx,cy){
var r=scImg.getBoundingClientRect();
var bx=cx-r.left,by=cy-r.top,bw=r.width,bh=r.height;
var rp=document.createElement('div');rp.className='rp';
rp.style.left=Math.round(bx)+'px';rp.style.top=Math.round(by)+'px';
mWrap.appendChild(rp);setTimeout(function(){if(rp.parentNode)rp.parentNode.removeChild(rp);},450);
fetch('/api/touch',{method:'POST',headers:hdr(),
body:JSON.stringify({x:Math.round(bx),y:Math.round(by),w:Math.round(bw),h:Math.round(bh)})
}).then(function(r){
if(r.status===401){var e=document.getElementById('m4');e.textContent='Unauthorized — set the Device secret in the Settings tab.';e.style.display='block';}
else document.getElementById('m4').style.display='none';
}).catch(function(){});
}
scImg.addEventListener('click',function(e){mTch(e.clientX,e.clientY);});
scImg.addEventListener('touchstart',function(e){
e.preventDefault();
if(e.changedTouches&&e.changedTouches.length)mTch(e.changedTouches[0].clientX,e.changedTouches[0].clientY);
},{passive:false});
mLd();
loadSec();
</script>
</body>
</html>)HTML";

static_assert(sizeof(PORTAL_HTML) < 24000, "portal HTML too large for no-PSRAM device");
