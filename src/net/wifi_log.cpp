#include "wifi_log.h"
#include <WiFi.h>
#include <WebServer.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ── Ring buffer ──────────────────────────────────────────────────────────────

static constexpr int LINE_LEN = 128;

static char     _buf[WIFI_LOG_LINES][LINE_LEN];
static uint32_t _total = 0;   // monotonically increasing; never wraps in practice

// ── HTTP server ──────────────────────────────────────────────────────────────

static WebServer _srv(80);

// Served once at GET /
static const char PAGE[] = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DuettGUI</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { background: #0d0d0d; color: #c8ffc8; font: 13px/1.5 'Courier New', monospace;
       display: flex; flex-direction: column; height: 100dvh; }
header { background: #111; border-bottom: 1px solid #1a3a1a; padding: 6px 12px;
         display: flex; align-items: center; gap: 12px; flex-shrink: 0; }
h1 { color: #4fc; font-size: 15px; font-weight: bold; }
.dot { width: 8px; height: 8px; border-radius: 50%; background: #0f0;
       box-shadow: 0 0 6px #0f0; flex-shrink: 0; }
.dot.off { background: #333; box-shadow: none; }
#status { color: #666; font-size: 12px; margin-left: auto; }
#log { flex: 1; overflow-y: auto; padding: 8px 12px; }
.line { padding: 1px 0; border-bottom: 1px solid #111; white-space: pre-wrap; word-break: break-all; }
.ts { color: #3a3; margin-right: 6px; }
footer { background: #111; border-top: 1px solid #1a3a1a; padding: 4px 12px;
         display: flex; align-items: center; gap: 8px; flex-shrink: 0; }
button { background: #1a2a1a; color: #8f8; border: 1px solid #2a4a2a; border-radius: 3px;
         padding: 3px 10px; cursor: pointer; font: inherit; }
button:hover { background: #2a3a2a; }
#count { color: #444; font-size: 11px; margin-left: auto; }
</style>
</head>
<body>
<header>
  <div class="dot off" id="dot"></div>
  <h1>&#x1F697; DuettGUI Console</h1>
  <span id="status">connecting…</span>
</header>
<div id="log"></div>
<footer>
  <button onclick="clearDisplay()">Clear display</button>
  <label style="color:#666;font-size:12px">
    <input type="checkbox" id="pause"> Pause
  </label>
  <span id="count"></span>
</footer>
<script>
var since = 0;
var errCount = 0;

function clearDisplay() {
  document.getElementById('log').innerHTML = '';
}

function appendLines(lines) {
  var el = document.getElementById('log');
  var atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 40;
  lines.forEach(function(line) {
    var div = document.createElement('div');
    div.className = 'line';
    // split off leading "[NNN.Ns] " timestamp if present
    var m = line.match(/^(\[\d+\.\d+s\] )(.*)/s);
    if (m) {
      var ts = document.createElement('span');
      ts.className = 'ts';
      ts.textContent = m[1];
      div.appendChild(ts);
      div.appendChild(document.createTextNode(m[2]));
    } else {
      div.textContent = line;
    }
    el.appendChild(div);
  });
  if (atBottom) el.scrollTop = el.scrollHeight;
  document.getElementById('count').textContent = since + ' lines total';
}

function poll() {
  if (document.getElementById('pause').checked) return;
  fetch('/log?since=' + since)
    .then(function(r) { return r.json(); })
    .then(function(d) {
      errCount = 0;
      document.getElementById('dot').className = 'dot';
      document.getElementById('status').textContent = 'connected · 192.168.4.1';
      if (d.lines && d.lines.length > 0) {
        appendLines(d.lines);
      }
      since = d.total;
    })
    .catch(function() {
      errCount++;
      document.getElementById('dot').className = 'dot off';
      document.getElementById('status').textContent = 'retrying… (' + errCount + ')';
    });
}

poll();
setInterval(poll, 1000);
</script>
</body>
</html>
)html";

static void handleRoot()
{
    _srv.send(200, "text/html", PAGE);
}

static void handleLog()
{
    uint32_t since = 0;
    if (_srv.hasArg("since"))
        since = (uint32_t)_srv.arg("since").toInt();

    // Clamp to the available window so we never send garbage slots.
    uint32_t oldest = (_total > WIFI_LOG_LINES) ? (_total - WIFI_LOG_LINES) : 0;
    if (since < oldest) since = oldest;

    // Build JSON: {"total":N,"lines":["…","…"]}
    // Each line is at most LINE_LEN chars + 4 (quotes, comma, two escape chars worst-case * LINE_LEN).
    // Use a pre-allocated String to avoid repeated heap allocs in the response path.
    String json;
    json.reserve(32 + (since < _total ? (_total - since) : 0) * (LINE_LEN + 4));

    json  = "{\"total\":";
    json += _total;
    json += ",\"lines\":[";

    bool first = true;
    for (uint32_t i = since; i < _total; i++) {
        if (!first) json += ',';
        first = false;
        json += '"';
        for (const char* p = _buf[i % WIFI_LOG_LINES]; *p; p++) {
            if (*p == '"' || *p == '\\') json += '\\';
            if (*p == '\n') { json += "\\n"; continue; }
            if (*p == '\r') continue;
            json += *p;
        }
        json += '"';
    }
    json += "]}";

    _srv.send(200, "application/json", json);
}

// ── Public API ───────────────────────────────────────────────────────────────

void wifi_log_init()
{
    WiFi.softAP(WIFI_LOG_SSID, WIFI_LOG_PASS);

    _srv.on("/",    HTTP_GET, handleRoot);
    _srv.on("/log", HTTP_GET, handleLog);
    _srv.begin();

    wlog("AP \"%s\"  IP %s  port 80",
         WIFI_LOG_SSID, WiFi.softAPIP().toString().c_str());
}

void wifi_log_update()
{
    _srv.handleClient();
}

void wlog(const char* fmt, ...)
{
    // Prepend a seconds timestamp so log lines are self-contained.
    char* dst = _buf[_total % WIFI_LOG_LINES];
    uint32_t ms = millis();
    int used = snprintf(dst, LINE_LEN, "[%lu.%01lus] ", ms / 1000, (ms % 1000) / 100);
    if (used < 0 || used >= LINE_LEN) used = 0;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst + used, LINE_LEN - used, fmt, ap);
    va_end(ap);

    _total++;
    Serial.println(dst);
}
