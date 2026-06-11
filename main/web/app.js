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
    document.getElementById('relay-btn').textContent = d.flow_active ? '닫기' : '열기';
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
            } else if (d.type === 'wifi') {
                updateWifiStatus(d);
                if (d.connected) addLog('WiFi 연결됨: ' + d.ssid + ' (' + d.ip + ')', 'lf');
                else             addLog('WiFi 연결 해제됨', 'ls');
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
        addLog('릴레이 토글: ' + (d.ok ? 'OK' : 'FAIL'), d.ok ? 'lf' : 'lerr');
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

/* ── WiFi ────────────────────────────────────────────────────────────────── */

function updateWifiStatus(d) {
    // 와이파이 설정 탭 상태 표시
    const dot  = document.getElementById('wd');
    const lbl  = document.getElementById('wifi-ssid-lbl');
    const ip   = document.getElementById('wifi-ip-lbl');
    dot.className = d.connected ? 'dot on' : 'dot';
    lbl.textContent = d.connected ? d.ssid : '연결 안됨';
    ip.textContent  = d.connected ? d.ip   : '';
    if (d.connected && d.ssid)
        document.getElementById('wifi-ssid-in').value = d.ssid;

    // 기기 현재 상태 탭 뱃지 동기화
    const bdot = document.getElementById('wd-s');
    const bssid = document.getElementById('wifi-badge-ssid');
    const bip   = document.getElementById('wifi-badge-ip');
    bdot.className = d.connected ? 'dot on' : 'dot';
    bssid.textContent = d.connected ? d.ssid : 'WiFi 연결 안됨';
    bip.textContent   = d.connected ? d.ip   : '';
}

async function loadWifiStatus() {
    try {
        const d = await (await fetch('/api/wifi/status')).json();
        updateWifiStatus(d);
    } catch (_) {}
}

function rssiBar(rssi) {
    if (rssi > -50) return '▂▄▆█';
    if (rssi > -60) return '▂▄▆░';
    if (rssi > -70) return '▂▄░░';
    return '▂░░░';
}

function escHtml(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

async function scanWifi() {
    const btn  = document.getElementById('scan-btn');
    const list = document.getElementById('ap-list');
    btn.disabled = true;
    btn.textContent = '검색 중...';
    list.style.display = 'block';
    list.innerHTML = '<div style="padding:10px;color:#64748b;font-size:.8rem;text-align:center">스캔 중...</div>';

    try {
        const d = await (await fetch('/api/wifi/scan')).json();
        const aps = d.aps || [];
        if (aps.length === 0) {
            list.innerHTML = '<div style="padding:10px;color:#64748b;font-size:.8rem;text-align:center">검색된 AP 없음</div>';
        } else {
            list.innerHTML = '';
            aps.forEach(ap => {
                const item = document.createElement('div');
                item.className = 'ap-item';
                item.innerHTML =
                    `<span class="ap-name">${escHtml(ap.ssid)}</span>` +
                    `<span class="ap-rssi">${ap.rssi} dBm</span>` +
                    `<span class="ap-sig">${rssiBar(ap.rssi)}</span>` +
                    `<span class="ap-lock">${ap.auth !== 0 ? '🔒' : '🔓'}</span>`;
                item.onclick = () => {
                    document.getElementById('wifi-ssid-in').value = ap.ssid;
                    document.getElementById('wifi-pass-in').value = '';
                    document.getElementById('wifi-pass-in').focus();
                };
                list.appendChild(item);
            });
        }
    } catch (e) {
        list.innerHTML = '<div style="padding:10px;color:#fb923c;font-size:.8rem;text-align:center">검색 실패</div>';
        addLog('WiFi 검색 오류: ' + e.message, 'lerr');
    } finally {
        btn.disabled = false;
        btn.textContent = 'AP 검색';
    }
}

async function connectWifi() {
    const ssid = document.getElementById('wifi-ssid-in').value.trim();
    const pass = document.getElementById('wifi-pass-in').value;
    if (!ssid) { addLog('SSID를 입력해 주세요', 'lerr'); return; }
    try {
        const d = await apiPost('/api/wifi/connect', { ssid, password: pass });
        if (d.ok) {
            addLog('WiFi 연결 시도 중: ' + ssid, 'lf');
            setTimeout(loadWifiStatus, 6000);
        }
    } catch (e) {
        addLog('WiFi 연결 오류: ' + e.message, 'lerr');
    }
}

async function forgetWifi() {
    if (!confirm('WiFi 연결을 해제하고 저장된 인증정보를 삭제하시겠습니까?')) return;
    try {
        await apiPost('/api/wifi/forget', {});
        addLog('WiFi 연결 해제됨', 'ls');
        updateWifiStatus({ connected: false });
    } catch (e) {
        addLog('WiFi 해제 오류: ' + e.message, 'lerr');
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

/* ── Tab switching ───────────────────────────────────────────────────────── */
function switchTab(name) {
    document.querySelectorAll('.tab-btn').forEach(b =>
        b.classList.toggle('active', b.dataset.tab === name));
    document.querySelectorAll('.tab-panel').forEach(p =>
        p.classList.toggle('active', p.dataset.tab === name));
}

/* ── Init ───────────────────────────────────────────────────────────────── */
connectWS();
loadConfig();
loadGPIO();
loadTopics();
loadMisc();
loadWifiStatus();
