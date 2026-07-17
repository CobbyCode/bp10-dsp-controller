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
  const sysApIp = $('sys-ap-ip');
  const sysVersion = $('sys-version');
  const dspSection = $('dsp-controls');
  const dspUnavailable = $('dsp-unavailable');
  const preeqDetails = $('preeq-details');
  const preeqFilters = $('preeq-filters');
  const otaUrl = $('ota-url');
  const otaProgress = $('ota-progress');
  const nsStatus = $('ns-readback');

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

      // DSP-Status: grün nur aus bestätigtem Readback
      if (data.dsp_connected) {
        dspStatus.textContent = 'DSP ✓';
        dspStatus.className = 'status-dot dot-on';
        dspSection.classList.remove('hidden');
        if (dspUnavailable) dspUnavailable.classList.add('hidden');
        if (data.dsp_noise_suppressor !== undefined) {
          const nsEl = $('noise-suppressor');
          if (nsEl) nsEl.checked = data.dsp_noise_suppressor;
        }
      } else {
        dspStatus.textContent = 'DSP ✗';
        dspStatus.className = 'status-dot dot-off';
        dspSection.classList.add('hidden');
        if (dspUnavailable) dspUnavailable.classList.remove('hidden');
      }

      // WiFi-Status: AP + STA getrennt
      if (data.sta_connected) {
        wifiStatus.textContent = 'STA ✓';
        wifiStatus.className = 'status-dot dot-on';
      } else {
        wifiStatus.textContent = 'AP';
        wifiStatus.className = 'status-dot dot-warn';
      }

      // IP: AP oder STA
      if (data.sta_ip) {
        deviceIp.textContent = 'STA: ' + data.sta_ip;
      } else {
        deviceIp.textContent = 'AP: ' + data.ap_ip;
      }

      // System-Info
      sysHostname.textContent = data.hostname || '-';
      sysMac.textContent = data.mac || '-';
      sysApIp.textContent = data.ap_ip || '-';
      sysIp.textContent = data.sta_ip || data.ap_ip || '-';
      sysVersion.textContent = data.version || '-';
    } catch (e) {
      console.error('Status update failed:', e);
    }
  }

  // --- DSP State (nur bei Connected ausgeführt) ---
  async function updateDspState() {
    try {
      const data = await api('GET', '/dsp');

      if (data.dsp === 'nicht verfügbar') {
        dspSection.classList.add('hidden');
        if (dspUnavailable) dspUnavailable.classList.remove('hidden');
        return;
      }

      dspSection.classList.remove('hidden');
      if (dspUnavailable) dspUnavailable.classList.add('hidden');

      $('noise-suppressor').checked = data.noise_suppressor;
      $('virtual-bass').checked = data.virtual_bass;
      $('silence-detector').checked = data.silence_detector;
      $('preeq-enable').checked = data.preeq_enabled;
      $('drc-enable').checked = data.drc_enabled;

      // Noise Suppressor Readback-Status
      if (nsStatus) {
        nsStatus.textContent = 'NS: ' + (data.noise_suppressor ? 'EIN' : 'AUS');
      }

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
    setInterval(updateStatus, 5000);
    setInterval(updateDspState, 15000);
  }

  document.addEventListener('DOMContentLoaded', init);
})();