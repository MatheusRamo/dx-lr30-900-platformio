/* Standalone browser client. Firmware serves snapshots; IndexedDB owns history. */
(function (root) {
  'use strict';
  const finite = v => typeof v === 'number' && Number.isFinite(v);
  const uid = () => Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 12);
  const REPORT_FIELDS = [
    'schema_version', 'rover_id', 'rover_boot', 'rover_uptime_ms', 'correction_source', 'correction_session', 'correction_source_elapsed_ms', 'radio_corrections_enabled',
    'rover_ntrip_state', 'rover_ntrip_error', 'rover_ntrip_http_status', 'rover_ntrip_worker_ready', 'rover_ntrip_host', 'rover_ntrip_port', 'rover_ntrip_mountpoint',
    'rover_ntrip_gga_enabled', 'rover_ntrip_body_bytes', 'rover_ntrip_rate_bps', 'rover_ntrip_attempts', 'rover_ntrip_failures', 'rover_ntrip_queue_stale_drops',
    'rover_ntrip_gga_sent', 'rover_ntrip_data_age_ms', 'rover_ntrip_gga_age_ms', 'rover_wifi_connected', 'rover_wifi_status', 'rover_wifi_rssi_dbm', 'rover_wifi_ip',
    'gga_sequence', 'gga_age_ms', 'gnss_fresh', 'gnss_utc_hhmmss', 'gnss_date_ddmmyy', 'rmc_age_ms', 'fix_quality', 'fix_state', 'latitude_deg', 'longitude_deg',
    'altitude_msl_m', 'geoid_separation_m', 'altitude_ellipsoid_m', 'satellites_used', 'hdop', 'pdop', 'vdop', 'gsa_age_ms', 'correction_age_gnss_s', 'rtcm_station_id',
    'gst_same_epoch', 'gst_age_ms', 'gst_utc_hhmmss', 'precision_source', 'sigma_lat_m', 'sigma_lon_m', 'sigma_alt_m', 'horizontal_rss_sigma_m', 'gst_rms_range_m',
    'sigma_major_m', 'sigma_minor_m', 'ellipse_orientation_deg', 'speed_m_s', 'course_deg', 'profile_active', 'profile_active_id', 'profile_radio', 'profile_radio_id',
    'radio_config_confirmed', 'base_profile_fresh', 'profile_pending', 'rescue', 'profile_result', 'frequency_hz', 'bandwidth_hz', 'spreading_factor', 'coding_rate',
    'preamble_symbols', 'power_dbm', 'radio_crc', 'radio_header', 'uart_baud', 'radio_packet_age_ms', 'rssi_rtcm_dbm', 'snr_rtcm_db', 'radio_packets', 'radio_lost',
    'radio_loss_pct', 'radio_duplicates', 'radio_invalid', 'rtcm_frames', 'rtcm_crc_ok', 'rtcm_crc_errors', 'rtcm_bytes', 'rtcm_rate_bps', 'rtcm_age_ms',
    'rtcm_max_completed_gap_ms', 'last_rtcm_type', 'gnss_written_bytes', 'uart_crc_errors', 'uart_malformed', 'uart_partial_timeouts', 'control_tx_ok', 'control_tx_errors',
    'control_config_errors', 'control_auth_errors', 'nmea_drops', 'free_heap_bytes', 'largest_free_heap_block_bytes', 'wifi_clients', 'wifi_ap_ready', 'wifi_ap_ip',
    'wifi_ap_channel', 'bluetooth_ready', 'bluetooth_connected', 'base_telemetry_age_ms', 'base_boot', 'rssi_control_dbm', 'snr_control_db', 'base_uptime_ms',
    'base_ntrip_rate_bps', 'base_radio_rate_bps', 'base_queue_messages', 'base_queue_drops', 'base_stale_drops', 'base_rtcm_valid', 'base_rtcm_crc_errors',
    'base_rtcm_filtered', 'base_tx_ok', 'base_tx_errors', 'base_config_errors', 'base_auth_errors', 'base_wifi_rssi_dbm', 'base_ntrip_state', 'nmea_gga', 'nmea_gst', 'nmea_gsa', 'nmea_rmc'
  ];
  function inflateSnapshot(snapshot) {
    if (!snapshot || !Array.isArray(snapshot.report_values)) return snapshot;
    if (snapshot.report_values.length !== REPORT_FIELDS.length) throw new Error('Telemetria incompatível com esta página');
    snapshot.report = Object.fromEntries(REPORT_FIELDS.map((key, index) => [key, snapshot.report_values[index]]));
    delete snapshot.report_values;
    return snapshot;
  }
  function distance(lat1, lon1, lat2, lon2) {
    if (![lat1, lon1, lat2, lon2].every(finite) || Math.abs(lat1) > 90 || Math.abs(lat2) > 90 || Math.abs(lon1) > 180 || Math.abs(lon2) > 180) return null;
    const rad = Math.PI / 180, dlat = (lat2 - lat1) * rad, dlon = (lon2 - lon1) * rad;
    const a = Math.sin(dlat / 2) ** 2 + Math.cos(lat1 * rad) * Math.cos(lat2 * rad) * Math.sin(dlon / 2) ** 2;
    return 6371008.8 * 2 * Math.atan2(Math.sqrt(Math.min(1, a)), Math.sqrt(Math.max(0, 1 - a)));
  }
  function csvCell(value) {
    if (value === null || value === undefined || (typeof value === 'number' && !finite(value))) return '';
    let s = String(value); if (typeof value === 'string' && /^\s*[=+@-]/.test(s)) s = "'" + s;
    return '"' + s.replace(/"/g, '""') + '"';
  }
  const META = ['record_type', 'record_id', 'session_id', 'point_id', 'point_name', 'project', 'operator', 'conditions', 'notes', 'phone_utc', 'phone_elapsed_ms',
    'connection', 'poll_gap_ms', 'project_base_name', 'base_latitude_deg', 'base_longitude_deg', 'base_position_source', 'base_position_fix', 'base_position_saved_utc',
    'tx_antenna_height_m', 'rx_antenna_height_m', 'distance_base_horizontal_approx_m', 'distance_method', 'test_status', 'first_fix_ms', 'fix_present_at_start',
    'test_duration_ms', 'test_samples', 'test_unique_gga', 'test_fixed_gga', 'test_float_gga', 'test_connection_gaps', 'test_profile_changed', 'test_device_restarted',
    'test_source_changed', 'correction_source', 'correction_session', 'rtcm_frames_delta', 'radio_lost_delta', 'event'];
  function makeRow(snapshot, meta, kind, phoneMs, gap) {
    const r = snapshot && snapshot.report ? { ...snapshot.report } : {};
    const row = {
      ...r, ...meta, record_type: kind, record_id: uid(), phone_utc: new Date(phoneMs).toISOString(),
      connection: snapshot ? 'online' : 'offline', poll_gap_ms: gap
    };
    row.distance_base_horizontal_approx_m = distance(row.base_latitude_deg, row.base_longitude_deg, row.latitude_deg, row.longitude_deg);
    row.distance_method = row.distance_base_horizontal_approx_m === null ? '' : 'haversine sphere R=6371008.8m; horizontal approximate';
    return row;
  }
  function ageSnapshot(snapshot, elapsed) {
    if (!snapshot?.report) return null; const r = { ...snapshot.report };
    for (const k of Object.keys(r)) if (k.endsWith('_age_ms') && finite(r[k])) r[k] += Math.round(elapsed);
    if (!finite(r.gga_age_ms) || r.gga_age_ms >= 3000) {
      r.gnss_fresh = false; r.fix_state = 'STALE/UNAVAILABLE';
      for (const k of ['latitude_deg', 'longitude_deg', 'altitude_msl_m', 'geoid_separation_m', 'altitude_ellipsoid_m', 'satellites_used', 'hdop', 'correction_age_gnss_s']) r[k] = null;
    }
    if (!r.gnss_fresh || !finite(r.gst_age_ms) || r.gst_age_ms >= 3000) {
      r.gst_same_epoch = false; r.precision_source = 'UNAVAILABLE';
      for (const k of ['sigma_lat_m', 'sigma_lon_m', 'sigma_alt_m', 'horizontal_rss_sigma_m', 'gst_rms_range_m', 'sigma_major_m', 'sigma_minor_m', 'ellipse_orientation_deg']) r[k] = null;
    }
    if (!finite(r.gsa_age_ms) || r.gsa_age_ms >= 3000) { r.pdop = null; r.vdop = null; }
    if (!finite(r.rmc_age_ms) || r.rmc_age_ms >= 3000) { r.speed_m_s = null; r.course_deg = null; }
    return { ...snapshot, baseFresh: snapshot.baseFresh && finite(r.base_telemetry_age_ms) && r.base_telemetry_age_ms < 4000, report: r };
  }
  function updateTest(test, row, elapsed) {
    test.test_duration_ms = Math.min(10000, Math.max(0, elapsed)); test.test_samples++;
    if (row.connection !== 'online' || row.poll_gap_ms > 2500) test.test_connection_gaps++;
    if (row.rover_id && test.start_rover_id && row.rover_id !== test.start_rover_id) test.test_device_restarted = true;
    if (finite(row.rover_boot) && finite(test.start_boot) && row.rover_boot !== test.start_boot) test.test_device_restarted = true;
    if (test.start_source !== 'NTRIP' && row.profile_radio && test.start_profile && row.profile_radio !== test.start_profile) test.test_profile_changed = true;
    if (row.correction_source && test.start_source && row.correction_source !== test.start_source) test.test_source_changed = true;
    if (finite(row.correction_session) && finite(test.start_correction_session) && row.correction_session !== test.start_correction_session) test.test_source_changed = true;
    const key = row.rover_id + ':' + row.rover_boot + ':' + row.gga_sequence;
    if (row.connection === 'online' && row.gnss_fresh && key !== test.last_gga && elapsed <= 10000) {
      test.last_gga = key; test.test_unique_gga++;
      if (row.fix_quality === 4) { test.test_fixed_gga++; if (test.first_fix_ms === null) test.first_fix_ms = Math.round(elapsed); }
      if (row.fix_quality === 5) test.test_float_gga++;
    }
    if (row.connection === 'online') {
      if (finite(row.rtcm_frames) && finite(test.start_rtcm) && !test.test_device_restarted && !test.test_source_changed && row.rtcm_frames >= test.start_rtcm) test.rtcm_frames_delta = row.rtcm_frames - test.start_rtcm;
      else test.rtcm_frames_delta = null;
      if (finite(row.radio_lost) && finite(test.start_lost) && !test.test_device_restarted && row.radio_lost >= test.start_lost) test.radio_lost_delta = row.radio_lost - test.start_lost;
      else test.radio_lost_delta = null;
    }
    if (elapsed >= 10000) {
      test.test_status = test.test_connection_gaps || test.test_profile_changed || test.test_source_changed || test.test_device_restarted ? 'incomplete' : test.first_fix_ms === null ? 'no_fix_in_10s' : 'completed';
    }
    return test;
  }
  const api = { distance, csvCell, makeRow, ageSnapshot, updateTest, inflateSnapshot, REPORT_FIELDS, META };
  if (typeof module !== 'undefined' && module.exports) { module.exports = api; return; }
  root.FieldReport = api;
  const $ = id => document.getElementById(id);
  let db = null, settings = {}, latest = null, lastSuccess = 0, lastPoll = 0, recording = false, session = uid(), sessionStart = performance.now(), test = null;
  let token = '', loaded = false, storageFailed = false, total = 0, pointTotal = 0, fetching = false;
  let currentPointName = '', currentPointId = uid();
  let reportKeys = [];
  let ntripLoaded = false, ntripLoading = false, correctionBusy = false;
  function note(message) { $('notice').textContent = message; }
  function openDB() {
    return new Promise((resolve, reject) => {
      const req = indexedDB.open('rtk-field-report', 1);
      req.onupgradeneeded = () => { const d = req.result; d.createObjectStore('records', { keyPath: 'record_id' }); d.createObjectStore('settings'); };
      req.onsuccess = () => { req.result.onversionchange = () => { req.result.close(); storageFailed = true; note('Banco alterado em outra aba. Reabra a página.'); }; resolve(req.result); };
      req.onerror = () => reject(req.error); req.onblocked = () => note('Feche outras abas deste caderno para abrir o banco.');
    });
  }
  function storePut(store, value, key) {
    return new Promise((resolve, reject) => {
      const tx = db.transaction(store, 'readwrite'); if (key === undefined) tx.objectStore(store).put(value); else tx.objectStore(store).put(value, key);
      tx.oncomplete = resolve; tx.onerror = () => reject(tx.error); tx.onabort = () => reject(tx.error || new Error('Gravação cancelada'));
    });
  }
  function getSettings() { return new Promise((resolve, reject) => { const req = db.transaction('settings').objectStore('settings').get('survey'); req.onsuccess = () => resolve(req.result || {}); req.onerror = () => reject(req.error); }); }
  function recordsEach(callback) {
    return new Promise((resolve, reject) => {
      const tx = db.transaction('records'), req = tx.objectStore('records').openCursor();
      req.onsuccess = () => { const cursor = req.result; if (cursor) { callback(cursor.value); cursor.continue(); } };
      tx.oncomplete = resolve; tx.onerror = () => reject(tx.error); tx.onabort = () => reject(tx.error);
    });
  }
  async function saveRecord(row, isNew = true) {
    if (!db || storageFailed) throw new Error('Armazenamento indisponível');
    try { await storePut('records', row); if (isNew) { total++; if (row.record_type === 'point') pointTotal++; } storageStatus(); }
    catch (e) { storageFailed = true; recording = false; renderButtons(); note('Não foi possível salvar no celular. Coleta pausada: ' + e.message + '. Exporte os dados já salvos.'); throw e; }
  }
  function storageStatus() { $('storage').textContent = total + ' registros · ' + pointTotal + ' pontos salvos neste celular' + (recording ? ' · gravando' : ''); }
  function inputNumber(id) { const raw = $(id).value.trim(); return raw === '' ? null : Number(raw); }
  function readIdentification() {
    return {
      project: $('project').value.trim(), operator: $('operator').value.trim(), point_name: $('point').value.trim(), conditions: $('conditions').value.trim(), notes: $('notes').value,
      tx_antenna_height_m: inputNumber('txHeight'), rx_antenna_height_m: inputNumber('rxHeight')
    };
  }
  function metadata() {
    const identity = readIdentification(); if (identity.point_name !== currentPointName) { currentPointName = identity.point_name; currentPointId = uid(); }
    return { session_id: session, point_id: test ? test.point_id : currentPointId, ...settings, ...identity, phone_elapsed_ms: Math.round(performance.now() - sessionStart) };
  }
  async function saveSettings() { settings = { ...settings, ...readIdentification() }; await storePut('settings', settings, 'survey'); note('Identificação salva no celular.'); }
  function showBase() {
    $('baseInfo').textContent = finite(settings.base_latitude_deg) && finite(settings.base_longitude_deg) ?
      settings.base_latitude_deg.toFixed(9) + ', ' + settings.base_longitude_deg.toFixed(9) + ' · ' + settings.base_position_source + ' · ' + (settings.base_position_fix || 'qualidade não informada') : 'Base ainda não cadastrada; distância ficará vazia.';
  }
  async function saveBase(capture) {
    let lat = inputNumber('baseLat'), lon = inputNumber('baseLon'), source = 'manual', fix = '';
    if (capture) {
      const r = freshSnapshot()?.report; if (!r || !r.gnss_fresh || !finite(r.latitude_deg) || !finite(r.longitude_deg)) throw new Error('Sem coordenadas GNSS recentes para capturar a base.');
      lat = r.latitude_deg; lon = r.longitude_deg; source = 'rover positioned at LoRa transmitter'; fix = r.fix_state;
    }
    if (!finite(lat) || !finite(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180) throw new Error('Informe latitude e longitude válidas em graus decimais.');
    settings = { ...settings, project_base_name: $('baseName').value.trim(), base_latitude_deg: lat, base_longitude_deg: lon, base_position_source: source, base_position_fix: fix, base_position_saved_utc: new Date().toISOString() };
    await storePut('settings', settings, 'survey'); $('baseLat').value = lat; $('baseLon').value = lon; showBase(); note('Posição do transmissor salva no celular.');
  }
  function freshSnapshot() { const elapsed = performance.now() - lastSuccess; return latest && elapsed < 2500 ? ageSnapshot(latest, elapsed) : null; }
  async function savePoint() {
    if (test) throw new Error('Aguarde a medição atual.');
    await saveSettings(); const row = makeRow(freshSnapshot(), metadata(), 'point', Date.now(), 0); row.test_status = 'instant'; await saveRecord(row); note('Ponto ' + (row.point_name || 'sem nome') + ' salvo no celular.');
  }
  async function beginTest() {
    if (test) throw new Error('Já existe uma medição.'); await saveSettings();
    currentPointId = uid();
    const snapshot = freshSnapshot(), r = snapshot?.report || {};
    if (snapshot?.pending && r.correction_source !== 'NTRIP') throw new Error('Aguarde a troca do perfil.');
    const row = makeRow(snapshot, metadata(), 'point', Date.now(), 0);
    test = {
      ...row, test_status: 'recording', first_fix_ms: r.gnss_fresh && r.fix_quality === 4 ? 0 : null, fix_present_at_start: !!(r.gnss_fresh && r.fix_quality === 4),
      test_duration_ms: 0, test_samples: 0, test_unique_gga: 0, test_fixed_gga: 0, test_float_gga: 0, test_connection_gaps: 0, test_profile_changed: false, test_device_restarted: false, test_source_changed: false,
      start_source: r.correction_source, start_correction_session: r.correction_session,
      start_boot: r.rover_boot, start_rover_id: r.rover_id, start_profile: r.profile_radio, start_rtcm: r.rtcm_frames, start_lost: r.radio_lost, last_gga: null,
      started_perf: performance.now(), rtcm_frames_delta: null, radio_lost_delta: null
    };
    try { await saveRecord(test); } catch (e) { test = null; throw e; }
    $('measurement').textContent = 'Medição de 10 segundos em andamento. Mantenha a tela aberta.'; renderButtons();
  }
  async function sample(snapshot, gap) {
    if (!recording && !test) return;
    const meta = metadata(); if (test) { for (const k of Object.keys(meta)) if (k in test && k !== 'phone_elapsed_ms') meta[k] = test[k]; }
    const row = makeRow(snapshot, meta, 'sample', Date.now(), gap); await saveRecord(row);
    if (test) {
      const running = test; const elapsed = performance.now() - running.started_perf; updateTest(running, row, elapsed);
      // Point coordinates are the latest received snapshot inside its measurement window.
      if (elapsed <= 10000) {
        if (snapshot) Object.assign(running, snapshot.report);
        else for (const k of reportKeys) running[k] = null;
        if (!snapshot) { running.latitude_deg = null; running.longitude_deg = null; running.fix_state = 'UNAVAILABLE'; running.gnss_fresh = false; }
        running.point_snapshot_phone_utc = row.phone_utc; running.distance_base_horizontal_approx_m = row.distance_base_horizontal_approx_m; running.connection = row.connection;
      }
      await saveRecord(running, false);
      if (test === running && elapsed >= 10000) {
        $('measurement').textContent = 'Ponto salvo: ' + running.test_status + ' · primeiro FIX ' + (running.first_fix_ms === null ? 'não observado' : (running.first_fix_ms / 1000).toFixed(1) + ' s') + ' · GGA FIX ' + running.test_fixed_gga + '/' + running.test_unique_gga;
        test = null; renderButtons();
      }
    }
  }
  async function toggleRecording() {
    if (!recording) { await saveSettings(); session = uid(); sessionStart = performance.now(); }
    const next = !recording; const row = makeRow(freshSnapshot(), metadata(), 'event', Date.now(), 0); row.event = next ? 'recording_started' : 'recording_stopped';
    await saveRecord(row); recording = next; storageStatus(); renderButtons(); note(next ? 'Gravando no celular a cada leitura.' : 'Gravação encerrada. Os dados permanecem no celular.');
  }
  function showPairs(id, rows) { $(id).replaceChildren(); for (const [key, value] of rows) { const dt = document.createElement('dt'), dd = document.createElement('dd'); dt.textContent = key; dd.textContent = value === null || value === undefined ? '—' : String(value); $(id).append(dt, dd); } }
  function render(snapshot) {
    const r = snapshot.report; const d = distance(settings.base_latitude_deg, settings.base_longitude_deg, r.latitude_deg, r.longitude_deg);
    showPairs('status', [['GNSS', r.fix_state], ['Coordenadas', finite(r.latitude_deg) && finite(r.longitude_deg) ? r.latitude_deg.toFixed(9) + ', ' + r.longitude_deg.toFixed(9) : 'Indisponíveis'],
    ['Distância horizontal aproximada à base', d === null ? 'Cadastre a posição do transmissor' : d.toFixed(1) + ' m'],
    ['Precisão GST · σ latitude / longitude / altitude', r.precision_source === 'UNAVAILABLE' ? 'Não informada / época diferente' : [r.sigma_lat_m, r.sigma_lon_m, r.sigma_alt_m].map(v => finite(v) ? v.toFixed(4) + ' m' : '—').join(' / ')],
    ['Origem das correções', r.correction_source || 'LORA'],
    ['Perfil confirmado pela base', r.correction_source === 'NTRIP' ? 'Não utilizado nas correções NTRIP' : snapshot.baseFresh ? r.profile_active : 'Aguardando confirmação'], ['Troca do rádio', snapshot.state],
    ['RTCM / sinal', r.rtcm_rate_bps + ' B/s · RSSI ' + r.rssi_rtcm_dbm + ' dBm · SNR ' + r.snr_rtcm_db + ' dB'],
    ['Telemetria da base', r.base_telemetry_age_ms === null ? 'Indisponível' : 'Recebida há ' + (r.base_telemetry_age_ms / 1000).toFixed(1) + ' s']]);
    showPairs('details', Object.entries(r));
    const direct = r.correction_source === 'NTRIP';
    $('correctionStatus').textContent = 'Origem ativa: ' + (r.correction_source || 'LORA') + (direct ? ' · Wi-Fi ' + (r.rover_wifi_connected ? 'conectado' : 'aguardando') + ' · NTRIP ' + r.rover_ntrip_state + ' · RTCM ' + r.rtcm_rate_bps + ' B/s' + (r.rover_ntrip_error ? ' · erro: ' + r.rover_ntrip_error : '') : ' · RTCM recebido do rádio');
    if (!ntripLoaded && !ntripLoading) loadNtrip().catch(e => { $('correctionFeedback').textContent = e.message; });
    if (!loaded) { snapshot.profiles.forEach((p, i) => { const o = document.createElement('option'); o.value = i; o.textContent = p; $('profile').appendChild(o); }); $('profile').value = snapshot.active; loaded = true; }
    renderButtons();
  }
  function renderButtons() {
    const unavailable = !db || storageFailed;
    $('change').disabled = !latest?.paired || latest?.pending || !!test || latest?.report?.correction_source === 'NTRIP' || correctionBusy;
    for (const id of ['applySource', 'saveNtrip', 'reloadNtrip']) $(id).disabled = !latest || !!test || correctionBusy || !!latest.pending;
    for (const id of ['savePoint', 'test']) $(id).disabled = unavailable || !!test;
    $('record').disabled = unavailable; $('record').textContent = recording ? 'Encerrar gravação contínua' : 'Iniciar gravação contínua · aproximadamente 1 Hz';
  }
  async function changeProfile() {
    if (test) throw new Error('Aguarde a medição do ponto.');
    const response = await fetch('/profile', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ id: $('profile').value }) });
    $('feedback').textContent = await response.text();
    const row = makeRow(freshSnapshot(), metadata(), 'event', Date.now(), 0); row.event = 'profile_request ' + $('profile').value + ' HTTP ' + response.status; await saveRecord(row);
  }
  async function loadNtrip() {
    if (ntripLoading) return; ntripLoading = true;
    try {
      const response = await fetch('/corrections/config', { cache: 'no-store' });
      if (!response.ok) throw new Error('Não foi possível ler a configuração NTRIP.');
      const c = await response.json();
      if (!c.source) throw new Error('Atualize o firmware do receptor para configurar NTRIP.');
      for (const [id, key] of Object.entries({ ntripSsid: 'ssid', ntripHost: 'host', ntripPort: 'port', ntripMount: 'mount', ntripUser: 'user', correctionSource: 'source' })) $(id).value = c[key] ?? '';
      $('ntripGga').value = c.gga ? '1' : '0';
      $('ntripWifiPass').value = ''; $('ntripPassword').value = ''; $('clearWifi').value = '0'; $('clearPassword').value = '0';
      $('ntripSaved').textContent = 'Senha Wi-Fi salva: ' + (c.wifiPasswordSaved ? 'sim' : 'não') + ' · senha NTRIP salva: ' + (c.ntripPasswordSaved ? 'sim' : 'não');
      ntripLoaded = true;
    } finally { ntripLoading = false; }
  }
  async function correctionPost(path, fields, eventName) {
    if (test || correctionBusy) throw new Error('Aguarde a medição ou operação atual.');
    correctionBusy = true; renderButtons();
    try {
      const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 8000);
      let response;
      try { response = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(fields), signal: controller.signal }); }
      finally { clearTimeout(timer); }
      const message = await response.text(); $('correctionFeedback').textContent = message;
      if (!response.ok) throw new Error(message);
      // Never store request fields: they can contain passwords.
      if (db && !storageFailed) { const row = makeRow(freshSnapshot(), metadata(), 'event', Date.now(), 0); row.event = eventName; await saveRecord(row); }
      $('ntripWifiPass').value = ''; $('ntripPassword').value = ''; ntripLoaded = false;
    } finally { correctionBusy = false; renderButtons(); }
  }
  async function saveNtrip() {
    const fields = {};
    for (const [key, id] of Object.entries({ ssid: 'ntripSsid', wifiPass: 'ntripWifiPass', host: 'ntripHost', port: 'ntripPort', mount: 'ntripMount', user: 'ntripUser', password: 'ntripPassword', gga: 'ntripGga', clearWifi: 'clearWifi', clearPassword: 'clearPassword' })) fields[key] = $(id).value;
    return correctionPost('/corrections/config', fields, 'ntrip_configuration_saved');
  }
  async function exportCSV(pointsOnly) {
    if (!db) throw new Error('Banco do celular indisponível.');
    const keys = new Set(META); let count = 0;
    const internal = new Set(['start_boot', 'start_rover_id', 'start_profile', 'start_source', 'start_correction_session', 'start_rtcm', 'start_lost', 'last_gga', 'started_perf']);
    await recordsEach(row => { if (pointsOnly && row.record_type !== 'point') return; count++; Object.keys(row).filter(k => !internal.has(k)).forEach(k => keys.add(k)); });
    if (!count) throw new Error('Não há registros para exportar.');
    const columns = [...keys], parts = ['\uFEFF' + columns.map(csvCell).join(',') + '\r\n']; let chunk = ''; let n = 0;
    await recordsEach(row => { if (pointsOnly && row.record_type !== 'point') return; chunk += columns.map(k => csvCell(row[k])).join(',') + '\r\n'; if (++n % 128 === 0) { parts.push(chunk); chunk = ''; } }); if (chunk) parts.push(chunk);
    const url = URL.createObjectURL(new Blob(parts, { type: 'text/csv;charset=utf-8' })), a = document.createElement('a');
    a.href = url; a.download = 'rtk-' + (pointsOnly ? 'pontos' : 'completo') + '-' + new Date().toISOString().replace(/[:.]/g, '-') + '.csv'; document.body.appendChild(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(url), 60000);
    note(count + ' registros exportados. Verifique o arquivo nos downloads do celular.');
  }
  async function poll() {
    if (fetching) return; fetching = true; const start = performance.now(), gap = lastPoll ? Math.round(start - lastPoll) : 0; lastPoll = start;
    let snapshot = null; const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 3000);
    try {
      const response = await fetch('/status', { cache: 'no-store', signal: controller.signal }); if (!response.ok) throw new Error('HTTP ' + response.status);
      snapshot = inflateSnapshot(await response.json()); if (!snapshot.report) throw new Error('Firmware sem telemetria de relatório'); snapshot.report.http_rtt_ms = Math.round(performance.now() - start); reportKeys = Object.keys(snapshot.report); latest = snapshot; lastSuccess = performance.now(); token = snapshot.token; render(snapshot);
    } catch (e) { latest = null; showPairs('status', [['Conexão', 'Sem resposta do rover; a lacuna será registrada.']]); renderButtons(); }
    finally { clearTimeout(timer); }
    try { await sample(snapshot, gap); } catch (e) { note('Coleta interrompida: ' + e.message); recording = false; test = null; renderButtons(); }
    fetching = false; setTimeout(poll, Math.max(50, 1000 - (performance.now() - start)));
  }
  async function init() {
    const bind = (id, fn) => $(id).addEventListener('click', () => Promise.resolve().then(fn).catch(e => note(e.message)));
    bind('saveSettings', saveSettings); bind('saveBase', () => saveBase(false)); bind('captureBase', () => saveBase(true)); bind('savePoint', savePoint); bind('test', beginTest); bind('record', toggleRecording);
    bind('change', changeProfile); bind('exportPoints', () => exportCSV(true)); bind('exportAll', () => exportCSV(false));
    bind('saveNtrip', saveNtrip); bind('reloadNtrip', loadNtrip);
    bind('applySource', () => correctionPost('/corrections/source', { source: $('correctionSource').value }, 'correction_source_requested ' + $('correctionSource').value));
    try {
      db = await openDB(); settings = await getSettings();
      const map = { project: 'project', operator: 'operator', point: 'point_name', conditions: 'conditions', notes: 'notes', txHeight: 'tx_antenna_height_m', rxHeight: 'rx_antenna_height_m', baseName: 'project_base_name', baseLat: 'base_latitude_deg', baseLon: 'base_longitude_deg' };
      for (const [id, key] of Object.entries(map)) $(id).value = settings[key] ?? '';
      const unfinished = []; await recordsEach(row => { total++; if (row.record_type === 'point') pointTotal++; if (row.test_status === 'recording') unfinished.push({ ...row, test_status: 'interrupted_page_closed' }); });
      for (const row of unfinished) await saveRecord(row, false);
      storageStatus(); showBase(); note(unfinished.length ? 'Medição anterior interrompida ao fechar a página; os registros já salvos foram preservados.' : 'Pronto. Inicie a gravação ou salve um ponto.');
      if (navigator.storage?.persist) navigator.storage.persist().catch(() => { });
    } catch (e) { storageFailed = true; note('Armazenamento do celular indisponível: ' + e.message); }
    document.addEventListener('visibilitychange', () => { if (document.hidden && test) { test.test_connection_gaps++; test.test_status = 'interrupted_background'; saveRecord(test, false).catch(() => { }); test = null; $('measurement').textContent = 'Medição interrompida: página em segundo plano.'; renderButtons(); } });
    renderButtons(); poll();
  }
  init();
})(typeof window !== 'undefined' ? window : globalThis);
