'use strict';

const PULSE_PER_LITER = 660;
let ws = null;

/* ── Event log ─────────────────────────────────────────────────────────── */
function addLog(msg, cls) {
    const container = document.getElementById('log');
    const entry = document.createElement('div');
    entry.className = 'le' + (cls ? ' ' + cls : '');
    entry.textContent = new Date().toLocaleTimeString() + '  ' + msg;
    container.insertBefore(entry, container.firstChild);
    if (container.children.length > 100) {
        container.removeChild(container.lastChild);
    }
}

/* ── Status update ──────────────────────────────────────────────────────── */
function updateStatus(d) {
    const el = document.getElementById('fs');
    el.textContent = d.flow_active ? '흐름' : '정지';
    el.className = 'val ' + (d.flow_active ? 'flow' : 'stop');
    document.getElementById('ps').textContent = d.pulse_per_sec;
    document.getElementById('pa').textContent = d.pulse_accum;
    document.getElementById('vl').textContent = (d.pulse_accum / PULSE_PER_LITER).toFixed(3);
}

/* ── WebSocket ──────────────────────────────────────────────────────────── */
function connectWS() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(proto + '//' + location.host + '/ws');

    ws.onopen = () => {
        document.getElementById('wsd').className = 'dot on';
        addLog('WebSocket 연결됨');
    };

    ws.onmessage = (e) => {
        try {
            const d = JSON.parse(e.data);
            if (d.type === 'status') {
                updateStatus(d);
                if (d.flow_active) {
                    addLog('유량: ' + d.pulse_per_sec + ' 펄스/초', 'lf');
                }
            }
        } catch (_) {}
    };

    ws.onclose = () => {
        document.getElementById('wsd').className = 'dot';
        addLog('연결 끊김, 3초 후 재연결...');
        setTimeout(connectWS, 3000);
    };

    ws.onerror = () => ws.close();
}

/* ── REST helpers ───────────────────────────────────────────────────────── */
async function apiPost(url, body) {
    const res = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
    });
    return res.json();
}

/* ── Relay toggle ───────────────────────────────────────────────────────── */
async function toggleRelay() {
    try {
        const d = await apiPost('/api/relay/toggle', {});
        addLog('릴레이 토글: ' + (d.ok ? 'OK' : 'FAIL'));
    } catch (e) {
        addLog('릴레이 오류: ' + e.message, 'lerr');
    }
}

/* ── Config load ────────────────────────────────────────────────────────── */
async function loadConfig() {
    try {
        const d = await (await fetch('/api/config')).json();
        document.getElementById('mc').value  = d.mqtt_client_id || '';
        document.getElementById('mu').value  = d.mqtt_uri       || '';
        document.getElementById('mp').value  = d.mqtt_port      || 1883;
        document.getElementById('mun').value = d.mqtt_user      || '';
        document.getElementById('mpw').value = d.mqtt_pass      || '';
        document.getElementById('aoe').checked = !!d.auto_off_enabled;
        document.getElementById('aot').value   = d.auto_off_time || 5;
        addLog('설정 로드 완료');
    } catch (e) {
        addLog('설정 로드 실패: ' + e.message, 'lerr');
    }
}

/* ── Auto-off save (immediate apply) ────────────────────────────────────── */
async function saveAO() {
    try {
        const d = await apiPost('/api/config/autooff', {
            auto_off_enabled: document.getElementById('aoe').checked,
            auto_off_time:    parseInt(document.getElementById('aot').value) || 5,
        });
        addLog('자동 절수 설정: ' + (d.ok ? '완료' : '실패'), d.ok ? 'lf' : 'lerr');
    } catch (e) {
        addLog('저장 오류: ' + e.message, 'lerr');
    }
}

/* ── GPIO config load / save ─────────────────────────────────────────────── */
async function loadGPIO() {
    try {
        const d = await (await fetch('/api/config/gpio')).json();
        document.getElementById('gp-relay').value = d.pin_relay ?? '';
        document.getElementById('gp-led1').value  = d.pin_led1  ?? '';
        document.getElementById('gp-led2').value  = d.pin_led2  ?? '';
        document.getElementById('gp-flow').value  = d.pin_flow  ?? '';
        document.getElementById('gp-pwm').value   = d.pin_pwm   ?? '';
    } catch (e) {
        addLog('GPIO 설정 로드 실패: ' + e.message, 'lerr');
    }
}

async function saveGPIO() {
    if (!confirm('저장 후 디바이스가 재시작됩니다. 계속하시겠습니까?')) return;
    try {
        const parse = (id) => { const v = parseInt(document.getElementById(id).value); return isNaN(v) ? null : v; };
        const body = { pin_relay: parse('gp-relay'), pin_led1: parse('gp-led1'), pin_led2: parse('gp-led2'), pin_flow: parse('gp-flow'), pin_pwm: parse('gp-pwm') };
        if (Object.values(body).some(v => v === null || v < 0 || v > 39)) {
            addLog('핀 번호는 0–39 사이여야 합니다', 'lerr');
            return;
        }
        await apiPost('/api/config/gpio', body);
        addLog('GPIO 설정 저장 완료, 재시작 중...', 'lf');
    } catch (_) {
        addLog('재시작 중...', 'lf');
    }
}

/* ── MQTT topic config load / save ──────────────────────────────────────── */
async function loadTopics() {
    try {
        const d = await (await fetch('/api/config/topic')).json();
        document.getElementById('tp-pub').value = d.topic_pub || '';
        document.getElementById('tp-sub').value = d.topic_sub || '';
        document.getElementById('tp-ota').value = d.topic_ota || '';
    } catch (e) {
        addLog('토픽 설정 로드 실패: ' + e.message, 'lerr');
    }
}

async function saveTopics() {
    if (!confirm('저장 후 디바이스가 재시작됩니다. 계속하시겠습니까?')) return;
    try {
        await apiPost('/api/config/topic', {
            topic_pub: document.getElementById('tp-pub').value,
            topic_sub: document.getElementById('tp-sub').value,
            topic_ota: document.getElementById('tp-ota').value,
        });
        addLog('토픽 설정 저장 완료, 재시작 중...', 'lf');
    } catch (_) {
        addLog('재시작 중...', 'lf');
    }
}

/* ── MQTT save + restart ─────────────────────────────────────────────────── */
async function saveMQ() {
    if (!confirm('저장 후 디바이스가 재시작됩니다. 계속하시겠습니까?')) return;
    try {
        await apiPost('/api/config/mqtt', {
            mqtt_client_id: document.getElementById('mc').value,
            mqtt_uri:       document.getElementById('mu').value,
            mqtt_port:      parseInt(document.getElementById('mp').value) || 1883,
            mqtt_user:      document.getElementById('mun').value,
            mqtt_pass:      document.getElementById('mpw').value,
        });
        addLog('MQTT 저장 완료, 재시작 중...', 'lf');
    } catch (_) {
        addLog('재시작 중...', 'lf');
    }
}

/* ── Misc config load / save (immediate apply, no restart) ──────────────── */
async function loadMisc() {
    try {
        const d = await (await fetch('/api/config/misc')).json();
        document.getElementById('ms-relay').value = d.relay_toggle_ms      ?? '';
        document.getElementById('ms-ppl').value   = d.flow_pulse_per_liter ?? '';
        document.getElementById('ms-disp').value  = d.display_slave_addr !== undefined
            ? '0x' + d.display_slave_addr.toString(16).toUpperCase().padStart(2, '0') : '';
    } catch (e) {
        addLog('기타 설정 로드 실패: ' + e.message, 'lerr');
    }
}

async function saveMisc() {
    try {
        const parseAddr = (s) => {
            const v = s.startsWith('0x') || s.startsWith('0X') ? parseInt(s, 16) : parseInt(s, 10);
            return isNaN(v) ? null : v;
        };
        const relay = parseInt(document.getElementById('ms-relay').value);
        const ppl   = parseInt(document.getElementById('ms-ppl').value);
        const addr  = parseAddr(document.getElementById('ms-disp').value);
        if (isNaN(relay) || relay < 1 || isNaN(ppl) || ppl < 1 || addr === null) {
            addLog('입력값을 확인해 주세요', 'lerr');
            return;
        }
        const d = await apiPost('/api/config/misc', {
            relay_toggle_ms:     relay,
            flow_pulse_per_liter: ppl,
            display_slave_addr:  addr,
        });
        addLog('기타 설정 적용: ' + (d.ok ? '완료' : '실패'), d.ok ? 'lf' : 'lerr');
    } catch (e) {
        addLog('기타 설정 오류: ' + e.message, 'lerr');
    }
}

/* ── Polling fallback (when WS is disconnected) ─────────────────────────── */
setInterval(async () => {
    if (ws && ws.readyState === WebSocket.OPEN) return;
    try {
        const d = await (await fetch('/api/status')).json();
        updateStatus(d);
    } catch (_) {}
}, 5000);

/* ── Init ───────────────────────────────────────────────────────────────── */
connectWS();
loadConfig();
loadGPIO();
loadTopics();
loadMisc();
