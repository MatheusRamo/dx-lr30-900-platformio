const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const {indexedDB,IDBObjectStore}=require('fake-indexeddb');
const script=fs.readFileSync(require('node:path').join(__dirname,'../ReceptorESP32/src/FieldReport.js'),'utf8');
let clock=100,offline=false,sequence=1,downloads=[];
const fixture=()=>({token:'test',profiles:['RTK_FAST'],active:0,paired:true,pending:false,baseFresh:true,state:'Pronto',report:{
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
   async fetch(url){if(offline)throw new Error('Offline');return {ok:true,status:200,async json(){return fixture();},async text(){return 'OK';}};}});
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
 console.log('IndexedDB integration passed: reload, points, continuous/offline samples, 10s test, interrupted reload, CSV and storage error');
})().catch(e=>{console.error(e);process.exitCode=1;});
