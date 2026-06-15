'use strict';

const http = require('http');
const WebSocket = require('ws');
const fs = require('fs');
const path = require('path');

const PORT    = parseInt(process.argv[2]) || 3000;
const WEB_DIR = path.resolve(__dirname, '../main/web');

const MIME = {
    '.html': 'text/html; charset=utf-8',
    '.css':  'text/css',
    '.js':   'application/javascript',
};

function serveStatic(res, filename) {
    const filepath = path.join(WEB_DIR, filename);
    const ext = path.extname(filename);
    try {
        const data = fs.readFileSync(filepath);
        res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
        res.end(data);
    } catch {
        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end('File not found: ' + filename);
    }
}

// ── Simulated device state ─────────────────────────────────────────────────
const cfg = {
    mqtt_client_id:   'esp32-sinkvalve-iot',
    mqtt_uri:         'mqtt://192.168.1.100',
    mqtt_port:        1883,
    mqtt_user:        'admin',
    mqtt_pass:        'password',
    auto_off_enabled: false,
    auto_off_time:    5,
};

const gpioCfg = {
    pin_relay: 16,
    pin_led1:  18,
    pin_led2:  19,
    pin_flow:  4,
    pin_pwm:   5,
};

const miscCfg = {
    relay_toggle_ms:      100,
    flow_pulse_per_liter: 660,
    display_slave_addr:   0x70,
};

const topicCfg = {
    topic_pub: 'home/hillstate/sinkvalve/state',
    topic_sub: 'home/hillstate/sinkvalve/command',
    topic_ota: 'home/hillstate/sinkvalve/ota',
};

const wifiState = {
    connected: true,
    ssid: 'HomeNetwork',
    ip:   '192.168.1.100',
};

const mqttState = {
    connected: true,
};

const mockAPs = [
    { ssid: 'HomeNetwork',  rssi: -42, auth: 3 },
    { ssid: 'Neighbor_5G',  rssi: -65, auth: 3 },
    { ssid: 'CoffeeShop',   rssi: -71, auth: 0 },
    { ssid: 'AndroidAP_EE', rssi: -78, auth: 4 },
];

const sensor = {
    flow_active:    false,
    pulse_per_sec:  0,
    pulse_accum:    0,
    _autoOffTick:   0,
    _nextStartTick: 15,
    _tick:          0,
};

function simTick() {
    sensor._tick++;

    if (sensor.flow_active) {
        sensor.pulse_per_sec = Math.floor(65 + Math.random() * 35);
        sensor.pulse_accum  += sensor.pulse_per_sec;
        sensor._autoOffTick++;

        if (cfg.auto_off_enabled && sensor._autoOffTick >= cfg.auto_off_time) {
            sensor.flow_active   = false;
            sensor.pulse_per_sec = 0;
            sensor._autoOffTick  = 0;
            log('[SIM]', 'Auto-off triggered', 'yellow');
        }
    } else {
        sensor.pulse_per_sec = 0;
        sensor._autoOffTick  = 0;

        if (sensor._tick >= sensor._nextStartTick) {
            sensor.flow_active    = true;
            sensor._nextStartTick = sensor._tick + 12 + Math.floor(Math.random() * 18);
            log('[SIM]', 'Water flow started', 'green');
        }
    }
}

setInterval(simTick, 1000);

// ── Console helpers ────────────────────────────────────────────────────────
const COLORS = { green: '\x1b[32m', yellow: '\x1b[33m', cyan: '\x1b[36m', red: '\x1b[31m', gray: '\x1b[90m', reset: '\x1b[0m' };

function log(tag, msg, color = 'gray') {
    const now = new Date().toLocaleTimeString();
    console.log(`${COLORS[color]}${tag}${COLORS.reset} [${now}] ${msg}`);
}

// ── Build status payload ───────────────────────────────────────────────────
function statusPayload(withType = false) {
    const obj = {
        flow_active:    sensor.flow_active,
        pulse_per_sec:  sensor.pulse_per_sec,
        pulse_accum:    sensor.pulse_accum,
        flow_rate:      sensor.pulse_per_sec / 660,
        volume:         sensor.pulse_accum / 660,
        wifi_connected: wifiState.connected,
        wifi_ssid:      wifiState.ssid,
        wifi_ip:        wifiState.ip,
        mqtt_connected: mqttState.connected,
    };
    if (withType) obj.type = 'status';
    return obj;
}

// ── HTTP request body helper ───────────────────────────────────────────────
function readBody(req) {
    return new Promise((resolve, reject) => {
        let data = '';
        req.on('data', c => (data += c));
        req.on('end', () => resolve(data));
        req.on('error', reject);
    });
}

// ── HTTP server ────────────────────────────────────────────────────────────

async function route(req, res) {
    const parsed = new URL(req.url, `http://localhost`);
    const urlPath = parsed.pathname;
    const meth = req.method;

    log(`[HTTP]`, `${meth} ${urlPath}`, 'cyan');

    const sendJson = (obj, status = 200) => {
        res.writeHead(status, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(obj));
    };

    // Static files served from main/web/
    if (meth === 'GET' && urlPath === '/') {
        serveStatic(res, 'index.html');
        return;
    }
    if (meth === 'GET' && (urlPath === '/app.css' || urlPath === '/app.js')) {
        serveStatic(res, urlPath.slice(1));
        return;
    }

    // GET /api/status
    if (meth === 'GET' && urlPath === '/api/status') {
        sendJson(statusPayload());
        return;
    }

    // POST /api/relay/toggle
    if (meth === 'POST' && urlPath === '/api/relay/toggle') {
        sensor.flow_active = !sensor.flow_active;
        if (!sensor.flow_active) sensor.pulse_per_sec = 0;
        log('[RELAY]', 'Toggle → flow_active=' + sensor.flow_active, sensor.flow_active ? 'green' : 'yellow');
        sendJson({ ok: true });
        return;
    }

    // GET /api/config
    if (meth === 'GET' && urlPath === '/api/config') {
        sendJson({ ...cfg });
        return;
    }

    // POST /api/config/autooff
    if (meth === 'POST' && urlPath === '/api/config/autooff') {
        try {
            const body = JSON.parse(await readBody(req));
            if (typeof body.auto_off_enabled === 'boolean') cfg.auto_off_enabled = body.auto_off_enabled;
            if (typeof body.auto_off_time    === 'number')  cfg.auto_off_time    = body.auto_off_time;
            log('[CFG]', `auto_off: enabled=${cfg.auto_off_enabled} time=${cfg.auto_off_time}s`, 'green');
            sendJson({ ok: true });
        } catch {
            sendJson({ ok: false, error: 'Invalid JSON' }, 400);
        }
        return;
    }

    // GET /api/config/gpio
    if (meth === 'GET' && urlPath === '/api/config/gpio') {
        sendJson({ ...gpioCfg });
        return;
    }

    // POST /api/config/gpio
    if (meth === 'POST' && urlPath === '/api/config/gpio') {
        try {
            const body = JSON.parse(await readBody(req));
            const check = (v) => typeof v === 'number' && v >= 0 && v <= 39;
            if (check(body.pin_relay)) gpioCfg.pin_relay = body.pin_relay;
            if (check(body.pin_led1))  gpioCfg.pin_led1  = body.pin_led1;
            if (check(body.pin_led2))  gpioCfg.pin_led2  = body.pin_led2;
            if (check(body.pin_flow))  gpioCfg.pin_flow  = body.pin_flow;
            if (check(body.pin_pwm))   gpioCfg.pin_pwm   = body.pin_pwm;
            log('[CFG]', `GPIO saved: relay=${gpioCfg.pin_relay} led1=${gpioCfg.pin_led1} led2=${gpioCfg.pin_led2} flow=${gpioCfg.pin_flow} pwm=${gpioCfg.pin_pwm}`, 'green');
            log('[SIM]', 'Device would restart here (skipped in dev mode)', 'yellow');
            sendJson({ ok: true });
        } catch {
            sendJson({ ok: false, error: 'Invalid JSON' }, 400);
        }
        return;
    }

    // GET /api/config/misc
    if (meth === 'GET' && urlPath === '/api/config/misc') {
        sendJson({ ...miscCfg });
        return;
    }

    // POST /api/config/misc
    if (meth === 'POST' && urlPath === '/api/config/misc') {
        try {
            const body = JSON.parse(await readBody(req));
            if (typeof body.relay_toggle_ms === 'number'     && body.relay_toggle_ms >= 1)
                miscCfg.relay_toggle_ms = body.relay_toggle_ms;
            if (typeof body.flow_pulse_per_liter === 'number' && body.flow_pulse_per_liter >= 1)
                miscCfg.flow_pulse_per_liter = body.flow_pulse_per_liter;
            if (typeof body.display_slave_addr === 'number')
                miscCfg.display_slave_addr = body.display_slave_addr;
            log('[CFG]', `Misc saved: relay_ms=${miscCfg.relay_toggle_ms} ppl=${miscCfg.flow_pulse_per_liter} disp=0x${miscCfg.display_slave_addr.toString(16)}`, 'green');
            sendJson({ ok: true });
        } catch {
            sendJson({ ok: false, error: 'Invalid JSON' }, 400);
        }
        return;
    }

    // GET /api/config/topic
    if (meth === 'GET' && urlPath === '/api/config/topic') {
        sendJson({ ...topicCfg });
        return;
    }

    // POST /api/config/topic
    if (meth === 'POST' && urlPath === '/api/config/topic') {
        try {
            const body = JSON.parse(await readBody(req));
            if (body.topic_pub) topicCfg.topic_pub = body.topic_pub;
            if (body.topic_sub) topicCfg.topic_sub = body.topic_sub;
            if (body.topic_ota) topicCfg.topic_ota = body.topic_ota;
            log('[CFG]', `Topics saved: pub=${topicCfg.topic_pub} sub=${topicCfg.topic_sub} ota=${topicCfg.topic_ota}`, 'green');
            log('[SIM]', 'Device would restart here (skipped in dev mode)', 'yellow');
            sendJson({ ok: true });
        } catch {
            sendJson({ ok: false, error: 'Invalid JSON' }, 400);
        }
        return;
    }

    // GET /api/wifi/status
    if (meth === 'GET' && urlPath === '/api/wifi/status') {
        sendJson({ ...wifiState });
        return;
    }

    // GET /api/wifi/scan
    if (meth === 'GET' && urlPath === '/api/wifi/scan') {
        await new Promise(r => setTimeout(r, 1200));  // simulate scan delay
        sendJson({ aps: mockAPs });
        return;
    }

    // POST /api/wifi/connect
    if (meth === 'POST' && urlPath === '/api/wifi/connect') {
        try {
            const body = JSON.parse(await readBody(req));
            if (!body.ssid) { sendJson({ ok: false, error: 'ssid required' }, 400); return; }
            wifiState.ssid      = body.ssid;
            wifiState.connected = true;
            wifiState.ip        = '192.168.1.' + (100 + Math.floor(Math.random() * 50));
            log('[WiFi]', `Connecting to: ${body.ssid} → ${wifiState.ip}`, 'yellow');
            sendJson({ ok: true });
        } catch {
            sendJson({ ok: false, error: 'Invalid JSON' }, 400);
        }
        return;
    }

    // POST /api/wifi/forget
    if (meth === 'POST' && urlPath === '/api/wifi/forget') {
        wifiState.connected = false;
        wifiState.ssid      = '';
        wifiState.ip        = '';
        log('[WiFi]', 'Credentials forgotten', 'red');
        sendJson({ ok: true });
        return;
    }

    // POST /api/config/mqtt
    if (meth === 'POST' && urlPath === '/api/config/mqtt') {
        try {
            const body = JSON.parse(await readBody(req));
            if (body.mqtt_client_id) cfg.mqtt_client_id = body.mqtt_client_id;
            if (body.mqtt_uri  !== undefined) cfg.mqtt_uri  = body.mqtt_uri;
            if (body.mqtt_port !== undefined) cfg.mqtt_port = body.mqtt_port;
            if (body.mqtt_user !== undefined) cfg.mqtt_user = body.mqtt_user;
            if (body.mqtt_pass !== undefined) cfg.mqtt_pass = body.mqtt_pass;
            log('[CFG]', `MQTT saved: id=${cfg.mqtt_client_id} ${cfg.mqtt_uri}:${cfg.mqtt_port} user=${cfg.mqtt_user}`, 'green');
            log('[SIM]', 'Device would restart here (skipped in dev mode)', 'yellow');
            sendJson({ ok: true });
        } catch {
            sendJson({ ok: false, error: 'Invalid JSON' }, 400);
        }
        return;
    }

    // POST /api/_sim/restart  (dev-only: simulate restart broadcast)
    if (meth === 'POST' && urlPath === '/api/_sim/restart') {
        log('[SIM]', 'Broadcasting restart notice', 'yellow');
        wss.clients.forEach(ws => {
            if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ type: 'restart' }));
        });
        sendJson({ ok: true });
        return;
    }

    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not Found');
}

const server = http.createServer((req, res) => {
    route(req, res).catch(err => {
        console.error('[ERROR]', err.message);
        res.writeHead(500);
        res.end('Internal Server Error');
    });
});

// ── WebSocket server (/ws) ─────────────────────────────────────────────────
const wss = new WebSocket.Server({ noServer: true });

server.on('upgrade', (req, socket, head) => {
    const { pathname } = new URL(req.url, `http://localhost`);
    if (pathname !== '/ws') {
        socket.destroy();
        return;
    }
    wss.handleUpgrade(req, socket, head, ws => wss.emit('connection', ws, req));
});

wss.on('connection', ws => {
    log('[WS]', 'Client connected', 'green');
    ws.on('close', () => log('[WS]', 'Client disconnected', 'red'));
    ws.on('error', () => {});
});

// Broadcast state every second (mirrors ESP32 timer1_callback interval)
setInterval(() => {
    if (wss.clients.size === 0) return;
    const payload = JSON.stringify(statusPayload(true));
    wss.clients.forEach(ws => {
        if (ws.readyState === WebSocket.OPEN) ws.send(payload);
    });
}, 1000);

// ── Start ──────────────────────────────────────────────────────────────────
server.listen(PORT, () => {
    console.log(`
${COLORS.cyan}╔═══════════════════════════════════════════════╗
║      Sink IoT Dev Server                      ║
╚═══════════════════════════════════════════════╝${COLORS.reset}
  ${COLORS.green}→${COLORS.reset} Dashboard  : http://localhost:${PORT}/
  ${COLORS.green}→${COLORS.reset} Status API : http://localhost:${PORT}/api/status
  ${COLORS.green}→${COLORS.reset} Config API : http://localhost:${PORT}/api/config
  ${COLORS.green}→${COLORS.reset} WebSocket  : ws://localhost:${PORT}/ws
  ${COLORS.gray}→${COLORS.reset} Web source : ${WEB_DIR}/

  ${COLORS.gray}Flow simulation: activates every ~12–30 s
  node server.js [port]   (default: 3000)
  Ctrl+C to stop${COLORS.reset}
`);
});
