#pragma once
#include <pgmspace.h>

/**
 * portal_assets.h — Self-contained single-page app for the EspScreen config portal.
 *
 * NO external / CDN requests — captive clients have no internet.
 * Inline CSS + vanilla JS only.
 *
 * Sections:
 *   1. WiFi — scan, select SSID, enter password, submit → POST /api/wifi
 *   2. Claude Auth — label + token fields → POST /api/claude/tokens
 *   3. Status — polls GET /api/status every 5 s
 */

const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EspScreen Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;min-height:100vh;padding:16px}
h1{font-size:1.4rem;font-weight:700;color:#38bdf8;margin-bottom:4px}
.sub{font-size:.8rem;color:#94a3b8;margin-bottom:20px}
.card{background:#1e293b;border-radius:10px;padding:16px;margin-bottom:16px}
.card h2{font-size:1rem;font-weight:600;color:#cbd5e1;margin-bottom:12px;display:flex;align-items:center;gap:6px}
label{display:block;font-size:.8rem;color:#94a3b8;margin-bottom:4px;margin-top:10px}
input,select{width:100%;padding:8px 10px;background:#0f172a;border:1px solid #334155;border-radius:6px;color:#e2e8f0;font-size:.9rem}
input:focus,select:focus{outline:none;border-color:#38bdf8}
button{margin-top:12px;padding:9px 18px;border:none;border-radius:6px;font-size:.85rem;font-weight:600;cursor:pointer;transition:opacity .15s}
.btn-primary{background:#0ea5e9;color:#fff}
.btn-secondary{background:#334155;color:#e2e8f0}
.btn-primary:hover,.btn-secondary:hover{opacity:.85}
.ssid-list{max-height:180px;overflow-y:auto;border:1px solid #334155;border-radius:6px;margin-top:8px}
.ssid-item{padding:8px 10px;cursor:pointer;border-bottom:1px solid #1e293b;display:flex;justify-content:space-between;align-items:center;font-size:.85rem}
.ssid-item:last-child{border-bottom:none}
.ssid-item:hover,.ssid-item.selected{background:#1e4a6e}
.rssi{font-size:.75rem;color:#64748b}
.enc-badge{font-size:.7rem;padding:2px 5px;border-radius:4px;background:#334155;color:#94a3b8}
.msg{margin-top:10px;padding:8px 10px;border-radius:6px;font-size:.82rem}
.msg.ok{background:#064e3b;color:#6ee7b7}
.msg.err{background:#450a0a;color:#fca5a5}
.msg.info{background:#1e3a5f;color:#93c5fd}
.kv{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #1e293b;font-size:.82rem}
.kv:last-child{border-bottom:none}
.kv-k{color:#64748b}
.kv-v{color:#e2e8f0;font-weight:500;text-align:right;max-width:60%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.badge-ok{color:#6ee7b7}.badge-warn{color:#fcd34d}.badge-err{color:#fca5a5}
</style>
</head>
<body>
<h1>EspScreen Setup</h1>
<p class="sub">Configure WiFi and Claude authentication</p>

<!-- ── WIFI CARD ─────────────────────────────────────────────────── -->
<div class="card" id="wifi-card">
  <h2>&#x1F4F6; WiFi</h2>
  <button class="btn-secondary" id="scan-btn" onclick="doScan()">Scan for networks</button>
  <div id="ssid-list-wrap" style="display:none">
    <div class="ssid-list" id="ssid-list"></div>
  </div>
  <label for="ssid-input">SSID</label>
  <input id="ssid-input" type="text" placeholder="Network name" autocomplete="off">
  <label for="pass-input">Password</label>
  <input id="pass-input" type="password" placeholder="WiFi password" autocomplete="off">
  <button class="btn-primary" onclick="doWifi()">Connect</button>
  <div id="wifi-msg" class="msg info" style="display:none"></div>
</div>

<!-- ── CLAUDE AUTH CARD ───────────────────────────────────────────── -->
<div class="card" id="claude-card">
  <h2>&#x1F511; Claude Auth</h2>
  <label for="c-label">Profile label</label>
  <input id="c-label" type="text" placeholder="e.g. Default" value="Default">
  <label for="c-access">Access token</label>
  <input id="c-access" type="password" placeholder="sk-ant-oat01-...">
  <label for="c-refresh">Refresh token</label>
  <input id="c-refresh" type="password" placeholder="sk-ant-ort01-...">
  <label for="c-expires">Expires at (Unix seconds, 0 = unknown)</label>
  <input id="c-expires" type="number" placeholder="0" value="0">
  <button class="btn-primary" onclick="doTokens()">Save tokens</button>
  <div id="claude-msg" class="msg info" style="display:none"></div>
</div>

<!-- ── STATUS CARD ────────────────────────────────────────────────── -->
<div class="card" id="status-card">
  <h2>&#x1F4CA; Status <span style="font-size:.7rem;font-weight:400;color:#475569">(auto-refresh 5s)</span></h2>
  <div id="status-body"><p style="color:#475569;font-size:.82rem">Loading…</p></div>
</div>

<script>
var selSsid = null;

function showMsg(id, text, type) {
  var el = document.getElementById(id);
  el.textContent = text;
  el.className = 'msg ' + type;
  el.style.display = 'block';
}

function doScan() {
  var btn = document.getElementById('scan-btn');
  btn.textContent = 'Scanning…';
  btn.disabled = true;
  fetch('/api/scan').then(function(r){ return r.json(); }).then(function(nets) {
    var list = document.getElementById('ssid-list');
    list.innerHTML = '';
    if (!nets || nets.length === 0) {
      list.innerHTML = '<div class="ssid-item">No networks found</div>';
    } else {
      nets.forEach(function(n) {
        var d = document.createElement('div');
        d.className = 'ssid-item';
        var enc = n.enc ? '<span class="enc-badge">&#x1F512;</span>' : '';
        d.innerHTML = '<span>' + escHtml(n.ssid) + ' ' + enc + '</span>' +
                      '<span class="rssi">' + n.rssi + ' dBm</span>';
        d.onclick = function() {
          selSsid = n.ssid;
          list.querySelectorAll('.ssid-item').forEach(function(i){ i.classList.remove('selected'); });
          d.classList.add('selected');
          document.getElementById('ssid-input').value = n.ssid;
          document.getElementById('pass-input').focus();
        };
        list.appendChild(d);
      });
    }
    document.getElementById('ssid-list-wrap').style.display = 'block';
  }).catch(function(e) {
    showMsg('wifi-msg', 'Scan failed: ' + e.message, 'err');
  }).finally(function() {
    btn.textContent = 'Scan again';
    btn.disabled = false;
  });
}

function doWifi() {
  var ssid = document.getElementById('ssid-input').value.trim();
  var pass = document.getElementById('pass-input').value;
  if (!ssid) { showMsg('wifi-msg', 'Enter an SSID', 'err'); return; }
  showMsg('wifi-msg', 'Connecting…', 'info');
  fetch('/api/wifi', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ssid: ssid, pass: pass})
  }).then(function(r){ return r.json(); }).then(function(j) {
    if (j.ok) {
      showMsg('wifi-msg', 'Connected! IP: ' + (j.ip || '?'), 'ok');
    } else {
      showMsg('wifi-msg', 'Failed: ' + (j.error || 'unknown'), 'err');
    }
  }).catch(function(e) {
    showMsg('wifi-msg', 'Request failed: ' + e.message, 'err');
  });
}

function doTokens() {
  var label   = document.getElementById('c-label').value.trim();
  var access  = document.getElementById('c-access').value.trim();
  var refresh = document.getElementById('c-refresh').value.trim();
  var expires = parseInt(document.getElementById('c-expires').value, 10) || 0;
  if (!label)   { showMsg('claude-msg', 'Enter a profile label', 'err'); return; }
  if (!access)  { showMsg('claude-msg', 'Enter an access token', 'err'); return; }
  if (!refresh) { showMsg('claude-msg', 'Enter a refresh token', 'err'); return; }
  showMsg('claude-msg', 'Saving…', 'info');
  fetch('/api/claude/tokens', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({label: label, access: access, refresh: refresh, expires_at: expires})
  }).then(function(r){ return r.json(); }).then(function(j) {
    if (j.ok) {
      showMsg('claude-msg', 'Tokens saved for profile "' + label + '"', 'ok');
    } else {
      showMsg('claude-msg', 'Error: ' + (j.error || 'unknown'), 'err');
    }
  }).catch(function(e) {
    showMsg('claude-msg', 'Request failed: ' + e.message, 'err');
  });
}

function kv(k, v, cls) {
  return '<div class="kv"><span class="kv-k">' + k + '</span>' +
         '<span class="kv-v' + (cls ? ' ' + cls : '') + '">' + v + '</span></div>';
}

function escHtml(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function refreshStatus() {
  fetch('/api/status').then(function(r){ return r.json(); }).then(function(j) {
    var html = '';
    html += kv('Device', escHtml(j.device || 'EspScreen'));
    html += kv('Version', escHtml(j.version || '?'));
    html += kv('Uptime', (j.uptime_s || 0) + ' s');

    var wconn = j.wifi && j.wifi.connected;
    html += kv('WiFi', wconn ? ('Connected (' + escHtml(j.wifi.ssid || '') + ')') : 'Disconnected',
               wconn ? 'badge-ok' : 'badge-warn');
    if (wconn) {
      html += kv('IP', escHtml(j.wifi.ip || ''));
      html += kv('RSSI', (j.wifi.rssi || 0) + ' dBm');
    }

    html += kv('Net mode', escHtml(j.net_mode || '?'));
    html += kv('Portal IP', escHtml(j.portal_ip || '?'));

    if (j.claude) {
      html += kv('Claude profiles', j.claude.profile_count || 0);
      if (j.claude.profile_count > 0) {
        html += kv('Active profile', escHtml(j.claude.active_label || ''));
        var exp = j.claude.token_expired;
        html += kv('Token', exp ? 'EXPIRED' : 'valid', exp ? 'badge-err' : 'badge-ok');
        if (j.claude.expires_in_s !== undefined) {
          var s = j.claude.expires_in_s;
          html += kv('Expires in', s < 0 ? 'past' : (Math.floor(s/60) + ' min'), s < 0 ? 'badge-err' : '');
        }
      }
    }

    if (j.last_retry_failed) {
      html += kv('Last connect attempt', 'FAILED', 'badge-err');
    }

    document.getElementById('status-body').innerHTML = html || '<p style="color:#475569">No data</p>';
  }).catch(function(e) {
    document.getElementById('status-body').innerHTML =
      '<p style="color:#fca5a5;font-size:.82rem">Status fetch failed: ' + escHtml(e.message) + '</p>';
  });
}

refreshStatus();
setInterval(refreshStatus, 5000);
</script>
</body>
</html>)HTML";

static_assert(sizeof(PORTAL_HTML) < 16000, "portal HTML too large for no-PSRAM device");
