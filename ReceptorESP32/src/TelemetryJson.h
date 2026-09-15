#pragma once
// Snapshot only: all historical storage and CSV generation live in the browser.
static String jsonText(const char *value)
{
    String s = "\"";
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            s += '\\';
            s += (char)*p;
        }
        else if (*p < 32)
        {
            char b[7];
            snprintf(b, sizeof(b), "\\u%04x", *p);
            s += b;
        }
        else
            s += (char)*p;
    }
    return s + "\"";
}
static void appendTelemetry(String &s)
{
    const uint32_t now = millis();
    const auto &g = gnss.status();
    const auto &m = radioControl.machine;
    const auto c = RadioControlLink::config(m.profile());
    const bool fresh = g.hasGga && now - lastGgaAt < 3000;
    const bool gstFresh = g.gstCount && now - lastGstAt < 3000;
    const bool gstMatch = gstFresh && fresh && *g.utc && *g.gstUtc && fabs(atof(g.utc) - atof(g.gstUtc)) < 0.001;
    s += ",\"report\":{\"schema_version\":3";
    auto num = [&](const char *k, double v, int digits = 0)
    { s += ",\"" + String(k) + "\":" + (isfinite(v) ? String(v, digits) : String("null")); };
    auto text = [&](const char *k, const char *v)
    { s += ",\"" + String(k) + "\":" + jsonText(v); };
    auto flag = [&](const char *k, bool v)
    { s += ",\"" + String(k) + "\":" + String(v ? "true" : "false"); };
    auto age = [&](const char *k, uint32_t at, bool valid)
    { num(k, valid ? (double)(uint32_t)(now - at) : NAN); };
    char device[17];
    snprintf(device, sizeof(device), "%012llx", (unsigned long long)ESP.getEfuseMac());
    text("rover_id", device);
    num("rover_boot", roverBoot);
    num("rover_uptime_ms", now);
    text("correction_source", corrections.name());
    num("correction_session", corrections.session);
    num("correction_source_elapsed_ms", (uint32_t)(now-correctionSelectedAt));
    flag("radio_corrections_enabled", corrections.source==CorrectionInput::LORA);
    text("rover_ntrip_state", ntripState.c_str());
    text("rover_ntrip_error", ntripError.c_str());
    num("rover_ntrip_http_status", ntripHttpStatus);
    flag("rover_ntrip_worker_ready", ntripWorkerReady);
    text("rover_ntrip_host", ntripConfig.host);
    num("rover_ntrip_port", ntripConfig.port);
    text("rover_ntrip_mountpoint", ntripConfig.mount);
    flag("rover_ntrip_gga_enabled", ntripConfig.sendGga);
    num("rover_ntrip_body_bytes", (double)ntripBodyBytes);
    num("rover_ntrip_rate_bps", ntripRate);
    num("rover_ntrip_attempts", ntripAttempts);
    num("rover_ntrip_failures", ntripFailures);
    num("rover_ntrip_queue_stale_drops", ntripQueueDrops);
    num("rover_ntrip_gga_sent", ntripGgaSent);
    age("rover_ntrip_data_age_ms", ntripLastData, ntripLastData!=0);
    age("rover_ntrip_gga_age_ms", ntripLastGga, ntripLastGga!=0);
    flag("rover_wifi_connected", WiFi.status()==WL_CONNECTED);
    num("rover_wifi_status", WiFi.status());
    num("rover_wifi_rssi_dbm", WiFi.status()==WL_CONNECTED?WiFi.RSSI():NAN);
    text("rover_wifi_ip", WiFi.status()==WL_CONNECTED?WiFi.localIP().toString().c_str():"");
    num("gga_sequence", g.ggaCount);
    age("gga_age_ms", lastGgaAt, g.hasGga);
    flag("gnss_fresh", fresh);
    text("gnss_utc_hhmmss", g.utc);
    text("gnss_date_ddmmyy", g.date);
    age("rmc_age_ms", lastRmcAt, g.hasRmc);
    num("fix_quality", g.hasGga ? g.quality : NAN);
    text("fix_state", fresh ? GnssMonitor::fixText(g.quality) : "STALE/UNAVAILABLE");
    num("latitude_deg", fresh ? g.latitude : NAN, 9);
    num("longitude_deg", fresh ? g.longitude : NAN, 9);
    num("altitude_msl_m", fresh ? g.altitude : NAN, 3);
    num("geoid_separation_m", fresh ? g.geoidSeparation : NAN, 3);
    num("altitude_ellipsoid_m", fresh ? g.altitude + g.geoidSeparation : NAN, 3);
    num("satellites_used", fresh ? g.satellites : NAN);
    num("hdop", fresh ? g.hdop : NAN, 3);
    num("pdop", g.gsaCount && now - lastGsaAt < 3000 ? g.pdop : NAN, 3);
    num("vdop", g.gsaCount && now - lastGsaAt < 3000 ? g.vdop : NAN, 3);
    age("gsa_age_ms", lastGsaAt, g.gsaCount);
    num("correction_age_gnss_s", fresh ? g.differentialAge : NAN, 3);
    text("rtcm_station_id", g.stationId);
    flag("gst_same_epoch", gstMatch);
    age("gst_age_ms", lastGstAt, g.gstCount);
    text("gst_utc_hhmmss", g.gstUtc);
    text("precision_source", gstMatch ? "NMEA GST (1 sigma)" : "UNAVAILABLE");
    num("sigma_lat_m", gstMatch ? g.sigmaLatitude : NAN, 4);
    num("sigma_lon_m", gstMatch ? g.sigmaLongitude : NAN, 4);
    num("sigma_alt_m", gstMatch ? g.sigmaAltitude : NAN, 4);
    num("horizontal_rss_sigma_m", gstMatch ? hypot(g.sigmaLatitude, g.sigmaLongitude) : NAN, 4);
    num("gst_rms_range_m", gstMatch ? g.gstRms : NAN, 4);
    num("sigma_major_m", gstMatch ? g.sigmaMajor : NAN, 4);
    num("sigma_minor_m", gstMatch ? g.sigmaMinor : NAN, 4);
    num("ellipse_orientation_deg", gstMatch ? g.ellipseOrientation : NAN, 3);
    num("speed_m_s", g.hasRmc && now - lastRmcAt < 3000 ? g.speedKnots * 0.514444444 : NAN, 3);
    num("course_deg", g.hasRmc && now - lastRmcAt < 3000 ? g.course : NAN, 3);
    text("profile_active", RadioControl::profiles[m.active()].name);
    num("profile_active_id", m.active());
    text("profile_radio", RadioControl::profiles[m.profile()].name);
    num("profile_radio_id", m.profile());
    flag("radio_config_confirmed", radioControl.ready());
    flag("base_profile_fresh", baseFresh());
    flag("profile_pending", m.pending());
    flag("rescue", m.rescue());
    num("profile_result", m.result());
    num("frequency_hz", c.frequency);
    num("bandwidth_hz", c.bandwidth);
    num("spreading_factor", c.sf);
    text("coding_rate", (String("4/") + String(c.cr)).c_str());
    num("preamble_symbols", c.preamble);
    num("power_dbm", c.power);
    flag("radio_crc", c.crc);
    text("radio_header", "explicit");
    num("uart_baud", UART_BAUD);
    age("radio_packet_age_ms", lastRadioAt, stats.radioPackets);
    num("rssi_rtcm_dbm", stats.radioPackets ? stats.rssi : NAN);
    num("snr_rtcm_db", stats.radioPackets ? stats.snr : NAN);
    num("radio_packets", stats.radioPackets);
    num("radio_lost", stats.lost);
    num("radio_loss_pct", lossPercent(), 3);
    num("radio_duplicates", stats.duplicates);
    num("radio_invalid", stats.invalidRadio);
    num("rtcm_frames", stats.rtcmFrames);
    num("rtcm_crc_ok", stats.rtcmCrcOk);
    num("rtcm_crc_errors", stats.rtcmCrcErrors);
    num("rtcm_bytes", (double)stats.rtcmBytes);
    num("rtcm_rate_bps", stats.rtcmRate);
    age("rtcm_age_ms", lastRtcmAt, stats.rtcmFrames);
    num("rtcm_max_completed_gap_ms", maxRtcmGap);
    num("last_rtcm_type", stats.rtcmFrames ? lastRtcmType : NAN);
    num("gnss_written_bytes", (double)gnssWritten);
    num("uart_crc_errors", lr30.crcErrors);
    num("uart_malformed", lr30.malformedFrames);
    num("uart_partial_timeouts", lr30.partialTimeouts);
    num("control_tx_ok", radioControl.txOk);
    num("control_tx_errors", radioControl.txErrors);
    num("control_config_errors", radioControl.configErrors);
    num("control_auth_errors", radioControl.authErrors);
    num("nmea_drops", nmeaDrops);
    num("free_heap_bytes", ESP.getFreeHeap());
    num("wifi_clients", WiFi.softAPgetStationNum());
    flag("bluetooth_connected", SerialBT.hasClient());
    age("base_telemetry_age_ms", radioControl.baseTelemetryAt, radioControl.hasBaseTelemetry);
    num("base_boot", radioControl.hasBaseTelemetry ? (double)radioControl.baseTelemetry.boot : NAN);
    num("rssi_control_dbm", radioControl.hasBaseTelemetry ? radioControl.controlRssi : NAN);
    num("snr_control_db", radioControl.hasBaseTelemetry ? radioControl.controlSnr : NAN);
    static const char *names[] = {"base_uptime_ms", "base_ntrip_rate_bps", "base_radio_rate_bps", "base_queue_messages", "base_queue_drops", "base_stale_drops",
                                  "base_rtcm_valid", "base_rtcm_crc_errors", "base_rtcm_filtered", "base_tx_ok", "base_tx_errors", "base_config_errors", "base_auth_errors", "base_wifi_rssi_dbm", "base_ntrip_state"};
    for (size_t i = 0; i < RadioControl::TELEMETRY_FIELDS; i++)
    {
        double v = radioControl.baseTelemetry.telemetry[i];
        if (i == RadioControl::WIFI_RSSI)
            v = (int32_t)radioControl.baseTelemetry.telemetry[i];
        num(names[i], radioControl.hasBaseTelemetry ? v : NAN);
    }
    text("nmea_gga", g.rawGga);
    text("nmea_gst", g.rawGst);
    text("nmea_gsa", g.rawGsa);
    text("nmea_rmc", g.rawRmc);
    s += "}";
}
