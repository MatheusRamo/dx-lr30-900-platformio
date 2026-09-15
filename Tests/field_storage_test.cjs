const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const {indexedDB,IDBObjectStore}=require('fake-indexeddb');
const script=fs.readFileSync(require('node:path').join(__dirname,'../ReceptorESP32/src/FieldReport.js'),'utf8');
let clock=100,offline=false,sequence=1,downloads=[],source='LORA',correctionSession=1,requests=[],rejectSource=false;
let ntripSettings={source:'LORA',ssid:'Internet campo',host:'caster.test',port:2101,mount:'TEST',user:'',gga:true,wifiPasswordSaved:true,ntripPasswordSaved:false};
const fixture=()=>({token:'test',profiles:['RTK_FAST'],active:0,paired:true,pending:false,baseFresh:true,state:'Pronto',report:{
 correction_source:source,correction_session:correctionSession,rover_ntrip_state:'STREAM',rover_wifi_connected:true,
 rover_id:'SIMULATED',rover_boot:123,rover_uptime_ms:clock,gga_sequence:sequence,gga_age_ms:100,gnss_fresh:true,fix_quality:4,fix_state:'RTK FIXED',
 latitude_deg:-23.001,longitude_deg:-46,gst_age_ms:100,gsa_age_ms:100,rmc_age_ms:100,base_telemetry_age_ms:100,
 precision_source:'NMEA GST (1 sigma)',sigma_lat_m:0.01,sigma_lon_m:0.02,sigma_alt_m:0.03,profile_radio:'RTK_FAST',profile_active:'RTK_FAST',
 rtcm_frames:sequence*5,radio_lost:0,rtcm_rate_bps:800,rssi_rtcm_dbm:-100,snr_rtcm_db:-3}});
async function drain(){for(let i=0;i<60;i++)await new Promise(r=>setImmediate(r));}
function boot(){
 const nodes=new Map(),timers=[],events={};
 function element(){return {children:[],value:'',textContent:'',disabled:false,listeners:{},append(...v){this.children.push(...v);},appendChild(v){this.children.push(v);},replaceChildren(){this.children=[];},
   addEventListener(name,fn){this.listeners[name]=fn;},click(){if(this.listeners.click)this.listeners.click();},remove(){}};}
 const get=id=>{if(!nodes.has(id))nodes.set(id,element());return nodes.get(id);};
 const document={body:element(),hidden:false,getElementById:get,createElement:element,addEventListener(name,fn){events[name]=fn;}};
 const context=vm.createContext({window:{},document,indexedDB,navigator:{},performance:{now:()=>clock},Date,Math,URLSearchParams,AbortController,Blob,console,
   URL:{createObjectURL(blob){downloads.push(blob);return 'blob:test';},revokeObjectURL(){}},
   setTimeout(fn,delay){timers.push({fn,delay});return timers.length;},clearTimeout(){},
   async fetch(url,options={}){
     if(offline)throw new Error('Offline');
     if(options.method==='POST'){
       const fields=Object.fromEntries(options.body);requests.push({url,fields});
       if(url==='/corrections/source'){
         if(rejectSource)return {ok:false,status:409,async text(){return 'Configuracao invalida';}};
         source=fields.source;correctionSession++;ntripSettings.source=source;
       }
       if(url==='/corrections/config')for(const k of ['ssid','host','port','mount','user'])ntripSettings[k]=fields[k];
       return {ok:true,status:200,async text(){return 'OK';}};
     }
     return {ok:true,status:200,async json(){return url==='/corrections/config'?{...ntripSettings}:fixture();},async text(){return 'OK';}};
   }
 });
 vm.runInContext(script,context);
 return {get,document,events,async click(id){get(id).click();await drain();},async poll(ms=1000){clock+=ms;sequence++;const index=timers.findIndex(t=>t.fn.name==='poll');assert.ok(index>=0,'poll scheduled');const t=timers.splice(index,1)[0];await t.fn();await drain();}};
}
(async()=>{
 let page=boot();await drain();assert.match(page.get('storage').textContent,/0 registros/);
 page.get('point').value='P,"01"';page.get('project').value='SIMULADO';page.get('notes').value='=formula\nsegunda linha';
 page.get('baseLat').value='-23';page.get('baseLon').value='-46';page.get('baseName').value='Base de teste';
 await page.click('saveBase');assert.match(page.get('notice').textContent,/Posição.*salva/);
 await page.click('savePoint');assert.match(page.get('storage').textContent,/1 registros · 1 pontos/);
 page=boot();await drain();assert.match(page.get('storage').textContent,/1 registros · 1 pontos/);assert.equal(page.get('point').value,'P,"01"');
 await page.click('record');await page.poll();offline=true;await page.poll(4000);offline=false;await page.poll();await page.click('record');
 await page.click('exportAll');let csv=await downloads.at(-1).text();assert.match(csv,/distance_base_horizontal_approx_m/);assert.match(csv,/offline/);assert.match(csv,/"'\=formula\nsegunda linha"/);assert.match(csv,/"P,""01"""/);
 assert.match(csv,/111\.195/);
 await page.click('test');assert.match(page.get('measurement').textContent,/andamento/);
 for(let i=0;i<11;i++)await page.poll();assert.match(page.get('measurement').textContent,/Ponto salvo: completed/);
 await page.click('test');await page.poll();page=boot();await drain();assert.match(page.get('notice').textContent,/interrompida/);
 await page.click('exportPoints');csv=await downloads.at(-1).text();assert.match(csv,/interrupted_page_closed/);assert.doesNotMatch(csv,/"sample"/);
 const original=IDBObjectStore.prototype.put;IDBObjectStore.prototype.put=function(){throw new Error('QuotaExceededError');};
 // Settings write fails first and is visibly reported; existing data can still export.
 await page.click('savePoint');assert.match(page.get('notice').textContent,/QuotaExceededError/);
 IDBObjectStore.prototype.put=original;await page.click('exportAll');assert.ok(downloads.length>=3);
 // Direct NTRIP configuration is sent to the rover, never persisted to phone records.
 assert.equal(page.get('ntripHost').value,'caster.test');
 page.get('ntripWifiPass').value='WIFI-SECRET-TEST';page.get('ntripPassword').value='CASTER-SECRET-TEST';
 await page.click('saveNtrip');assert.equal(requests.at(-1).url,'/corrections/config');assert.equal(requests.at(-1).fields.password,'CASTER-SECRET-TEST');
 assert.equal(page.get('ntripPassword').value,'');await page.poll();
 page.get('correctionSource').value='NTRIP';await page.click('applySource');await page.poll();
 assert.match(page.get('correctionStatus').textContent,/NTRIP/);assert.equal(page.get('change').disabled,true);
 await page.click('savePoint');await page.click('exportAll');csv=await downloads.at(-1).text();
 assert.match(csv,/correction_source/);assert.match(csv,/"NTRIP"/);assert.doesNotMatch(csv,/WIFI-SECRET-TEST|CASTER-SECRET-TEST/);
 await page.click('test');assert.equal(page.get('applySource').disabled,true);
 source='LORA';correctionSession++;for(let i=0;i<11;i++)await page.poll();
 assert.match(page.get('measurement').textContent,/incomplete/);
 rejectSource=true;page.get('correctionSource').value='NTRIP';await page.click('applySource');assert.match(page.get('notice').textContent,/Configuracao invalida/);assert.equal(source,'LORA');
 console.log('IndexedDB integration passed: reload, points, continuous/offline samples, 10s test, interrupted reload, CSV and storage error');
 console.log('NTRIP page integration passed: config load/save, source selection, secrets excluded from CSV, source-change interruption, failed request');
})().catch(e=>{console.error(e);process.exitCode=1;});
