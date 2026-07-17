// A800X DSP Controller — Web UI Logic
(function() {
  'use strict';

  const API_BASE = '/api';

  // --- DOM-Refs ---
  const $ = id => document.getElementById(id);
  const dspStatus = $('dsp-status');
  const wifiStatus = $('wifi-status');
  const deviceIp = $('device-ip');
  const sysHostname = $('sys-hostname');
  const sysMac = $('sys-mac');
  const sysIp = $('sys-ip');
  const sysVersion = $('sys-version');
  const preeqDetails = $('preeq-details');
  const preeqFilters = $('preeq-filters');
  const otaUrl = $('ota-url');
  const otaProgress = $('ota-progress');

  // --- API Helpers ---
  async function api(method, path, body) {
    const opts = { method, headers: {} };
    if (body) {
      opts.headers['Content-Type'] = 'application/json';
      opts.body = JSON.stringify(body);
    }
    const res = await fetch(API_BASE + path, opts);
    return res.json();
  }

  // --- Status Update ---
  async function updateStatus() {
    try {
      const data = await api('GET', '/status');
      dspStatus.textContent = data.dsp_connected ? 'DSP' : 'DSP ✗';
      dspStatus.className = 'status-dot ' + (data.dsp_connected ? 'dot-on' : 'dot-off');
      wifiStatus.textContent = data.wifi_connected ? 'WiFi' : 'WiFi ✗';
      wifiStatus.className = 'status-dot ' + (data.wifi_connected ? 'dot-on' : 'dot-off');
      deviceIp.textContent = data.ip || '-';
      sysHostname.textContent = data.hostname || '-';
      sysMac.textContent = data.mac || '-';
      sysIp.textContent = data.ip || '-';
      sysVersion.textContent = data.version || '-';
    } catch (e) {
      console.error('Status update failed:', e);
    }
  }

  // --- DSP State ---
  async function updateDspState() {
    try {
      const data = await api('GET', '/dsp');
      $('noise-suppressor').checked = data.noise_suppressor;
      $('virtual-bass').checked = data.virtual_bass;
      $('silence-detector').checked = data.silence_detector;
      $('preeq-enable').checked = data.preeq_enabled;
      $('drc-enable').checked = data.drc_enabled;

      // PreEQ Filters
      if (data.preeq_filters && data.preeq_filters.length > 0) {
        preeqDetails.classList.remove('hidden');
        preeqFilters.innerHTML = data.preeq_filters.map((f, i) =>
          `<div class="filter-card">
            <strong>F${i}</strong> ${f.enabled ? '✓' : '✗'}
            ${f.enabled ? `| ${['PK','LS','HS','LP','HP','BP','NH','LO','HO'][f.type] || '?'} ${f.frequency_hz} Hz Q:${(f.q_raw / 1024).toFixed(2)} ${(f.gain_raw / 256).toFixed(1)} dB` : ''}
          </div>`
        ).join('');
      } else {
        preeqDetails.classList.add('hidden');
      }
    } catch (e) {
      console.error('DSP state update failed:', e);
    }
  }

  // --- Toggle Handlers ---
  async function handleToggle(effect, enable) {
    const endpoint = effect === 'noise' ? '/dsp/noise' :
                     effect === 'bass' ? '/dsp/bass' : '';
    if (!endpoint) return;
    await api('POST', endpoint, { enable });
  }

  document.querySelectorAll('[data-effect]').forEach(el => {
    el.addEventListener('change', e => {
      handleToggle(e.target.dataset.effect, e.target.checked);
    });
  });

  // --- WiFi Reconnect ---
  async function handleWifiConnect(ssid, password) {
    await api('POST', '/wifi/connect', { ssid, password });
  }

  // --- OTA Update ---
  $('btn-ota-update').addEventListener('click', async () => {
    const url = otaUrl.value.trim();
    if (!url) return;
    otaProgress.classList.remove('hidden');
    $('btn-ota-update').disabled = true;
    try {
      await api('POST', '/ota/update', { url });
    } catch (e) {
      alert('OTA-Update fehlgeschlagen: ' + e.message);
    }
    otaProgress.classList.add('hidden');
    $('btn-ota-update').disabled = false;
  });

  // --- Factory Reset ---
  $('btn-factory-reset').addEventListener('click', async () => {
    if (!confirm('Werkseinstellungen wiederherstellen? Gerät startet neu.')) return;
    await api('POST', '/device/reset');
  });

  // --- Profile Save/Load ---
  $('btn-profile-save').addEventListener('click', async () => {
    const name = prompt('Profilname:');
    if (!name) return;
    await api('POST', '/profiles/save', { name });
  });

  $('btn-profile-load').addEventListener('click', async () => {
    const data = await api('GET', '/profiles');
    // UI für Profilauswahl (vereinfacht)
    alert('Profile: ' + JSON.stringify(data.profiles || []));
  });

  // --- Config Export/Import ---
  $('btn-config-export').addEventListener('click', async () => {
    const data = await api('POST', '/config/export');
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'a800x-config.json';
    a.click();
  });

  $('btn-config-import').addEventListener('click', async () => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async () => {
      const text = await input.files[0].text();
      await api('POST', '/config/import', JSON.parse(text));
      updateDspState();
    };
    input.click();
  });

  // --- Init ---
  async function init() {
    await updateStatus();
    await updateDspState();
    setInterval(updateStatus, 10000);
  }

  document.addEventListener('DOMContentLoaded', init);
})();