// A800X DSP Controller — Web UI Logic
(function() {
  'use strict';

  const API_BASE = '/api';

  // --- DOM-Refs ---
  const $ = id => document.getElementById(id);
  const dspStatus = $('dsp-status');
  const wifiStatus = $('wifi-status');
  const sysHostname = $('sys-hostname');
  const sysMac = $('sys-mac');
  const sysIp = $('sys-ip');
  const sysApIp = $('sys-ap-ip');
  const sysApRow = $('sys-ap-row');
  const sysMdns = $('sys-mdns');
  const sysVersion = $('sys-version');
  const connectionBanner = $('connection-banner');
  const connectionBannerText = $('connection-banner-text');
  const dspSection = $('dsp-controls');
  const dspUnavailable = $('dsp-unavailable');
  const preeqFilters = $('preeq-filters');
  const noiseState = $('noise-state');
  const noiseMessage = $('noise-message');
  const noiseEditor = $('noise-module').querySelector('.module-editor');
  const bassState = $('bass-state');
  const bassMessage = $('bass-message');
  const bassEditor = $('bass-module').querySelector('.module-editor');
  const silenceState = $('silence-state');
  const silenceMessage = $('silence-message');
  const silenceEditor = $('silence-module').querySelector('.module-editor');
  const preeqState = $('preeq-state');
  const preeqMessage = $('preeq-message');
  const preeqEditor = $('preeq-module').querySelector('.module-editor');
  const drcState = $('drc-state');
  const drcMessage = $('drc-message');
  const drcEditor = $('drc-module').querySelector('.module-editor');
  let preeqBaseline = null;
  const factoryPreeqFilters = [
    null, // F0 gehört zum rückseitigen Crossover und wird immer erhalten.
    { enabled:true,  type:3, frequency_hz:500,   q:0.707, gain_db:0 },
    { enabled:true,  type:4, frequency_hz:35,    q:0.800, gain_db:0 },
    { enabled:true,  type:0, frequency_hz:55,    q:3.500, gain_db:1.5 },
    { enabled:true,  type:0, frequency_hz:85,    q:3.500, gain_db:1.5 },
    { enabled:false, type:0, frequency_hz:0,     q:0,     gain_db:0 },
    { enabled:false, type:0, frequency_hz:0,     q:0,     gain_db:0 },
    { enabled:false, type:0, frequency_hz:20000, q:0.707, gain_db:0 },
    { enabled:false, type:0, frequency_hz:0,     q:0,     gain_db:0 },
    { enabled:false, type:0, frequency_hz:0,     q:0,     gain_db:0 }
  ];
  const wifiForm = $('wifi-form');
  const wifiSsid = $('wifi-ssid');
  const wifiPassword = $('wifi-password');
  const wifiAutoOff = $('wifi-auto-off');
  const wifiConfigState = $('wifi-config-state');
  const wifiFormMessage = $('wifi-form-message');
  const wifiScanButton = $('btn-wifi-scan');
  const wifiScanResults = $('wifi-scan-results');
  let wifiScanPolling = false;

  // --- API Helpers ---
  function formatBytes(bytes) {
    if (!bytes) return '0 B';
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(2) + ' MB';
  }

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
  function updateWifiStatusBar(data) {
    if (data.sta_connected && !data.ap_active) {
      // Reiner STA-Modus
      wifiStatus.textContent = 'STA ✓';
      wifiStatus.className = 'status-dot dot-on';
    } else if (data.sta_connected && data.ap_active) {
      // AP+STA – Übergangsphase
      wifiStatus.textContent = 'AP+STA ✓';
      wifiStatus.className = 'status-dot dot-on';
    } else if (data.ap_active) {
      // Nur AP
      wifiStatus.textContent = 'AP';
      wifiStatus.className = 'status-dot dot-warn';
    } else {
      wifiStatus.textContent = '✗';
      wifiStatus.className = 'status-dot dot-off';
    }
  }

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
        resetSilenceUnread();
        resetPreeqUnread();
        resetDrcUnread();
      }

      // WiFi-Status: AP + STA getrennt
      updateWifiStatusBar(data);

      // System-Info
      sysHostname.textContent = data.hostname || '-';
      sysMac.textContent = data.mac || '-';
      sysMdns.textContent = data.mdns_address || '-';

      // AP-Zeile nur anzeigen wenn AP aktiv
      if (data.ap_active) {
        sysApRow.classList.remove('hidden');
        sysApIp.textContent = data.ap_ip || '192.168.4.1';

        // Countdown-Banner
        if (data.ap_shutdown_remaining_s > 0) {
          connectionBanner.classList.remove('hidden');
          connectionBanner.className = 'connection-banner is-transition';
          connectionBannerText.textContent =
            'Setup Wi-Fi shuts down in ' + data.ap_shutdown_remaining_s +
            ' seconds';
        } else if (data.sta_connected) {
          connectionBanner.classList.remove('hidden');
          connectionBanner.className = 'connection-banner is-connected';
          connectionBannerText.innerHTML =
            '✓ Connected to home network · ' +
            (data.sta_ssid || '') + ' · ' +
            (data.sta_ip || '') + '<br>' +
            '<small>WebGUI: <a href="http://' + (data.mdns_address || '') +
            '">' + (data.mdns_address || '') + '</a> or ' +
            (data.sta_ip || '') + '</small>';
        }
      } else {
        sysApRow.classList.add('hidden');
        connectionBanner.classList.add('hidden');
      }

      sysIp.textContent = data.sta_connected && data.sta_ip
        ? data.sta_ip : 'not connected';
      sysVersion.textContent = data.version || '-';
    } catch (e) {
      console.error('Status update failed:', e);
    }
  }

  async function loadWifiConfig() {
    try {
      const data = await api('GET', '/wifi/config');
      wifiSsid.value = data.ssid || '';
      wifiPassword.value = '';
      wifiPassword.placeholder = data.password_saved
        ? 'Saved — leave blank to keep it'
        : 'Wi-Fi password';
      wifiAutoOff.checked = data.auto_off === true;
      wifiConfigState.textContent = data.configured ? 'CONFIGURED' : 'NOT CONFIGURED';
      wifiConfigState.className = 'state-pill ' + (data.configured ? 'is-ready' : '');
    } catch (error) {
      wifiFormMessage.textContent = 'Unable to read Wi-Fi configuration';
      wifiFormMessage.className = 'form-message is-error';
    }
  }

  wifiForm.addEventListener('submit', async event => {
    event.preventDefault();
    const button = $('btn-wifi-save');
    button.disabled = true;
    wifiFormMessage.textContent = 'Saving…';
    wifiFormMessage.className = 'form-message';
    try {
      const result = await api('POST', '/wifi/config', {
        ssid: wifiSsid.value.trim(),
        password: wifiPassword.value,
        auto_off: wifiAutoOff.checked
      });
      if (result.status !== 'ok') throw new Error(result.error || 'Save failed');
      wifiFormMessage.textContent = 'Saved — connecting…';
      wifiFormMessage.className = 'form-message';
      await loadWifiConfig();
      await waitForWifiConnection();
    } catch (error) {
      wifiFormMessage.textContent = error.message;
      wifiFormMessage.className = 'form-message is-error';
    } finally {
      button.disabled = false;
    }
  });

  function renderWifiNetworks(networks) {
    wifiScanResults.innerHTML = '';
    wifiScanResults.classList.remove('hidden');
    if (!networks.length) {
      wifiScanResults.textContent = 'No visible Wi-Fi networks found.';
      return;
    }
    networks.forEach(network => {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'wifi-network';
      button.innerHTML = `<strong></strong><span>${network.rssi} dBm · ${network.quality} · ${network.secure ? 'Encrypted' : 'Open'}</span>`;
      button.querySelector('strong').textContent = network.ssid;
      button.addEventListener('click', () => {
        wifiSsid.value = network.ssid;
        wifiSsid.focus();
      });
      wifiScanResults.appendChild(button);
    });
  }

  async function pollWifiScan() {
    if (!wifiScanPolling) return;
    try {
      const result = await api('GET', '/wifi/scan');
      if (result.state === 'scanning') {
        setTimeout(pollWifiScan, 700);
        return;
      }
      wifiScanPolling = false;
      wifiScanButton.disabled = false;
      wifiScanButton.textContent = 'Refresh';
      if (result.state === 'failed') throw new Error(result.message || 'Wi-Fi scan failed');
      renderWifiNetworks(result.networks || []);
    } catch (error) {
      wifiScanPolling = false;
      wifiScanButton.disabled = false;
      wifiScanButton.textContent = 'Refresh';
      wifiScanResults.classList.remove('hidden');
      wifiScanResults.textContent = error.message || 'Wi-Fi scan failed; please try again.';
    }
  }

  wifiScanButton.addEventListener('click', async () => {
    if (wifiScanPolling) return;
    wifiScanPolling = true;
    wifiScanButton.disabled = true;
    wifiScanButton.textContent = 'Scanning…';
    wifiScanResults.classList.remove('hidden');
    wifiScanResults.textContent = 'Scanning…';
    try {
      const result = await api('POST', '/wifi/scan');
      if (result.status !== 'ok') throw new Error(result.error || 'Unable to start Wi-Fi scan');
      setTimeout(pollWifiScan, 400);
    } catch (error) {
      wifiScanPolling = false;
      wifiScanButton.disabled = false;
      wifiScanButton.textContent = 'Scan Wi-Fi';
      wifiScanResults.textContent = error.message;
    }
  });

  async function waitForWifiConnection() {
    for (let attempt = 0; attempt < 20; attempt += 1) {
      await new Promise(resolve => setTimeout(resolve, 750));
      const status = await api('GET', '/wifi/status');
      if (status.sta_connected) {
        wifiFormMessage.textContent = `Connected · ${status.sta_ip || 'IP assigned'}`;
        wifiFormMessage.className = 'form-message is-success';
        sysIp.textContent = status.sta_ip || '-';
        return;
      }
      if (status.connection_state === 'failed') {
        throw new Error(status.connection_message || 'Connection failed; Setup AP remains available');
      }
      wifiFormMessage.textContent = status.connection_message || 'Connecting…';
    }
    throw new Error('Connection is taking longer than expected; Setup AP remains available');
  }

  // --- DSP State (nur bei Connected ausgeführt) ---
  async function updateDspState() {
    try {
      const data = await api('GET', '/dsp');

      if (data.dsp === 'nicht verfügbar' || data.dsp === 'unavailable') {
        dspSection.classList.add('hidden');
        if (dspUnavailable) dspUnavailable.classList.remove('hidden');
        return;
      }

      dspSection.classList.remove('hidden');
      if (dspUnavailable) dspUnavailable.classList.add('hidden');

      setNoiseForm(data);
      setBassForm(data);
      if (data.silence && data.silence.valid === true) {
        setSilenceForm(data.silence.enabled);
      } else {
        resetSilenceUnread();
      }
      if (data.preeq && data.preeq.valid === true) {
        const preeqData = {
          preeq_enabled: data.preeq.enabled,
          preeq_pregain_db: data.preeq.pregain_db,
          preeq_filters: data.preeq.filters
        };
        if (!preeqEditor.classList.contains('is-dirty')) setPreeqForm(preeqData);
      } else {
        resetPreeqUnread();
      }
      if (data.drc && data.drc.valid === true) setDrcForm(data.drc);
      else resetDrcUnread();
    } catch (e) {
      console.error('DSP state update failed:', e);
    }
  }

  function setBassForm(data) {
    $('virtual-bass').checked = data.virtual_bass === true;
    $('bass-cutoff').value = data.bass_cutoff_hz;
    $('bass-intensity').value = data.bass_intensity_pct;
    $('bass-enhanced').checked = data.bass_enhanced === true;
    bassState.textContent = data.virtual_bass ? 'ON · CONFIRMED' : 'OFF · CONFIRMED';
    bassState.className = 'module-state ' + (data.virtual_bass ? 'is-on' : '');
    bassEditor.classList.remove('is-dirty');
  }

  ['virtual-bass', 'bass-cutoff', 'bass-intensity', 'bass-enhanced']
    .forEach(id => $(id).addEventListener('input', () => {
      bassEditor.classList.add('is-dirty');
      bassMessage.textContent = 'Unapplied changes';
      bassMessage.className = 'form-message';
    }));

  $('btn-bass-read').addEventListener('click', async () => {
    $('virtual-bass').checked = false;
    $('bass-cutoff').value = 42;
    $('bass-intensity').value = 4;
    $('bass-enhanced').checked = false;
    bassEditor.classList.add('is-dirty');
    bassMessage.textContent = 'Factory values loaded locally · Apply to write';
    bassMessage.className = 'form-message';
  });

  $('btn-bass-apply').addEventListener('click', async () => {
    const button = $('btn-bass-apply');
    button.disabled = true;
    bassMessage.textContent = 'Writing and verifying…';
    try {
      const result = await api('POST', '/dsp/bass', {
        enable: $('virtual-bass').checked,
        cutoff_hz: Number($('bass-cutoff').value),
        intensity_pct: Number($('bass-intensity').value),
        bass_enhanced: $('bass-enhanced').checked
      });
      if (result.status !== 'ok' || !result.data || !result.data.confirmed) {
        throw new Error(result.error || 'Readback mismatch');
      }
      setBassForm({
        virtual_bass: result.data.enabled,
        bass_cutoff_hz: result.data.cutoff_hz,
        bass_intensity_pct: result.data.intensity_pct,
        bass_enhanced: result.data.bass_enhanced
      });
      bassMessage.textContent = 'Change confirmed by readback';
      bassMessage.className = 'form-message is-success';
    } catch (error) {
      bassState.textContent = 'MISMATCH';
      bassState.className = 'module-state is-error';
      bassMessage.textContent = error.message;
      bassMessage.className = 'form-message is-error';
    } finally { button.disabled = false; }
  });

  function setNoiseForm(data) {
    $('noise-suppressor').checked = data.noise_suppressor === true;
    $('noise-threshold').value = Number(data.noise_threshold_db).toFixed(2);
    $('noise-ratio').value = data.noise_ratio;
    $('noise-attack').value = data.noise_attack_ms;
    $('noise-release').value = data.noise_release_ms;
    noiseState.textContent = data.noise_suppressor ? 'ON · CONFIRMED' : 'OFF · CONFIRMED';
    noiseState.className = 'module-state ' + (data.noise_suppressor ? 'is-on' : '');
    noiseEditor.classList.remove('is-dirty');
  }

  function setDrcForm(data) {
    const fullBandSupported = Number(data.mode) === 0;
    $('drc-enable').checked = data.enabled === true;
    $('drc-pregain').value = Number(data.pregain_db).toFixed(2);
    $('drc-threshold').value = Number(data.threshold_db).toFixed(2);
    $('drc-ratio').value = Number(data.ratio).toFixed(2);
    $('drc-attack').value = data.attack_ms;
    $('drc-release').value = data.release_ms;
    ['drc-enable','drc-pregain','drc-threshold','drc-ratio','drc-attack','drc-release']
      .forEach(id => $(id).disabled = !fullBandSupported);
    $('btn-drc-reset').disabled = !fullBandSupported;
    $('btn-drc-apply').disabled = !fullBandSupported;
    drcState.textContent = fullBandSupported
      ? (data.enabled ? 'ON · CONFIRMED' : 'OFF · CONFIRMED')
      : `MODE ${data.mode} · READ ONLY`;
    drcState.className = 'module-state ' + (fullBandSupported && data.enabled ? 'is-on' : '');
    drcMessage.textContent = fullBandSupported
      ? ''
      : 'Only Full-Band mode can be edited safely; multiband and crossover values remain unchanged.';
    drcMessage.className = 'form-message' + (fullBandSupported ? '' : ' is-error');
    drcEditor.classList.remove('is-dirty');
  }

  function resetDrcUnread() {
    ['drc-enable','drc-pregain','drc-threshold','drc-ratio','drc-attack','drc-release']
      .forEach(id => { $(id).disabled = true; if (id !== 'drc-enable') $(id).value = ''; });
    $('drc-enable').checked = false;
    $('btn-drc-reset').disabled = true;
    $('btn-drc-apply').disabled = true;
    drcState.textContent = 'NOT READ';
    drcState.className = 'module-state';
    drcEditor.classList.remove('is-dirty');
  }

  ['drc-enable','drc-pregain','drc-threshold','drc-ratio','drc-attack','drc-release']
    .forEach(id => $(id).addEventListener('input', () => {
      drcEditor.classList.add('is-dirty');
      drcMessage.textContent = 'Unapplied changes';
      drcMessage.className = 'form-message';
    }));

  $('btn-drc-reset').addEventListener('click', () => {
    $('drc-enable').checked = true;
    $('drc-pregain').value = '2.00';
    $('drc-threshold').value = '-5.00';
    $('drc-ratio').value = '1.00';
    $('drc-attack').value = 2;
    $('drc-release').value = 800;
    drcEditor.classList.add('is-dirty');
    drcMessage.textContent = 'Full-Band factory values loaded locally · Apply to write';
    drcMessage.className = 'form-message';
  });

  $('btn-drc-apply').addEventListener('click', async () => {
    const button = $('btn-drc-apply');
    button.disabled = true;
    drcMessage.textContent = 'Read-modify-write and verification…';
    try {
      const result = await api('POST', '/dsp/drc', {
        enable: $('drc-enable').checked,
        pregain_db: Number($('drc-pregain').value),
        threshold_db: Number($('drc-threshold').value),
        ratio: Number($('drc-ratio').value),
        attack_ms: Number($('drc-attack').value),
        release_ms: Number($('drc-release').value)
      });
      if (result.status !== 'ok' || !result.data || !result.data.confirmed) {
        throw new Error(result.error || 'Readback mismatch');
      }
      setDrcForm(result.data);
      drcMessage.textContent = 'Complete DRC readback confirmed';
      drcMessage.className = 'form-message is-success';
    } catch (error) {
      drcState.textContent = 'MISMATCH';
      drcState.className = 'module-state is-error';
      drcMessage.textContent = error.message;
      drcMessage.className = 'form-message is-error';
    } finally { button.disabled = $('drc-enable').disabled; }
  });

  function setSilenceForm(enabled) {
    $('silence-detector').checked = enabled === true;
    $('silence-detector').disabled = false;
    $('btn-silence-apply').disabled = false;
    $('btn-silence-read').disabled = false;
    silenceState.textContent = enabled ? 'ON · CONFIRMED' : 'OFF · CONFIRMED';
    silenceState.className = 'module-state ' + (enabled ? 'is-on' : '');
    silenceEditor.classList.remove('is-dirty');
  }

  function resetSilenceUnread() {
    $('silence-detector').checked = false;
    $('silence-detector').disabled = true;
    $('btn-silence-apply').disabled = true;
    $('btn-silence-read').disabled = true;
    silenceState.textContent = 'NOT READ';
    silenceState.className = 'module-state';
    silenceEditor.classList.remove('is-dirty');
  }

  $('silence-detector').addEventListener('input', () => {
    silenceEditor.classList.add('is-dirty');
    silenceMessage.textContent = 'Unapplied change';
    silenceMessage.className = 'form-message';
  });

  $('btn-silence-read').addEventListener('click', async () => {
    $('silence-detector').checked = false;
    silenceEditor.classList.add('is-dirty');
    silenceMessage.textContent = 'Factory value OFF loaded locally · Apply to write';
    silenceMessage.className = 'form-message';
  });

  $('btn-silence-apply').addEventListener('click', async () => {
    const button = $('btn-silence-apply');
    button.disabled = true;
    silenceMessage.textContent = 'Writing and verifying…';
    try {
      const result = await api('POST', '/dsp/silence', {
        enable: $('silence-detector').checked
      });
      if (result.status !== 'ok' || !result.data || !result.data.confirmed) {
        throw new Error(result.error || 'Readback mismatch');
      }
      setSilenceForm(result.data.enabled);
      silenceMessage.textContent = 'Change confirmed by readback';
      silenceMessage.className = 'form-message is-success';
    } catch (error) {
      silenceState.textContent = 'MISMATCH';
      silenceState.className = 'module-state is-error';
      silenceMessage.textContent = error.message;
      silenceMessage.className = 'form-message is-error';
    } finally {
      button.disabled = $('silence-detector').disabled;
    }
  });

  const filterTypes = ['PK','LS','HS','LP','HP','BP','NH','LO','HO'];

  function clonePreeq(data) {
    return {
      enabled: data.preeq_enabled === true,
      pregain_db: Number(data.preeq_pregain_db),
      filters: (data.preeq_filters || []).map(f => ({
        enabled: f.enabled === true, type: Number(f.type),
        frequency_hz: Number(f.frequency_hz), q: Number(f.q), gain_db: Number(f.gain_db)
      }))
    };
  }

  function setPreeqForm(data) {
    if (!Array.isArray(data.preeq_filters) || data.preeq_filters.length !== 10) return;
    preeqBaseline = clonePreeq(data);
    $('preeq-enable').checked = preeqBaseline.enabled;
    $('preeq-enable').disabled = false;
    $('preeq-pregain').value = preeqBaseline.pregain_db.toFixed(2);
    $('preeq-pregain').disabled = false;
    preeqFilters.innerHTML = preeqBaseline.filters.map((f, i) => `
      <div class="filter-card" data-filter="${i}">
        <div class="filter-title"><strong>F${i}</strong><label><input type="checkbox" data-field="enabled" ${f.enabled ? 'checked' : ''}> On</label></div>
        <div class="filter-fields">
          <label class="field"><span>Type</span><select data-field="type">${filterTypes.map((t, n) => `<option value="${n}" ${n === f.type ? 'selected' : ''}>${t}</option>`).join('')}</select></label>
          <label class="field"><span>Frequency (Hz)</span><input type="number" data-field="frequency_hz" min="1" max="65535" step="1" value="${f.frequency_hz}"></label>
          <label class="field"><span>Gain (dB)</span><input type="number" data-field="gain_db" step="0.01" value="${f.gain_db.toFixed(2)}"></label>
          <label class="field"><span>Q</span><input type="number" data-field="q" min="0.001" step="0.001" value="${f.q.toFixed(3)}"></label>
        </div>
      </div>`).join('');
    $('btn-preeq-apply').disabled = false;
    $('btn-preeq-read').disabled = false;
    preeqState.textContent = preeqBaseline.enabled ? 'ON · CONFIRMED' : 'OFF · CONFIRMED';
    preeqState.className = 'module-state ' + (preeqBaseline.enabled ? 'is-on' : '');
    preeqEditor.classList.remove('is-dirty');
    drawPreeq();
  }

  function resetPreeqUnread() {
    preeqBaseline = null;
    $('preeq-enable').checked = false;
    $('preeq-enable').disabled = true;
    $('preeq-pregain').value = '';
    $('preeq-pregain').disabled = true;
    $('preeq-pregain').placeholder = 'Not read';
    $('btn-preeq-apply').disabled = true;
    $('btn-preeq-read').disabled = true;
    preeqFilters.innerHTML = '<p class="module-hint">A complete PreEQ readback is required.</p>';
    preeqState.textContent = 'NOT READ';
    preeqState.className = 'module-state';
    preeqEditor.classList.remove('is-dirty');
    drawPreeq();
  }

  function getPreeqForm() {
    return {
      enabled: $('preeq-enable').checked,
      pregain_db: Number($('preeq-pregain').value),
      filters: [...preeqFilters.querySelectorAll('[data-filter]')].map(card => ({
        enabled: card.querySelector('[data-field="enabled"]').checked,
        type: Number(card.querySelector('[data-field="type"]').value),
        frequency_hz: Number(card.querySelector('[data-field="frequency_hz"]').value),
        gain_db: Number(card.querySelector('[data-field="gain_db"]').value),
        q: Number(card.querySelector('[data-field="q"]').value)
      }))
    };
  }

  function changedFilters(now) {
    if (!preeqBaseline) return [];
    return now.filters.flatMap((f, index) => {
      const old = preeqBaseline.filters[index];
      const change = { index };
      let dirty = false;
      for (const key of ['enabled','type','frequency_hz','gain_db','q']) {
        if (f[key] !== old[key]) { change[key] = f[key]; dirty = true; }
      }
      return dirty ? [change] : [];
    });
  }

  function markPreeqDirty() {
    preeqEditor.classList.add('is-dirty');
    preeqMessage.textContent = 'Local preview · not yet applied';
    preeqMessage.className = 'form-message';
    drawPreeq();
  }
  $('preeq-enable').addEventListener('input', markPreeqDirty);
  $('preeq-pregain').addEventListener('input', markPreeqDirty);
  preeqFilters.addEventListener('input', markPreeqDirty);
  preeqFilters.addEventListener('change', markPreeqDirty);

  function biquadMagnitude(filter, frequency) {
    if (!filter.enabled || filter.type > 6) return 0;
    const fs = 48000, w0 = 2 * Math.PI * filter.frequency_hz / fs;
    const q = Math.max(.001, filter.q), A = Math.pow(10, filter.gain_db / 40);
    const alpha = Math.sin(w0) / (2 * q), c = Math.cos(w0);
    let b0, b1, b2, a0, a1, a2;
    if (filter.type === 0) { b0=1+alpha*A; b1=-2*c; b2=1-alpha*A; a0=1+alpha/A; a1=-2*c; a2=1-alpha/A; }
    else if (filter.type === 1 || filter.type === 2) {
      const rootA = Math.sqrt(A), two = 2 * rootA * alpha;
      if (filter.type === 1) { b0=A*((A+1)-(A-1)*c+two); b1=2*A*((A-1)-(A+1)*c); b2=A*((A+1)-(A-1)*c-two); a0=(A+1)+(A-1)*c+two; a1=-2*((A-1)+(A+1)*c); a2=(A+1)+(A-1)*c-two; }
      else { b0=A*((A+1)+(A-1)*c+two); b1=-2*A*((A-1)+(A+1)*c); b2=A*((A+1)+(A-1)*c-two); a0=(A+1)-(A-1)*c+two; a1=2*((A-1)-(A+1)*c); a2=(A+1)-(A-1)*c-two; }
    } else if (filter.type === 3) { b0=(1-c)/2; b1=1-c; b2=(1-c)/2; a0=1+alpha; a1=-2*c; a2=1-alpha; }
    else if (filter.type === 4) { b0=(1+c)/2; b1=-(1+c); b2=(1+c)/2; a0=1+alpha; a1=-2*c; a2=1-alpha; }
    else if (filter.type === 5) { b0=alpha; b1=0; b2=-alpha; a0=1+alpha; a1=-2*c; a2=1-alpha; }
    else { b0=1; b1=-2*c; b2=1; a0=1+alpha; a1=-2*c; a2=1-alpha; }
    const w = 2 * Math.PI * frequency / fs, cw=Math.cos(w), sw=Math.sin(w), c2=Math.cos(2*w), s2=Math.sin(2*w);
    const nr=b0+b1*cw+b2*c2, ni=-(b1*sw+b2*s2), dr=a0+a1*cw+a2*c2, di=-(a1*sw+a2*s2);
    return 20 * Math.log10(Math.max(1e-9, Math.hypot(nr,ni)/Math.hypot(dr,di)));
  }

  function drawCurve(ctx, state, color, dashed) {
    const w=ctx.canvas.width, h=ctx.canvas.height, left=48, right=12, top=12, bottom=28;
    ctx.save(); ctx.strokeStyle=color; ctx.lineWidth=2; ctx.setLineDash(dashed ? [7,5] : []); ctx.beginPath();
    for (let x=left; x<w-right; x++) {
      const f=20*Math.pow(1000,(x-left)/(w-left-right));
      let db=state.pregain_db; state.filters.forEach(filter => db += biquadMagnitude(filter,f));
      const y=top+(18-Math.max(-18,Math.min(18,db)))/36*(h-top-bottom);
      x===left ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
    }
    ctx.stroke(); ctx.restore();
  }

  function drawIndividualCurves(ctx, state) {
    const colors=['#e67e80','#68b5e8','#e2b85b','#b589e8','#67c7b2','#e38ac8','#d6dc70','#8d9aa0','#718e75','#c48b62'];
    state.filters.forEach((filter,index) => {
      if (!filter.enabled || filter.type > 6) return;
      drawCurve(ctx,{pregain_db:0,filters:[filter]},colors[index],true);
    });
  }

  function drawPreeq() {
    const canvas=$('preeq-canvas'), ctx=canvas.getContext('2d'), w=canvas.width, h=canvas.height;
    ctx.clearRect(0,0,w,h); ctx.fillStyle='#121a1d'; ctx.fillRect(0,0,w,h);
    ctx.strokeStyle='#273236'; ctx.fillStyle='#819096'; ctx.font='12px sans-serif';
    for (const db of [-18,-12,-6,0,6,12,18]) { const y=12+(18-db)/36*(h-40); ctx.beginPath(); ctx.moveTo(48,y); ctx.lineTo(w-12,y); ctx.stroke(); ctx.fillText(`${db} dB`,4,y+4); }
    for (const f of [20,50,100,200,500,1000,2000,5000,10000,20000]) { const x=48+Math.log10(f/20)/3*(w-60); ctx.beginPath(); ctx.moveTo(x,12); ctx.lineTo(x,h-28); ctx.stroke(); ctx.fillText(f>=1000?`${f/1000}k`:`${f}`,x-8,h-8); }
    if (preeqBaseline) drawCurve(ctx,preeqBaseline,'#738087',false);
    if (preeqBaseline && preeqFilters.querySelector('[data-filter]')) {
      const preview=getPreeqForm();
      drawIndividualCurves(ctx,preview);
      drawCurve(ctx,preview,'#a8ff35',true);
    }
  }

  $('btn-preeq-read').addEventListener('click', async () => {
    if (!preeqBaseline) return;
    const reset = {
      enabled: true,
      pregain_db: 0,
      filters: preeqBaseline.filters.map((filter, index) =>
        index === 0 ? { ...filter } : { ...factoryPreeqFilters[index] })
    };
    $('preeq-enable').checked = reset.enabled;
    $('preeq-pregain').value = reset.pregain_db.toFixed(2);
    [...preeqFilters.querySelectorAll('[data-filter]')].forEach((card, index) => {
      const filter = reset.filters[index];
      card.querySelector('[data-field="enabled"]').checked = filter.enabled;
      card.querySelector('[data-field="type"]').value = filter.type;
      card.querySelector('[data-field="frequency_hz"]').value = filter.frequency_hz;
      card.querySelector('[data-field="gain_db"]').value = Number(filter.gain_db).toFixed(2);
      card.querySelector('[data-field="q"]').value = Number(filter.q).toFixed(3);
    });
    markPreeqDirty();
    preeqMessage.textContent='Factory values loaded locally for F1–F9; F0/crossover is preserved · Apply to write';
  });

  $('btn-preeq-apply').addEventListener('click', async () => {
    const button=$('btn-preeq-apply'); button.disabled=true; preeqMessage.textContent='Read-modify-write and verification…';
    try {
      const now=getPreeqForm();
      const result=await api('POST','/dsp/preeq',{enable:now.enabled,pregain_db:now.pregain_db,filters:changedFilters(now)});
      if(result.status!=='ok'||!result.data||!result.data.confirmed) throw new Error(result.error||'Readback mismatch');
      const fresh=await api('GET','/dsp'); setPreeqForm(fresh);
      preeqMessage.textContent='Complete PreEQ readback confirmed'; preeqMessage.className='form-message is-success';
    } catch(error) { preeqState.textContent='MISMATCH'; preeqState.className='module-state is-error'; preeqMessage.textContent=error.message; preeqMessage.className='form-message is-error'; }
    finally { button.disabled=false; }
  });

  ['noise-suppressor', 'noise-threshold', 'noise-ratio', 'noise-attack', 'noise-release']
    .forEach(id => $(id).addEventListener('input', () => {
      noiseEditor.classList.add('is-dirty');
      noiseMessage.textContent = 'Unapplied changes';
      noiseMessage.className = 'form-message';
    }));

  $('btn-noise-read').addEventListener('click', async () => {
    $('noise-suppressor').checked = true;
    $('noise-threshold').value = '-55.00';
    $('noise-ratio').value = 4;
    $('noise-attack').value = 2;
    $('noise-release').value = 100;
    noiseEditor.classList.add('is-dirty');
    noiseMessage.textContent = 'Factory values loaded locally · Apply to write';
    noiseMessage.className = 'form-message';
  });

  $('btn-noise-apply').addEventListener('click', async () => {
    const button = $('btn-noise-apply');
    button.disabled = true;
    noiseMessage.textContent = 'Writing and verifying…';
    try {
      const result = await api('POST', '/dsp/noise', {
        enable: $('noise-suppressor').checked,
        threshold_db: Number($('noise-threshold').value),
        ratio: Number($('noise-ratio').value),
        attack_ms: Number($('noise-attack').value),
        release_ms: Number($('noise-release').value)
      });
      if (result.status !== 'ok' || !result.data || !result.data.confirmed) {
        throw new Error(result.error || 'Readback mismatch');
      }
      setNoiseForm({
        noise_suppressor: result.data.enabled,
        noise_threshold_db: result.data.threshold_db,
        noise_ratio: result.data.ratio,
        noise_attack_ms: result.data.attack_ms,
        noise_release_ms: result.data.release_ms
      });
      noiseMessage.textContent = 'Change confirmed by readback';
      noiseMessage.className = 'form-message is-success';
    } catch (error) {
      noiseState.textContent = 'MISMATCH';
      noiseState.className = 'module-state is-error';
      noiseMessage.textContent = error.message;
      noiseMessage.className = 'form-message is-error';
    } finally {
      button.disabled = false;
    }
  });

  // --- Toggle Handlers ---
  async function handleToggle(effect, enable) {
    const endpoint = effect === 'noise' ? '/dsp/noise' :
                     effect === 'bass' ? '/dsp/bass' :
                     effect === 'silence' ? '/dsp/silence' :
                     effect === 'preeq' ? '/dsp/preeq' :
                     effect === 'drc' ? '/dsp/drc' : '';
    if (!endpoint) return;
    const result = await api('POST', endpoint, { enable });
    if (result.status !== 'ok' || !result.data || !result.data.confirmed) {
      throw new Error(result.error || 'DSP readback not confirmed');
    }
    return result.data.enabled;
  }

  document.querySelectorAll('[data-effect]').forEach(el => {
    el.addEventListener('change', async e => {
      const input = e.target;
      const requested = input.checked;
      input.disabled = true;
      try {
        input.checked = await handleToggle(input.dataset.effect, requested);
      } catch (error) {
        input.checked = !requested;
        console.error('DSP write/readback failed:', error);
        alert('DSP change not confirmed: ' + error.message);
      } finally {
        input.disabled = false;
        await updateDspState();
      }
    });
  });

  // --- OTA Firmware Update ---
  let otaSelectedFile = null;

  // File selection
  $('btn-ota-select').addEventListener('click', () => {
    $('ota-file-input').click();
  });

  $('ota-file-input').addEventListener('change', () => {
    const file = $('ota-file-input').files[0];
    if (!file) {
      otaSelectedFile = null;
      $('ota-file-name').textContent = 'No file selected';
      $('ota-file-size').textContent = '';
      $('ota-confirm-row').classList.add('hidden');
      $('ota-new-info').classList.add('hidden');
      return;
    }
    otaSelectedFile = file;
    $('ota-file-name').textContent = file.name;
    $('ota-file-size').textContent = formatBytes(file.size);
    $('ota-confirm-row').classList.remove('hidden');
  });

  // Cancel selection
  $('btn-ota-cancel').addEventListener('click', () => {
    otaSelectedFile = null;
    $('ota-file-input').value = '';
    $('ota-file-name').textContent = 'No file selected';
    $('ota-file-size').textContent = '';
    $('ota-confirm-row').classList.add('hidden');
    $('ota-new-info').classList.add('hidden');
    $('ota-result').classList.add('hidden');
  });

  // Install firmware
  $('btn-ota-install').addEventListener('click', async () => {
    if (!otaSelectedFile) return;

    // Double confirmation
    if (!confirm('Install firmware "' + otaSelectedFile.name + '"?\n\nThe device will restart after installation.')) {
      return;
    }

    const form = $('ota-upload-form');
    const confirmRow = $('ota-confirm-row');
    const progressArea = $('ota-progress-area');
    const resultDiv = $('ota-result');

    // Lock UI
    form.querySelectorAll('button').forEach(b => b.disabled = true);
    confirmRow.querySelectorAll('button').forEach(b => b.disabled = true);
    $('btn-factory-reset').disabled = true;
    progressArea.classList.remove('hidden');
    resultDiv.classList.add('hidden');
    resultDiv.className = '';

    try {
      const xhr = new XMLHttpRequest();

      xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
          const pct = Math.round((e.loaded / e.total) * 100);
          $('ota-progress-pct').textContent = pct + '%';
          $('ota-progress-bytes').textContent =
            formatBytes(e.loaded) + ' / ' + formatBytes(e.total);
          $('ota-progress-fill').style.width = pct + '%';
        }
      });

      xhr.addEventListener('load', () => {
        if (xhr.status === 200) {
          try {
            const resp = JSON.parse(xhr.responseText);
            resultDiv.textContent = '✓ ' + (resp.message || 'Update successful')
              + ' \u2014 Rebooting...';
            resultDiv.className = 'form-message success';
          } catch (_) {
            resultDiv.textContent = '✓ Update successful \u2014 Rebooting...';
            resultDiv.className = 'form-message success';
          }
          resultDiv.classList.remove('hidden');

          // Countdown to reconnect
          let sec = 5;
          const timer = setInterval(() => {
            sec--;
            resultDiv.textContent = '✓ Update successful \u2014 '
              + 'Rebooting... (reconnect in ' + sec + 's)';
            if (sec <= 0) clearInterval(timer);
          }, 1000);
        } else {
          let errMsg = 'Upload failed (HTTP ' + xhr.status + ')';
          try {
            const resp = JSON.parse(xhr.responseText);
            errMsg = resp.error || resp.message || errMsg;
          } catch (_) {}
          resultDiv.textContent = '✗ ' + errMsg;
          resultDiv.className = 'form-message error';
          resultDiv.classList.remove('hidden');
        }

        // Unlock UI (on failure; on success device reboots)
        if (xhr.status !== 200) {
          form.querySelectorAll('button').forEach(b => b.disabled = false);
          confirmRow.querySelectorAll('button').forEach(b => b.disabled = false);
          $('btn-factory-reset').disabled = false;
          progressArea.classList.add('hidden');
        }
      });

      xhr.addEventListener('error', () => {
        resultDiv.textContent = '✗ Network error \u2014 check connection';
        resultDiv.className = 'form-message error';
        resultDiv.classList.remove('hidden');
        form.querySelectorAll('button').forEach(b => b.disabled = false);
        confirmRow.querySelectorAll('button').forEach(b => b.disabled = false);
        $('btn-factory-reset').disabled = false;
        progressArea.classList.add('hidden');
      });

      xhr.addEventListener('abort', () => {
        resultDiv.textContent = '✗ Upload aborted';
        resultDiv.className = 'form-message error';
        resultDiv.classList.remove('hidden');
        form.querySelectorAll('button').forEach(b => b.disabled = false);
        confirmRow.querySelectorAll('button').forEach(b => b.disabled = false);
        $('btn-factory-reset').disabled = false;
        progressArea.classList.add('hidden');
      });

      xhr.open('POST', '/api/ota/upload', true);
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.send(otaSelectedFile);
    } catch (e) {
      resultDiv.textContent = '✗ ' + e.message;
      resultDiv.className = 'form-message error';
      resultDiv.classList.remove('hidden');
      form.querySelectorAll('button').forEach(b => b.disabled = false);
      confirmRow.querySelectorAll('button').forEach(b => b.disabled = false);
      $('btn-factory-reset').disabled = false;
      progressArea.classList.add('hidden');
    }
  });

  // OTA status polling
  async function updateOtaStatus() {
    try {
      const status = await api('GET', '/ota/status');
      $('ota-current-version').textContent = status.current_version || '-';
      $('ota-running-partition').textContent = status.running_partition || '-';

      if (status.uploaded_version) {
        $('ota-new-info').classList.remove('hidden');
        $('ota-new-version').textContent = status.uploaded_version;
        $('ota-target-partition').textContent = status.target_partition || '-';
      }

      if (status.rollback_pending) {
        $('ota-result').textContent = '⚠ App pending verification \u2014 rollback possible';
        $('ota-result').className = 'form-message warning';
        $('ota-result').classList.remove('hidden');
      }
    } catch (_) {
      // Ignore
    }
  }

  // --- Factory Reset ---
  $('btn-factory-reset').addEventListener('click', async () => {
    if (!confirm('Restore factory settings? The device will restart.')) return;
    await api('POST', '/device/reset');
  });

  // --- DSP Configuration Export/Import ---
  let importPreviewData = null;

  $('btn-dsp-export').addEventListener('click', async () => {
    try {
      const data = await api('POST', '/dsp/config/export');
      const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'a800x-dsp-config.json';
      a.click();
    } catch (e) {
      alert('Export failed: ' + e.message);
    }
  });

  function showImportPreview(data) {
    const preview = $('import-preview');
    const content = $('import-preview-content');
    const msg = $('import-message');

    importPreviewData = data;
    preview.classList.remove('hidden');
    msg.textContent = '';
    msg.className = 'form-message';

    if (!data.data || !data.data.dsp) {
      content.innerHTML = '<p class="module-hint">No DSP data in the import file.</p>';
      importPreviewData = null;
      return;
    }

    const dsp = data.data.dsp;
    let html = '<div class="preview-grid">';

    // Noise Suppressor
    const ns = dsp.noise_suppressor || {};
    html += '<div class="preview-item"><strong>Noise Suppressor</strong>';
    html += `<span>${ns.enabled ? 'ON' : 'OFF'}</span>`;
    html += `<span>Threshold: ${ns.threshold_db != null ? Number(ns.threshold_db).toFixed(2) + ' dB' : '-'}</span>`;
    html += `<span>Ratio: ${ns.ratio != null ? ns.ratio : '-'}</span>`;
    html += '</div>';

    // Virtual Bass
    const vb = dsp.virtual_bass || {};
    html += '<div class="preview-item"><strong>Virtual Bass</strong>';
    html += `<span>${vb.enabled ? 'ON' : 'OFF'}</span>`;
    html += `<span>Cutoff: ${vb.cutoff_hz != null ? vb.cutoff_hz + ' Hz' : '-'}</span>`;
    html += `<span>Intensity: ${vb.intensity_pct != null ? vb.intensity_pct + '%' : '-'}</span>`;
    html += '</div>';

    // Silence Detector
    const sd = dsp.silence_detector || {};
    html += '<div class="preview-item"><strong>Silence Detector</strong>';
    html += `<span>${sd.enabled ? 'ON' : 'OFF'}</span>`;
    html += '</div>';

    // PreEQ
    const peq = dsp.preeq || {};
    html += '<div class="preview-item"><strong>PreEQ</strong>';
    html += `<span>${peq.enabled ? 'ON' : 'OFF'}</span>`;
    html += `<span>Pre-Gain: ${peq.pregain_db != null ? Number(peq.pregain_db).toFixed(2) + ' dB' : '-'}</span>`;
    html += '</div>';

    // DRC
    const drc = dsp.drc || {};
    html += '<div class="preview-item"><strong>DRC</strong>';
    html += `<span>${drc.enabled ? 'ON' : 'OFF'}</span>`;
    html += `<span>Mode: ${drc.mode != null ? drc.mode : '-'}</span>`;
    html += '</div>';

    html += '</div>';
    content.innerHTML = html;
  }

  $('btn-dsp-import').addEventListener('click', async () => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async () => {
      try {
        const text = await input.files[0].text();
        const json = JSON.parse(text);
        const result = await api('POST', '/dsp/config/import', json);
        if (result.status !== 'ok') throw new Error(result.error || 'Import validation failed');
        showImportPreview(result);
      } catch (e) {
        alert('Import failed: ' + e.message);
      }
    };
    input.click();
  });

  $('btn-import-cancel').addEventListener('click', () => {
    importPreviewData = null;
    $('import-preview').classList.add('hidden');
  });

  $('btn-import-apply').addEventListener('click', async () => {
    if (!importPreviewData) return;
    const button = $('btn-import-apply');
    const msg = $('import-message');
    button.disabled = true;
    msg.textContent = 'Applying and verifying…';
    msg.className = 'form-message';
    try {
      // Parse the import data into the apply endpoint format
      const dsp = importPreviewData.data.dsp;
      // Build apply payload
      const payload = {
        schema_version: 1,
        type: 'a800x-dsp-config',
        dsp: dsp
      };
      const result = await api('POST', '/dsp/apply', payload);
      if (result.status !== 'ok' || !result.data || !result.data.applied) {
        throw new Error(result.error || 'Apply failed');
      }
      msg.textContent = 'Configuration applied, confirmed, and saved.';
      msg.className = 'form-message is-success';
      importPreviewData = null;
      // Kurz warten, dann DSP-State aktualisieren
      setTimeout(async () => {
        await updateDspState();
      }, 500);
    } catch (e) {
      msg.textContent = e.message;
      msg.className = 'form-message is-error';
    } finally {
      button.disabled = false;
    }
  });

  // --- Init ---
  async function init() {
    await updateStatus();
    await loadWifiConfig();
    await updateDspState();
    await updateOtaStatus();
    setInterval(updateStatus, 5000);
    setInterval(updateDspState, 15000);
    setInterval(updateOtaStatus, 30000);
  }

  document.addEventListener('DOMContentLoaded', init);
})();
