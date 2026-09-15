const assert=require('node:assert/strict');
const fs=require('node:fs');
const {distance,csvCell,makeRow,ageSnapshot,updateTest,inflateSnapshot,REPORT_FIELDS}=require('../ReceptorESP32/src/FieldReport.js');
const telemetryHeader=fs.readFileSync('../ReceptorESP32/src/TelemetryJson.h','utf8');
const staticFields=[...telemetryHeader.matchAll(/(?:num|text|flag|age)\("([^"]+)"/g)].map(match=>match[1]);
const baseNames=telemetryHeader.match(/static const char \*names\[\] = \{([^}]+)\}/s)[1];
const baseFields=[...baseNames.matchAll(/"([^"]+)"/g)].map(match=>match[1]);
const firmwareFields=[...staticFields.slice(0,-4),...baseFields,...staticFields.slice(-4)];
assert.deepEqual(REPORT_FIELDS,firmwareFields);
const compact=inflateSnapshot({report_values:REPORT_FIELDS.map((_,index)=>index)});
assert.equal(compact.report.schema_version,0);assert.equal(compact.report.nmea_rmc,REPORT_FIELDS.length-1);assert.equal(compact.report_values,undefined);
assert.equal(distance(0,0,0,0),0);assert.equal(distance(null,0,0,0),null);assert.equal(distance(91,0,0,0),null);
assert.ok(Math.abs(distance(0,0,0,1)-111195.0802)<0.1);
assert.ok(distance(0,179.999,0,-179.999)<223);
assert.equal(csvCell('Ponto "A", linha\n2'),'"Ponto ""A"", linha\n2"');
assert.equal(csvCell('=1+1'),'"\'=1+1"');assert.equal(csvCell(-23.123),'"-23.123"');assert.equal(csvCell(null),'');
const snapshot={baseFresh:true,report:{rover_id:'test',rover_boot:123,gga_sequence:1,gnss_fresh:true,gga_age_ms:500,
 fix_quality:5,latitude_deg:-23,longitude_deg:-46,profile_radio:'RTK_RANGE',rtcm_frames:10,radio_lost:2,
 gst_age_ms:500,gsa_age_ms:500,rmc_age_ms:500,base_telemetry_age_ms:500,sigma_lat_m:0.01,sigma_lon_m:0.02}};
const meta={point_name:'P1',base_latitude_deg:-23,base_longitude_deg:-46};
let row=makeRow(snapshot,meta,'point',0,1000);assert.equal(row.distance_base_horizontal_approx_m,0);assert.equal(row.fix_quality,5);
const missing=makeRow(null,meta,'sample',0,4000);assert.equal(missing.connection,'offline');assert.equal(missing.latitude_deg,undefined);assert.equal(missing.distance_base_horizontal_approx_m,null);
const stale=ageSnapshot(snapshot,2600);assert.equal(stale.report.latitude_deg,null);assert.equal(stale.report.sigma_lat_m,null);assert.equal(stale.report.gnss_fresh,false);
const fresh=ageSnapshot(snapshot,200);assert.equal(fresh.report.gga_age_ms,700);assert.equal(snapshot.report.gga_age_ms,500);
function test(){return {first_fix_ms:null,test_samples:0,test_unique_gga:0,test_fixed_gga:0,test_float_gga:0,test_connection_gaps:0,
 test_profile_changed:false,test_device_restarted:false,start_boot:123,start_rover_id:'test',start_profile:'RTK_RANGE',start_rtcm:10,start_lost:2,last_gga:null};}
let t=test();updateTest(t,row,1000);assert.equal(t.test_float_gga,1);updateTest(t,row,2000);assert.equal(t.test_unique_gga,1);
row={...row,gga_sequence:2,fix_quality:4};updateTest(t,row,3000);assert.equal(t.first_fix_ms,3000);
updateTest(t,{...row,rtcm_frames:18,radio_lost:3},10000);assert.equal(t.test_status,'completed');assert.equal(t.rtcm_frames_delta,8);
t=test();updateTest(t,missing,10000);assert.equal(t.test_status,'incomplete');assert.equal(t.first_fix_ms,null);
t=test();updateTest(t,{...row,rover_boot:124},10000);assert.equal(t.test_device_restarted,true);assert.equal(t.test_status,'incomplete');assert.equal(t.rtcm_frames_delta,null);
t=test();updateTest(t,{...row,profile_radio:'RTK_FAST'},10000);assert.equal(t.test_profile_changed,true);assert.equal(t.test_status,'incomplete');
t=test();updateTest(t,{...row,gnss_fresh:false},10000);assert.equal(t.first_fix_ms,null);assert.equal(t.test_status,'no_fix_in_10s');
t={...test(),start_source:'NTRIP',start_correction_session:7};updateTest(t,{...row,correction_source:'NTRIP',correction_session:7,profile_radio:'RTK_FAST'},10000);assert.equal(t.test_status,'completed');assert.equal(t.test_profile_changed,false);
t={...test(),start_source:'NTRIP',start_correction_session:7};updateTest(t,{...row,correction_source:'NTRIP',correction_session:8,rtcm_frames:20},10000);assert.equal(t.test_status,'incomplete');assert.equal(t.test_source_changed,true);assert.equal(t.rtcm_frames_delta,null);
console.log('Phone report tests passed: CSV, distance, stale/missing data, time to FIX, duplicates, gaps and restarts');
