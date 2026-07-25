// BP10 DSP Controller — Web UI Logic
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
  const outeqState = $('outeq-state');
  const outeqMessage = $('outeq-message');
  const outeqEditor = $('outeq-module').querySelector('.module-editor');
  const outeqFilters = $('outeq-filters');
  const usbOutGainState = $('usb-out-gain-state');
  const usbOutGainMessage = $('usb-out-gain-message');
  const usbOutGainEditor = $('usb-out-gain-module').querySelector('.module-editor');
  let preeqBaseline = null;
  let outeqBaseline = null;
  let outeqPending = false;
  let outeqSyncing = false;
  let activeCapabilities = {};
  let activePreeqSchema = 'none';
  let activeDeviceProfile = 'unknown';
  let activePath = 'music';
  let pathCount = 0;
  let drcMode = null;
  let drcVisibleBands = [];
  const drcDraftStates = new Map();
  let renderedDrcKey = null;

  function currentDrcKey() {
    return `${activeDeviceProfile}:${activePath}`;
  }

  function currentDrcDraftState() {
    const key = currentDrcKey();
    if (!drcDraftStates.has(key)) {
      drcDraftStates.set(key, { baseline: null, draft: null, dirty: false });
    }
    return drcDraftStates.get(key);
  }

  function cloneDrcState(data) {
    return data ? JSON.parse(JSON.stringify(data)) : null;
  }

  const factoryValueButtonIds = [
    'btn-noise-read', 'btn-bass-read', 'btn-silence-read',
    'btn-preeq-read', 'btn-drc-reset',
    'btn-vb-classic-read', 'btn-phase-read'
  ];

  function hasA800xFactoryDefaults() {
    return activeDeviceProfile === 'a800x_fixed';
  }

  function updateFactoryValueActions() {
    const available = hasA800xFactoryDefaults();
    factoryValueButtonIds.forEach(id => {
      const button = $(id);
      button.classList.toggle('hidden', !available);
      if (!available) button.disabled = true;
    });
  }

  function toggleModule(id, available) {
    const module = $(id);
    if (module) module.classList.toggle('hidden', available !== true);
  }

  const dspModuleIds = [
    'noise-module', 'bass-module', 'vb-classic-module', 'phase-module',
    'delay-module', 'silence-module', 'preeq-module', 'outeq-module',
    'drc-module', 'usb-out-gain-module'
  ];

  function hideAllDspModules() {
    dspModuleIds.forEach(id => toggleModule(id, false));
  }
  hideAllDspModules();
  // Known fixed A800X values only; Generic ACP has no universal defaults.
  const a800xFactoryPreeqFilters = [
    null, // F0 gehört zum rückseitigen Crossover und wird immer erhalten.
    { enabled:true,  type:3, frequency_hz:500,   q:0.707, gain_db:0 },
    { enabled:true,  type:4, frequency_hz:35,    q:0.800, gain_db:0 },
    { enabled:true,  type:0, frequency_hz:55,    q:3.500, gain_db:1.5 },
    { enabled:true,  type:0, frequency_hz:85,    q:3.500, gain_db:1.5 },
    { enabled:false, type:0, frequency_hz:20000, q:0.707, gain_db:0 },
    { enabled:false, type:0, frequency_hz:20000, q:0.707, gain_db:0 },
    { enabled:false, type:0, frequency_hz:20000, q:0.707, gain_db:0 },
    { enabled:false, type:0, frequency_hz:20000, q:0.707, gain_db:0 },
    { enabled:false, type:0, frequency_hz:20000, q:0.707, gain_db:0 }
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

  let dspRequestId = 0;

  function withActivePath(url) {
    const separator = url.includes('?') ? '&' : '?';
    return `${url}${separator}path=${encodeURIComponent(activePath)}`;
  }

  function applyPathCapabilities(pathCaps) {
    toggleModule('noise-module', pathCaps.noise_suppressor);
    toggleModule('bass-module', pathCaps.virtual_bass);
    toggleModule('vb-classic-module', pathCaps.virtual_bass_classic);
    toggleModule('phase-module', pathCaps.phase);
    toggleModule('delay-module', pathCaps.delay);
    toggleModule('preeq-module', pathCaps.preeq);
    toggleModule('outeq-module', pathCaps.out_eq);
    toggleModule('drc-module', pathCaps.drc);
    toggleModule('usb-out-gain-module', pathCaps.usb_out_gain);
    toggleModule('silence-module',
      activePath === 'music' && pathCaps.silence_detector);
    activePreeqSchema = pathCaps.preeq_schema || 'none';
    activeCapabilities.drc_schema = pathCaps.drc_schema || 'none';
    const legacyDrcRatio = $('drc-ratio');
    if (legacyDrcRatio) {
      legacyDrcRatio.step = String(pathCaps.drc_ratio_step || 0.01);
    }
  }

  function refreshPathCapabilities() {
    if (!activeCapabilities._pathMap || pathCount < 1) return;
    const pathCaps = activeCapabilities._pathMap[activePath];
    if (pathCaps) applyPathCapabilities(pathCaps);
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
        // Controls remain hidden until /dsp has returned confirmed hardware
        // values. Showing unchecked HTML defaults here causes OFF -> ON jumps.
        hideAllDspModules();
        activeCapabilities = data.capabilities || {};
        activeDeviceProfile = data.device && data.device.profile || 'unknown';
        activePreeqSchema = activeCapabilities.preeq_schema || 'none';
        updateFactoryValueActions();

        // --- Path Selector (nur Generic Multi-Path) ---
        pathCount = (data.paths && data.paths.count) || 0;
        const isA800X = activeDeviceProfile === 'a800x_fixed';
        const pathSelector = $('path-selector');
        if (pathCount > 1 && !isA800X) {
          pathSelector.classList.remove('hidden');
          $('btn-path-music').className = activePath === 'music'
            ? 'btn btn-secondary active' : 'btn btn-secondary';
          $('btn-path-rec').className = activePath === 'rec'
            ? 'btn btn-secondary active' : 'btn btn-secondary';
        } else {
          pathSelector.classList.add('hidden');
        }

        // --- Per-Path Capabilities ---
        if (data.paths && data.paths.available) {
          activeCapabilities._pathMap = {};
          data.paths.available.forEach(p => {
            const key = String(p.key || p.label || '').toLowerCase();
            activeCapabilities._pathMap[key] = p.capabilities || {};
          });
          if (pathCount === 1 && data.paths.available.length === 1) {
            activePath = String(
              data.paths.available[0].key ||
              data.paths.available[0].label ||
              'music'
            ).toLowerCase();
          }
          refreshPathCapabilities();
        } else {
          // Legacy fallback: globale Capabilities
          toggleModule('noise-module', activeCapabilities.noise_suppressor);
          toggleModule('bass-module', activeCapabilities.virtual_bass);
          toggleModule('vb-classic-module', activeCapabilities.virtual_bass_classic);
          toggleModule('phase-module', activeCapabilities.music_phase);
          toggleModule('delay-module', activeCapabilities.music_delay);
          toggleModule('silence-module', activeCapabilities.silence_detector);
          toggleModule('preeq-module', activeCapabilities.preeq);
          toggleModule('drc-module', activeCapabilities.drc);
          toggleModule('outeq-module', false);
          toggleModule('usb-out-gain-module', false);
        }
        const legacyDrcRatio = $('drc-ratio');
        if (legacyDrcRatio) {
          legacyDrcRatio.step =
            String(activeCapabilities.drc_ratio_step || 0.01);
        }
        const normalizedPersistence = data.device &&
          (data.device.profile === 'a800x_fixed' ||
           (data.device.profile === 'generic_acp_classic' &&
            data.device.fingerprint_valid));
        $('btn-dsp-export').disabled = !normalizedPersistence;
        $('btn-dsp-import').disabled = !normalizedPersistence;
      } else {
        activeDeviceProfile = 'unknown';
        updateFactoryValueActions();
        dspStatus.textContent = 'DSP ✗';
        dspStatus.className = 'status-dot dot-off';
        dspSection.classList.add('hidden');
        if (dspUnavailable) dspUnavailable.classList.remove('hidden');
        resetSilenceUnread();
        resetPreeqUnread();
        resetDrcUnread();
        hideAllDspModules();
        $('btn-dsp-export').disabled = true;
        $('btn-dsp-import').disabled = true;
      }

      // WiFi-Status: AP + STA getrennt
      updateWifiStatusBar(data);

      // System-Info
      sysHostname.textContent = data.hostname || '-';
      sysMac.textContent = data.mac || '-';
      sysMdns.textContent = data.mdns_address || '-';
      sysApRow.classList.remove('hidden');
      sysApIp.textContent = data.ap_ip || '192.168.4.1';

      // AP state only controls the transition banner. The configured AP
      // address remains visible in the system summary when SoftAP is off.
      if (data.ap_active) {
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
      wifiFormMessage.textContent = 'Saved — connecting… The setup network will close after a successful connection.';
      wifiFormMessage.className = 'form-message';
      await waitForWifiConnection(result.data && result.data.mdns_address);
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

  async function waitForWifiConnection(mdnsAddress) {
    let transitionStarted = false;
    for (let attempt = 0; attempt < 20; attempt += 1) {
      await new Promise(resolve => setTimeout(resolve, 750));
      try {
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
      } catch (error) {
        // Losing 192.168.4.1 is the expected success-path transition: GOT_IP
        // stops the setup AP. Do not turn that into a red "Failed to fetch".
        if (error instanceof TypeError || /fetch|network/i.test(error.message || '')) {
          transitionStarted = true;
          wifiFormMessage.textContent = 'Setup network closed; checking the home-network connection. ';
          if (mdnsAddress) {
            const link = document.createElement('a');
            link.href = mdnsAddress;
            link.textContent = 'Open the controller after your device rejoins the home Wi-Fi.';
            wifiFormMessage.appendChild(link);
          } else {
            wifiFormMessage.appendChild(document.createTextNode(
              'Rejoin your home Wi-Fi and open the controller there.'));
          }
          wifiFormMessage.className = 'form-message is-success';
          continue;
        }
        throw error;
      }
    }
    if (transitionStarted) return;
    throw new Error('Connection is taking longer than expected; Setup AP remains available');
  }

  // --- DSP State (nur bei Connected ausgeführt) ---
  async function updateDspState() {
    try {
      const reqId = ++dspRequestId;
      const data = await api('GET', withActivePath('/dsp/state'));
      // Stale response: Pfadwechsel während Request
      if (reqId !== dspRequestId || data.path !== activePath) return;

      if (data.dsp === 'nicht verfügbar' || data.dsp === 'unavailable') {
        dspSection.classList.add('hidden');
        if (dspUnavailable) dspUnavailable.classList.remove('hidden');
        return;
      }

      dspSection.classList.remove('hidden');
      if (dspUnavailable) dspUnavailable.classList.add('hidden');

      if (data.noise_suppressor && data.noise_suppressor.valid &&
          !noiseEditor.classList.contains('is-dirty')) {
        setNoiseForm({
          noise_suppressor: data.noise_suppressor.enabled,
          noise_threshold_db: data.noise_suppressor.threshold_db,
          noise_ratio: data.noise_suppressor.ratio,
          noise_attack_ms: data.noise_suppressor.attack_ms,
          noise_release_ms: data.noise_suppressor.release_ms
        });
      }
      if (data.virtual_bass && data.virtual_bass.valid &&
          !bassEditor.classList.contains('is-dirty')) {
        setBassForm({
          virtual_bass: data.virtual_bass.enabled,
          bass_cutoff_hz: data.virtual_bass.cutoff_hz,
          bass_intensity_pct: data.virtual_bass.intensity_pct,
          bass_enhanced: data.virtual_bass.bass_enhanced
        });
      }
      if (data.silence && data.silence.valid) {
        if (!silenceEditor.classList.contains('is-dirty'))
          setSilenceForm(data.silence.enabled);
      } else {
        resetSilenceUnread();
      }
      // PreEQ: immer in /dsp/state, flache Felder
      if (data.preeq && data.preeq.valid &&
          !preeqEditor.classList.contains('is-dirty')) {
        setPreeqForm({
          preeq_enabled: data.preeq.enabled,
          preeq_pregain_db: data.preeq.pregain_db,
          preeq_filters: data.preeq.filters
        });
      } else if (!data.preeq || !data.preeq.valid) {
        resetPreeqUnread();
      }

      // Out EQ
      if (data.out_eq && data.out_eq.valid) {
        if (!outeqEditor.classList.contains('is-dirty')) setOutEqForm(data.out_eq);
      } else {
        resetOutEqUnread();
      }

      if (data.drc && data.drc.valid === true) adoptDrcReadback(data.drc);
      else resetDrcUnread();
      if (data.virtual_bass_classic && data.virtual_bass_classic.valid)
        setVbClassicForm(data.virtual_bass_classic);
      if (data.phase && data.phase.valid)
        setPhaseForm(data.phase);
      if (data.delay && data.delay.valid)
        setDelayForm(data.delay);
      // USB Out Gain
      if (data.usb_out_gain && data.usb_out_gain.valid)
        setUsbOutGainForm(data.usb_out_gain);
      else
        resetUsbOutGainUnread();
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

  async function applyWithReadback(apiPath, payload, opts) {
    const { button, message, state, editor, formSetter } = opts;
    button.disabled = true;
    message.textContent = 'Writing and verifying…';
    try {
      const result = await api('POST', withActivePath(apiPath), payload);
      if (result.status !== 'ok' || !result.data || !result.data.confirmed) {
        throw new Error(result.error || 'Readback mismatch');
      }
      formSetter(result.data);
      editor.classList.remove('is-dirty');
      message.textContent = 'Change confirmed by readback';
      message.className = 'form-message is-success';
    } catch (error) {
      state.textContent = 'MISMATCH';
      state.className = 'module-state is-error';
      message.textContent = error.message;
      message.className = 'form-message is-error';
    } finally { button.disabled = false; }
  }

  // --- Path Switch Handler ---
  async function switchPath(newPath) {
    if (activePath === newPath || pathCount <= 1) return;
    $('btn-path-music').disabled = true;
    $('btn-path-rec').disabled = true;
    activePath = newPath;
    ++dspRequestId;
    hideAllDspModules();
    $('btn-path-music').className = activePath === 'music'
      ? 'btn btn-secondary active' : 'btn btn-secondary';
    $('btn-path-rec').className = activePath === 'rec'
      ? 'btn btn-secondary active' : 'btn btn-secondary';
    $('btn-path-music').disabled = false;
    $('btn-path-rec').disabled = false;
    refreshPathCapabilities();
    resetPreeqUnread();
    resetOutEqUnread();
    resetDrcUnread();
    resetSilenceUnread();
    await updateDspState();
  }
  $('btn-path-music').addEventListener('click', () => switchPath('music'));
  $('btn-path-rec').addEventListener('click', () => switchPath('rec'));

  // --- Out EQ Form Functions ---
  function cloneOutEq(data) {
    return {
      enabled: data.enabled === true,
      pregain_db: Number(data.pregain_db),
      filters: (data.filters || []).map(f => ({
        enabled: f.enabled === true, type: Number(f.type),
        frequency_hz: Number(f.frequency_hz), q: Number(f.q), gain_db: Number(f.gain_db)
      }))
    };
  }

  function setOutEqForm(data) {
    if (!Array.isArray(data.filters) || data.filters.length !== 10) return;
    outeqSyncing = true;
    try {
      outeqBaseline = cloneOutEq(data);
      const filterTypes = activePreeqSchema === 'classic_10band'
        ? classicFilterTypes : a800xFilterTypes;
      $('outeq-enable').checked = outeqBaseline.enabled;
      $('outeq-enable').disabled = false;
      $('outeq-pregain').value = outeqBaseline.pregain_db.toFixed(2);
      $('outeq-pregain').disabled = false;
      outeqFilters.innerHTML = outeqBaseline.filters.map((f, i) => `
        <div class="filter-card" data-filter="${i}">
          <div class="filter-title"><strong>F${i}</strong><label><input type="checkbox" data-field="enabled" ${f.enabled ? 'checked' : ''}> On</label></div>
          <div class="filter-fields">
            <label class="field"><span>Type</span><select data-field="type">${filterTypes.map((t, n) => `<option value="${n}" ${n === f.type ? 'selected' : ''}>${t}</option>`).join('')}</select></label>
            <label class="field"><span>Frequency (Hz)</span><input type="number" data-field="frequency_hz" min="1" max="65535" step="1" value="${f.frequency_hz}"></label>
            <label class="field"><span>Gain (dB)</span><input type="number" data-field="gain_db" step="0.01" value="${f.gain_db.toFixed(2)}"></label>
            <label class="field"><span>Q</span><input type="number" data-field="q" min="0.001" step="0.001" value="${f.q.toFixed(3)}"></label>
          </div>
        </div>`).join('');
      $('btn-outeq-apply').disabled = false;
      outeqState.textContent = outeqBaseline.enabled ? 'ON · CONFIRMED' : 'OFF · CONFIRMED';
      outeqState.className = 'module-state ' + (outeqBaseline.enabled ? 'is-on' : '');
      outeqPending = false;
      outeqEditor.classList.remove('is-dirty');
      drawOutEq();
    } finally {
      outeqSyncing = false;
    }
  }

  function adoptConfirmedOutEqReadback(state) {
    if (!state || !state.valid || !Array.isArray(state.filters) ||
        state.filters.length !== 10) {
      throw new Error('Complete Out EQ readback missing after verification');
    }
    setOutEqForm(state);
    outeqMessage.textContent = 'Complete Out EQ readback confirmed';
    outeqMessage.className = 'form-message is-success';
    drawOutEq();
  }

  function resetOutEqUnread() {
    outeqBaseline = null;
    $('outeq-enable').checked = false;
    $('outeq-enable').disabled = true;
    $('outeq-pregain').value = '';
    $('outeq-pregain').disabled = true;
    $('outeq-pregain').placeholder = 'Not read';
    $('btn-outeq-apply').disabled = true;
    outeqFilters.innerHTML = '<p class="module-hint">A complete Out EQ readback is required.</p>';
    outeqState.textContent = 'NOT READ';
    outeqState.className = 'module-state';
    outeqMessage.textContent = '';
    outeqMessage.className = 'form-message';
    outeqPending = false;
    outeqEditor.classList.remove('is-dirty');
    drawOutEq();
  }

  function getOutEqForm() {
    return {
      enabled: $('outeq-enable').checked,
      pregain_db: Number($('outeq-pregain').value),
      filters: [...outeqFilters.querySelectorAll('[data-filter]')].map(card => ({
        enabled: card.querySelector('[data-field="enabled"]').checked,
        type: Number(card.querySelector('[data-field="type"]').value),
        frequency_hz: Number(card.querySelector('[data-field="frequency_hz"]').value),
        gain_db: Number(card.querySelector('[data-field="gain_db"]').value),
        q: Number(card.querySelector('[data-field="q"]').value)
      }))
    };
  }

  function changedOutEqFilters(now) {
    if (!outeqBaseline) return [];
    return now.filters.flatMap((f, index) => {
      const old = outeqBaseline.filters[index];
      const change = { index };
      let dirty = false;
      for (const key of ['enabled','type','frequency_hz','gain_db','q']) {
        if (f[key] !== old[key]) { change[key] = f[key]; dirty = true; }
      }
      return dirty ? [change] : [];
    });
  }

  function markOutEqDirty() {
    if (outeqSyncing) return;
    outeqPending = true;
    outeqEditor.classList.add('is-dirty');
    outeqMessage.textContent = 'Local preview · not yet applied';
    outeqMessage.className = 'form-message';
    drawOutEq();
  }

  function drawOutEq() {
    const canvas=$('outeq-canvas'), ctx=canvas.getContext('2d'), w=canvas.width, h=canvas.height;
    ctx.clearRect(0,0,w,h); ctx.fillStyle='#121a1d'; ctx.fillRect(0,0,w,h);
    ctx.strokeStyle='#273236'; ctx.fillStyle='#819096'; ctx.font='12px sans-serif';
    for (const db of [-18,-12,-6,0,6,12,18]) { const y=12+(18-db)/36*(h-40); ctx.beginPath(); ctx.moveTo(48,y); ctx.lineTo(w-12,y); ctx.stroke(); ctx.fillText(`${db} dB`,4,y+4); }
    for (const f of [20,50,100,200,500,1000,2000,5000,10000,20000]) { const x=48+Math.log10(f/20)/3*(w-60); ctx.beginPath(); ctx.moveTo(x,12); ctx.lineTo(x,h-28); ctx.stroke(); ctx.fillText(f>=1000?`${f/1000}k`:`${f}`,x-8,h-8); }
    if (outeqBaseline) drawCurve(ctx,outeqBaseline,'#738087',false);
    if (outeqBaseline && outeqFilters.querySelector('[data-filter]')) {
      const preview=getOutEqForm();
      drawIndividualCurves(ctx,preview);
      drawCurve(ctx,preview,'#a8ff35',true);
    }
  }

  $('outeq-enable').addEventListener('input', markOutEqDirty);
  $('outeq-pregain').addEventListener('input', markOutEqDirty);
  outeqFilters.addEventListener('input', markOutEqDirty);
  outeqFilters.addEventListener('change', markOutEqDirty);

  $('btn-outeq-apply').addEventListener('click', async () => {
    const button=$('btn-outeq-apply'); button.disabled=true; outeqMessage.textContent='Read-modify-write and verification…';
    try {
      const reqId = ++dspRequestId;
      const now=getOutEqForm();
      const result=await api('POST',withActivePath('/dsp/outeq'),{
        enable:now.enabled,pregain_db:now.pregain_db,filters:changedOutEqFilters(now)
      });
      if (reqId !== dspRequestId) return;
      if(result.status!=='ok'||!result.data||!result.data.confirmed) throw new Error(result.error||'Readback mismatch');
      const fresh=await api('GET',withActivePath('/dsp/state'));
      if (reqId !== dspRequestId) return;
      adoptConfirmedOutEqReadback(fresh.out_eq);
    } catch(error) { outeqState.textContent='MISMATCH'; outeqState.className='module-state is-error'; outeqMessage.textContent=error.message; outeqMessage.className='form-message is-error'; }
    finally { button.disabled=false; }
  });

  // --- USB Out Gain Form Functions ---
  function setUsbOutGainForm(data) {
    $('usb-out-gain-db').value = Number(data.gain_db).toFixed(2);
    $('usb-out-gain-db').disabled = false;
    $('btn-usb-out-gain-apply').disabled = false;
    usbOutGainState.textContent = Number(data.gain_db).toFixed(2) + ' dB · CONFIRMED';
    usbOutGainState.className = 'module-state';
    usbOutGainEditor.classList.remove('is-dirty');
  }

  function resetUsbOutGainUnread() {
    $('usb-out-gain-db').value = '';
    $('usb-out-gain-db').disabled = true;
    $('usb-out-gain-db').placeholder = 'Not read';
    $('btn-usb-out-gain-apply').disabled = true;
    usbOutGainState.textContent = 'NOT READ';
    usbOutGainState.className = 'module-state';
    usbOutGainEditor.classList.remove('is-dirty');
  }

  $('usb-out-gain-db').addEventListener('input', () => {
    usbOutGainEditor.classList.add('is-dirty');
    usbOutGainMessage.textContent = 'Unapplied change';
    usbOutGainMessage.className = 'form-message';
  });

  $('btn-usb-out-gain-apply').addEventListener('click', () => applyWithReadback('/dsp/usb-out-gain', {
    gain_db: Number($('usb-out-gain-db').value)
  }, {
    button: $('btn-usb-out-gain-apply'),
    message: usbOutGainMessage,
    state: usbOutGainState,
    editor: usbOutGainEditor,
    formSetter: setUsbOutGainForm
  }));

  function setVbClassicForm(data) {
    $('vb-classic-enable').checked = data.enabled === true;
    $('vb-classic-cutoff').value = data.cutoff_hz;
    $('vb-classic-intensity').value = data.intensity_pct;
    $('vb-classic-state').textContent = (data.enabled ? 'ON' : 'OFF') + ' · CONFIRMED';
    $('vb-classic-state').className = 'module-state ' + (data.enabled ? 'is-on' : '');
  }
  ['vb-classic-enable','vb-classic-cutoff','vb-classic-intensity']
    .forEach(id => $(id).addEventListener('input', () => {
      $('vb-classic-module').querySelector('.module-editor').classList.add('is-dirty');
      $('vb-classic-message').textContent = 'Unapplied changes';
      $('vb-classic-message').className = 'form-message';
    }));
  $('btn-vb-classic-read').addEventListener('click', () => {
    if (!hasA800xFactoryDefaults()) return;
    $('vb-classic-enable').checked = false;
    $('vb-classic-cutoff').value = 100;
    $('vb-classic-intensity').value = 35;
    $('vb-classic-module').querySelector('.module-editor').classList.add('is-dirty');
    $('vb-classic-message').textContent = 'Factory values loaded locally · Apply to write';
    $('vb-classic-message').className = 'form-message';
  });
  $('btn-vb-classic-apply').addEventListener('click', () => applyWithReadback('/dsp/vb-classic', {
    enable: $('vb-classic-enable').checked,
    cutoff_hz: Number($('vb-classic-cutoff').value),
    intensity_pct: Number($('vb-classic-intensity').value)
  }, {
    button: $('btn-vb-classic-apply'),
    message: $('vb-classic-message'),
    state: $('vb-classic-state'),
    editor: $('vb-classic-module').querySelector('.module-editor'),
    formSetter: setVbClassicForm
  }));

  function setPhaseForm(data) {
    $('phase-invert').checked = data.inverted === true;
    $('phase-state').textContent = (data.inverted ? 'INVERTED' : 'NORMAL') + ' · CONFIRMED';
    $('phase-state').className = 'module-state ' + (data.inverted ? 'is-on' : '');
  }
  $('phase-invert').addEventListener('input', () => {
    $('phase-module').querySelector('.module-editor').classList.add('is-dirty');
    $('phase-message').textContent = 'Unapplied change';
    $('phase-message').className = 'form-message';
  });
  $('btn-phase-read').addEventListener('click', () => {
    if (!hasA800xFactoryDefaults()) return;
    $('phase-invert').checked = true;
    $('phase-module').querySelector('.module-editor').classList.add('is-dirty');
    $('phase-message').textContent = 'Factory values loaded locally · Apply to write';
    $('phase-message').className = 'form-message';
  });
  $('btn-phase-apply').addEventListener('click', () => applyWithReadback('/dsp/phase', {
    invert: $('phase-invert').checked
  }, {
    button: $('btn-phase-apply'),
    message: $('phase-message'),
    state: $('phase-state'),
    editor: $('phase-module').querySelector('.module-editor'),
    formSetter: setPhaseForm
  }));

  function setDelayForm(data) {
    $('delay-enable').checked = data.enabled === true;
    $('delay-ms').value = data.delay_ms;
    $('delay-hq').checked = data.hq_enabled === true;
    $('delay-state').textContent = (data.enabled ? 'ON' : 'OFF') + ' · CONFIRMED';
    $('delay-state').className = 'module-state ' + (data.enabled ? 'is-on' : '');
  }
  ['delay-enable','delay-ms','delay-hq']
    .forEach(id => $(id).addEventListener('input', () => {
      $('delay-module').querySelector('.module-editor').classList.add('is-dirty');
      $('delay-message').textContent = 'Unapplied changes';
      $('delay-message').className = 'form-message';
    }));
  $('btn-delay-apply').addEventListener('click', () => applyWithReadback('/dsp/delay', {
    enable: $('delay-enable').checked,
    delay_ms: Number($('delay-ms').value),
    hq_enabled: $('delay-hq').checked
  }, {
    button: $('btn-delay-apply'),
    message: $('delay-message'),
    state: $('delay-state'),
    editor: $('delay-module').querySelector('.module-editor'),
    formSetter: setDelayForm
  }));

  ['virtual-bass', 'bass-cutoff', 'bass-intensity', 'bass-enhanced']
    .forEach(id => $(id).addEventListener('input', () => {
      bassEditor.classList.add('is-dirty');
      bassMessage.textContent = 'Unapplied changes';
      bassMessage.className = 'form-message';
    }));

  $('btn-bass-read').addEventListener('click', async () => {
    if (!hasA800xFactoryDefaults()) return;
    $('virtual-bass').checked = true;
    $('bass-cutoff').value = 42;
    $('bass-intensity').value = 4;
    $('bass-enhanced').checked = true;
    bassEditor.classList.add('is-dirty');
    bassMessage.textContent = 'Factory values loaded locally · Apply to write';
    bassMessage.className = 'form-message';
  });

  $('btn-bass-apply').addEventListener('click', async () => {
    const button = $('btn-bass-apply');
    button.disabled = true;
    bassMessage.textContent = 'Writing and verifying…';
    try {
      const result = await api('POST', withActivePath('/dsp/bass'), {
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

  function renderDrcForm(data, dirty) {
    const bandLabels = { lower: 'Lower Band', upper: 'Upper Band', full: 'Full Band' };
    drcMode = Number(data.mode);
    drcVisibleBands = [];
    if (data.lower_upper_visible === true) drcVisibleBands.push('lower', 'upper');
    if (data.full_band_supported === true) drcVisibleBands.push('full');
    $('drc-enable').checked = data.enabled === true;
    $('drc-enable').disabled = drcVisibleBands.length === 0;
    $('drc-mode').value = String(drcMode);
    $('drc-mode').disabled = drcMode < 0 || drcMode > 6;
    $('drc-mode-label').textContent = `Mode ${drcMode}`;
    $('drc-crossover-fields').classList.toggle('hidden', data.crossover_visible !== true);
    $('drc-crossover').disabled = data.crossover_visible !== true;
    $('drc-crossover').value = data.crossover_hz;
    ['lp', 'hp'].forEach(kind => {
      $(`drc-q-${kind}-field`).classList.toggle('hidden', data.q_visible !== true);
      $(`drc-q-${kind}`).disabled = data.q_visible !== true;
      $(`drc-q-${kind}`).value = Number(data[`q_${kind}`]).toFixed(3);
    });
    $('drc-bands').innerHTML = drcVisibleBands.map(name => {
      const band = data.bands[name];
      return `<fieldset class="module-card drc-band" data-drc-band="${name}">
        <legend>${bandLabels[name]}</legend>
        <div class="parameter-grid dsp-param-grid">
          ${name !== 'full' || data.lower_upper_visible !== true ? `<label class="field"><span>Pre-Gain (dB)</span><input type="number" data-key="pregain_db" step="0.01" value="${Number(band.pregain_db).toFixed(2)}"></label>` : ''}
          <label class="field"><span>Threshold (dB)</span><input type="number" data-key="threshold_db" step="0.01" value="${Number(band.threshold_db).toFixed(2)}"></label>
          <label class="field"><span>Ratio</span><input type="number" data-key="ratio" step="1" value="${Number(band.ratio).toFixed(0)}"></label>
          <label class="field"><span>Attack (ms)</span><input type="number" data-key="attack_ms" step="1" value="${band.attack_ms}"></label>
          <label class="field"><span>Release (ms)</span><input type="number" data-key="release_ms" step="1" value="${band.release_ms}"></label>
        </div></fieldset>`;
    }).join('');
    $('drc-bands').querySelectorAll('input').forEach(input =>
      input.addEventListener('input', markDrcDirty));
    $('btn-drc-reset').disabled = !hasA800xFactoryDefaults();
    $('btn-drc-apply').disabled = drcVisibleBands.length === 0;
    renderedDrcKey = currentDrcKey();
    drcEditor.classList.toggle('is-dirty', dirty === true);
    if (!dirty) {
      drcMessage.textContent = drcVisibleBands.length ? '' : 'Unknown mode; complete state was read without enabling writes.';
      drcMessage.className = 'form-message' + (drcVisibleBands.length ? '' : ' is-error');
    }
  }

  function updateDrcReadbackStatus(data) {
    const mode = Number(data.mode);
    const hasWritableBands = data.lower_upper_visible === true ||
      data.full_band_supported === true;
    drcState.textContent = hasWritableBands
      ? (data.enabled ? 'ON · CONFIRMED' : 'OFF · CONFIRMED')
      : `MODE ${mode} · READ ONLY`;
    drcState.className = 'module-state ' + (data.enabled ? 'is-on' : '');
  }

  function adoptDrcReadback(data, applied) {
    const state = currentDrcDraftState();
    state.baseline = cloneDrcState(data);
    updateDrcReadbackStatus(data);
    if (applied === true) {
      state.draft = cloneDrcState(data);
      state.dirty = false;
      renderDrcForm(state.draft, false);
      return;
    }
    if (state.dirty) {
      // Polls may refresh the confirmed status/baseline, but the local draft
      // remains the form's source of truth until Apply + Verify succeeds.
      if (renderedDrcKey !== currentDrcKey() && state.draft)
        renderDrcForm(state.draft, true);
      return;
    }
    state.draft = cloneDrcState(data);
    renderDrcForm(state.draft, false);
  }

  function resetDrcUnread() {
    ['drc-enable','drc-mode','drc-crossover','drc-q-lp','drc-q-hp']
      .forEach(id => { $(id).disabled = true; if (id !== 'drc-enable') $(id).value = ''; });
    $('drc-enable').checked = false;
    $('drc-bands').innerHTML = '';
    $('drc-crossover-fields').classList.add('hidden');
    $('drc-mode-label').textContent = 'Mode not read';
    drcMode = null;
    drcVisibleBands = [];
    $('btn-drc-reset').disabled = !hasA800xFactoryDefaults();
    $('btn-drc-apply').disabled = true;
    drcState.textContent = 'NOT READ';
    drcState.className = 'module-state';
    drcEditor.classList.remove('is-dirty');
    renderedDrcKey = null;
  }

  function captureDrcDraft() {
    const state = currentDrcDraftState();
    const draft = cloneDrcState(state.draft || state.baseline);
    if (!draft) return null;
    draft.enabled = $('drc-enable').checked;
    draft.mode = drcMode;
    draft.crossover_hz = Number($('drc-crossover').value);
    draft.q_lp = Number($('drc-q-lp').value);
    draft.q_hp = Number($('drc-q-hp').value);
    drcVisibleBands.forEach(name => {
      const root = $('drc-bands').querySelector(`[data-drc-band="${name}"]`);
      if (!root || !draft.bands || !draft.bands[name]) return;
      root.querySelectorAll('[data-key]').forEach(input => {
        draft.bands[name][input.dataset.key] = Number(input.value);
      });
    });
    return draft;
  }

  function markDrcDirty() {
    const state = currentDrcDraftState();
    state.draft = captureDrcDraft();
    state.dirty = true;
    drcEditor.classList.add('is-dirty');
    drcMessage.textContent = 'Unapplied changes';
    drcMessage.className = 'form-message';
  }
  ['drc-enable','drc-crossover','drc-q-lp','drc-q-hp']
    .forEach(id => $(id).addEventListener('input', markDrcDirty));

  $('drc-mode').addEventListener('change', async () => {
    const mode = Number($('drc-mode').value);
    $('drc-mode').disabled = true;
    drcMessage.textContent = 'Writing mode selector 0x02 and reading back…';
    drcMessage.className = 'form-message';
    try {
      const result = await api('POST', withActivePath('/dsp/drc'), {
        mode,
        mode_only: true
      });
      if (result.status !== 'ok' || !result.data || !result.data.confirmed) {
        throw new Error(result.error || 'Mode readback mismatch');
      }
      adoptDrcReadback(result.data, true);
      drcMessage.textContent = 'Mode change confirmed by full readback';
      drcMessage.className = 'form-message is-success';
    } catch (error) {
      drcMessage.textContent = error.message;
      drcMessage.className = 'form-message is-error';
      try {
        const reread = await api('GET', withActivePath('/dsp/state'));
        if (reread.status === 'ok' && reread.data && reread.data.drc)
          adoptDrcReadback(reread.data.drc, true);
      } catch (_) {}
    } finally {
      $('drc-mode').disabled = drcMode == null || drcMode < 0 || drcMode > 6;
    }
  });

  $('btn-drc-reset').addEventListener('click', () => {
    if (!hasA800xFactoryDefaults()) return;
    // Build mode-0 Full-Band factory view and re-render the entire form
    const factory = {
      enabled: true,
      mode: 0,
      lower_upper_visible: false,
      full_band_supported: true,
      crossover_visible: false,
      q_visible: false,
      crossover_hz: 300,
      q_lp: 0.707,
      q_hp: 0.707,
      bands: {
        lower:  { pregain_db: 0, threshold_db: 0, ratio: 1, attack_ms: 2, release_ms: 100 },
        upper:  { pregain_db: 0, threshold_db: 0, ratio: 1, attack_ms: 2, release_ms: 100 },
        full:   { pregain_db: 2.00, threshold_db: -5.00, ratio: 1, attack_ms: 2, release_ms: 800 }
      }
    };
    adoptDrcReadback(factory, true);
    drcMessage.textContent = 'Full-Band factory values loaded locally · Apply to write';
    drcMessage.className = 'form-message';
  });

  $('btn-drc-apply').addEventListener('click', async () => {
    const button = $('btn-drc-apply');
    button.disabled = true;
    drcMessage.textContent = 'Read-modify-write and verification…';
    try {
      const bands = {};
      drcVisibleBands.forEach(name => {
        const root = $('drc-bands').querySelector(`[data-drc-band="${name}"]`);
        bands[name] = {};
        root.querySelectorAll('[data-key]').forEach(input => {
          bands[name][input.dataset.key] = Number(input.value);
        });
      });
      const result = await api('POST', withActivePath('/dsp/drc'), {
        enable: $('drc-enable').checked,
        mode: drcMode,
        crossover_hz: Number($('drc-crossover').value),
        q_lp: Number($('drc-q-lp').value),
        q_hp: Number($('drc-q-hp').value),
        bands
      });
      if (result.status !== 'ok' || !result.data || !result.data.confirmed) {
        throw new Error(result.error || 'Readback mismatch');
      }
      adoptDrcReadback(result.data, true);
      drcMessage.textContent = 'Complete DRC readback confirmed';
      drcMessage.className = 'form-message is-success';
    } catch (error) {
      drcState.textContent = 'MISMATCH';
      drcState.className = 'module-state is-error';
      drcMessage.textContent = error.message;
      drcMessage.className = 'form-message is-error';
    } finally { button.disabled = drcVisibleBands.length === 0; }
  });

  function setSilenceForm(enabled) {
    $('silence-detector').checked = enabled === true;
    $('silence-detector').disabled = false;
    $('btn-silence-apply').disabled = false;
    $('btn-silence-read').disabled = !hasA800xFactoryDefaults();
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
    if (!hasA800xFactoryDefaults()) return;
    $('silence-detector').checked = true;
    silenceEditor.classList.add('is-dirty');
    silenceMessage.textContent = 'Factory value ON loaded locally · Apply to write';
    silenceMessage.className = 'form-message';
  });

  $('btn-silence-apply').addEventListener('click', async () => {
    const button = $('btn-silence-apply');
    button.disabled = true;
    silenceMessage.textContent = 'Writing and verifying…';
    try {
      const result = await api('POST', withActivePath('/dsp/silence'), {
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

  const a800xFilterTypes = ['PK','LS','HS','LP','HP','BP','NH','LO','HO'];
  const classicFilterTypes = ['PK','LS','HS','LP','HP','BP','NH'];

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
    const filterTypes = activePreeqSchema === 'classic_10band'
      ? classicFilterTypes : a800xFilterTypes;
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
    $('btn-preeq-read').disabled = !hasA800xFactoryDefaults();
    preeqState.textContent = preeqBaseline.enabled ? 'ON · CONFIRMED' : 'OFF · CONFIRMED';
    preeqState.className = 'module-state ' + (preeqBaseline.enabled ? 'is-on' : '');
    preeqEditor.classList.remove('is-dirty');
    drawPreeq();
  }

  function adoptConfirmedPreeqReadback(state) {
    if (!state || !state.valid || !Array.isArray(state.filters) ||
        state.filters.length !== 10) {
      throw new Error('Complete PreEQ readback missing after verification');
    }
    setPreeqForm({
      preeq_enabled: state.enabled,
      preeq_pregain_db: state.pregain_db,
      preeq_filters: state.filters
    });
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
    if (!preeqBaseline || !hasA800xFactoryDefaults()) return;
    const reset = {
      enabled: true,
      pregain_db: 0,
      filters: preeqBaseline.filters.map((filter, index) =>
        index === 0 ? { ...filter } : { ...a800xFactoryPreeqFilters[index] })
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
      const reqId = ++dspRequestId;
      const now=getPreeqForm();
      const result=await api('POST',withActivePath('/dsp/preeq'),{enable:now.enabled,pregain_db:now.pregain_db,filters:changedFilters(now)});
      if (reqId !== dspRequestId) return;
      if(result.status!=='ok'||!result.data||!result.data.confirmed) throw new Error(result.error||'Readback mismatch');
      const fresh=await api('GET',withActivePath('/dsp/state'));
      if (reqId !== dspRequestId) return;
      adoptConfirmedPreeqReadback(fresh.preeq);
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
    if (!hasA800xFactoryDefaults()) return;
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
      const result = await api('POST', withActivePath('/dsp/noise'), {
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
  let otaUploading = false;

  function unlockOtaUi() {
    otaUploading = false;
    $('btn-ota-select').disabled = false;
    $('btn-ota-clear').disabled = false;
    $('btn-ota-install').disabled = false;
    $('btn-factory-reset').disabled = false;
  }

  // File selection
  $('btn-ota-select').addEventListener('click', () => {
    $('ota-file-input').click();
  });

  $('ota-file-input').addEventListener('change', () => {
    const file = $('ota-file-input').files[0];
    if (!file) {
      otaSelectedFile = null;
      $('ota-file-info').classList.add('hidden');
      $('ota-action-area').classList.add('hidden');
      return;
    }
    otaSelectedFile = file;
    $('ota-file-name').textContent = file.name;
    $('ota-file-size').textContent = formatBytes(file.size);
    $('ota-file-info').classList.remove('hidden');
    $('ota-action-area').classList.remove('hidden');
    $('ota-result').classList.add('hidden');
  });

  // Clear selection
  $('btn-ota-clear').addEventListener('click', () => {
    otaSelectedFile = null;
    otaUploading = false;
    $('ota-file-input').value = '';
    $('ota-file-info').classList.add('hidden');
    $('ota-action-area').classList.add('hidden');
    $('ota-progress-area').classList.add('hidden');
    $('ota-result').classList.add('hidden');
    unlockOtaUi();
  });

  // Install firmware
  $('btn-ota-install').addEventListener('click', async () => {
    if (!otaSelectedFile || otaUploading) return;
    otaUploading = true;

    const progressArea = $('ota-progress-area');
    const resultDiv = $('ota-result');

    // Lock UI
    $('btn-ota-select').disabled = true;
    $('btn-ota-clear').disabled = true;
    $('btn-ota-install').disabled = true;
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
          resultDiv.textContent = '✓ Update successful – Device will restart.';
          resultDiv.className = 'success';
          resultDiv.classList.remove('hidden');

          // Countdown to reconnect
          let sec = 5;
          const timer = setInterval(() => {
            sec--;
            if (sec <= 0) {
              clearInterval(timer);
              resultDiv.textContent = '✓ Update successful – Device will restart.';
            } else {
              resultDiv.textContent = '✓ Update successful – Device will restart.'
                + ' (Reconnect in ' + sec + 's)';
            }
          }, 1000);
        } else {
          let errMsg = 'Upload failed (HTTP ' + xhr.status + ')';
          try {
            const resp = JSON.parse(xhr.responseText);
            errMsg = resp.error || resp.message || errMsg;
          } catch (_) {}
          resultDiv.textContent = '✗ ' + errMsg;
          resultDiv.className = 'error';
          resultDiv.classList.remove('hidden');
          unlockOtaUi();
          progressArea.classList.add('hidden');
        }
      });

      xhr.addEventListener('error', () => {
        resultDiv.textContent = '✗ Network error – check connection';
        resultDiv.className = 'error';
        resultDiv.classList.remove('hidden');
        unlockOtaUi();
        progressArea.classList.add('hidden');
      });

      xhr.addEventListener('abort', () => {
        resultDiv.textContent = '✗ Upload aborted';
        resultDiv.className = 'error';
        resultDiv.classList.remove('hidden');
        unlockOtaUi();
        progressArea.classList.add('hidden');
      });

      xhr.open('POST', '/api/ota/upload', true);
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.send(otaSelectedFile);
    } catch (e) {
      resultDiv.textContent = '✗ ' + e.message;
      resultDiv.className = 'error';
      resultDiv.classList.remove('hidden');
      unlockOtaUi();
      progressArea.classList.add('hidden');
    }
  });

  // OTA status polling
  async function updateOtaStatus() {
    try {
      const status = await api('GET', '/ota/status');
      $('ota-current-version').textContent = status.current_version || '-';
      $('ota-running-partition').textContent = status.running_partition || '-';

      if (status.rollback_pending) {
        $('ota-result').textContent = '⚠ App pending verification – Rollback possible';
        $('ota-result').className = 'warning';
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
  let importFileConfig = null;  // Complete validated file, including fingerprint

  $('btn-dsp-export').addEventListener('click', async () => {
    try {
      const data = await api('POST', '/dsp/config/export');
      const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'bp10-dsp-config.json';
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
    html += '<div class="preview-item"><strong>Pre EQ</strong>';
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
        importFileConfig = json;
        showImportPreview(result);
      } catch (e) {
        alert('Import failed: ' + e.message);
      }
    };
    input.click();
  });

  $('btn-import-cancel').addEventListener('click', () => {
    importPreviewData = null;
    importFileConfig = null;
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
      // Use original file DSP data (not stripped preview) for full validation
      const payload = importFileConfig;
      const result = await api('POST', '/dsp/apply', payload);
      if (result.status !== 'ok' || !result.data || !result.data.applied) {
        throw new Error(result.error || 'Apply failed');
      }
      msg.textContent = 'Configuration applied, confirmed, and saved.';
      msg.className = 'form-message is-success';
      importPreviewData = null;
      importFileConfig = null;
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
