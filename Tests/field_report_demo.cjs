// Local synthetic data only, for browser/IndexedDB smoke tests. No hardware access.
const http=require('node:http'),fs=require('node:fs'),path=require('node:path');
const root=path.join(__dirname,'../ReceptorESP32/src');let sequence=0;const at=Date.now();
const profiles=['RTK_FAST','RTK_FAST_P12','RTK_BALANCED','RTK_RANGE','RTK_RANGE_250','RTK_RANGE_SF8','RTK_RANGE_SF9','RTK_RANGE_CR46'];
http.createServer((req,res)=>{
 const send=(type,body)=>{res.setHeader('Content-Type',type);res.setHeader('Cache-Control','no-store');res.end(body);};
 if(req.url==='/'){const source=fs.readFileSync(path.join(root,'FieldPage.h'),'utf8');return send('text/html; charset=utf-8',source.match(/R"HTML\(([\s\S]*)\)HTML"/)[1]);}
 if(req.url==='/report.js')return send('application/javascript',fs.readFileSync(path.join(root,'FieldReport.js')));
 if(req.url==='/status'){sequence++;const report={schema_version:2,rover_id:'SIMULATED',rover_boot:123,rover_uptime_ms:Date.now()-at,
   gga_sequence:sequence,gga_age_ms:100,gnss_fresh:true,fix_quality:4,fix_state:'RTK FIXED (SIMULADO)',latitude_deg:-23.001,longitude_deg:-46,
   altitude_msl_m:750,geoid_separation_m:-3,altitude_ellipsoid_m:747,hdop:0.6,pdop:1.1,vdop:0.9,
   gst_age_ms:100,gsa_age_ms:100,rmc_age_ms:100,gst_same_epoch:true,precision_source:'NMEA GST (1 sigma)',sigma_lat_m:0.01,sigma_lon_m:0.012,sigma_alt_m:0.025,
   profile_radio:'RTK_FAST',profile_active:'RTK_FAST',frequency_hz:915000000,bandwidth_hz:500000,spreading_factor:5,coding_rate:'4/5',power_dbm:22,preamble_symbols:8,
   radio_config_confirmed:true,rtcm_frames:sequence*5,rtcm_rate_bps:800,rssi_rtcm_dbm:-100,snr_rtcm_db:-3,radio_lost:0,
   base_telemetry_age_ms:100,base_boot:456,base_ntrip_rate_bps:800,base_queue_messages:1,base_queue_drops:0};
   return send('application/json',JSON.stringify({token:'demo',profiles,active:0,radio:0,ready:true,paired:true,pending:false,baseFresh:true,state:'DEMONSTRAÇÃO',report}));}
 if(req.url==='/profile'){res.statusCode=202;return send('text/plain','Simulação: pedido aceito');}
 res.statusCode=404;res.end();
}).listen(8766,'127.0.0.1',()=>console.log('Synthetic field report demo: http://127.0.0.1:8766'));
