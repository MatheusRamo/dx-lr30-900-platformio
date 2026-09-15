#pragma once
static const char FIELD_PAGE[] PROGMEM=R"HTML(<!doctype html><html lang="pt-BR"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>RTK · Caderno de campo</title>
<style>body{font:16px system-ui;background:#10202c;color:#eef5f7;max-width:850px;margin:auto;padding:18px}section{background:#1c3443;padding:18px;border-radius:12px;margin:14px 0}h1{font-size:28px}h2{font-size:20px}button,input,select,textarea{box-sizing:border-box;font:inherit;padding:11px;width:100%;margin:5px 0 12px;border-radius:7px;border:1px solid #638094}input,select,textarea{background:#f4fafc;color:#10202c}button{background:#78dfb7;border:0;color:#10202c;cursor:pointer}button:disabled{opacity:.45;cursor:default}dt,small{color:#b2cbd8}dd{margin:0 0 10px;overflow-wrap:anywhere}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.wide{grid-column:1/-1}#notice{white-space:pre-wrap}#storage{font-weight:600}details{margin:14px 0}summary{cursor:pointer}label{display:block}textarea{min-height:70px}a{color:#78dfb7}@media(max-width:520px){.grid{grid-template-columns:1fr}}</style>
<h1>Caderno de campo RTK</h1><p>Dados GNSS do rover, rádio e telemetria da base.</p><p id="notice" role="status">Abrindo armazenamento do celular…</p>
<section><div id="storage">Verificando armazenamento…</div><small>O histórico fica neste navegador do celular. Exporte o CSV ao terminar. Mantenha a página visível durante a coleta; tela bloqueada ou Wi-Fi desconectado pode interromper as amostras.</small></section>
<section><h2>Leitura atual</h2><dl id="status"></dl><details><summary>Telemetria completa</summary><dl id="details"></dl></details></section>
<section><h2>Origem das correções</h2>
<p id="correctionStatus">Consultando origem no rover…</p>
<label>Usar RTCM de<select id="correctionSource"><option value="LORA">LoRa · transmissor em casa</option><option value="NTRIP">NTRIP direto · internet no rover</option></select></label>
<button id="applySource">Aplicar origem no rover</button><p id="correctionFeedback" role="status"></p>
<small>A seleção fica salva no rover. NTRIP não usa RTCM do rádio nem volta automaticamente para LoRa se perder internet. A troca reinicia os contadores RTCM da sessão, mas não reinicia o GNSS; um FIX já existente pode permanecer por algum tempo.</small>
<details><summary>Configurar NTRIP direto</summary>
<p>Conecte o rover a um roteador ou hotspot de 2,4 GHz com internet. O Wi-Fi RTK-ROVER continua disponível para esta página; conectar o celular a ele não fornece internet ao rover. Para usar hotspot e esta página ao mesmo tempo, prefira um segundo celular ou um roteador.</p>
<div class="grid">
<label>Wi-Fi com internet · SSID<input id="ntripSsid" maxlength="32" autocomplete="off"></label>
<label>Senha desse Wi-Fi<input id="ntripWifiPass" type="password" maxlength="63" autocomplete="new-password" placeholder="Vazio mantém a senha salva"></label>
<label>Host do caster · sem http://<input id="ntripHost" maxlength="127" autocapitalize="none" placeholder="caster.exemplo.com"></label>
<label>Porta TCP<input id="ntripPort" type="number" min="1" max="65535" value="2101"></label>
<label>Mountpoint<input id="ntripMount" maxlength="127" autocapitalize="none"></label>
<label>Usuário · vazio para acesso anônimo<input id="ntripUser" maxlength="95" autocapitalize="none" autocomplete="off"></label>
<label>Senha NTRIP<input id="ntripPassword" type="password" maxlength="95" autocomplete="new-password" placeholder="Vazio mantém a senha salva"></label>
<label>Enviar posição GGA<select id="ntripGga"><option value="1">Sim · a cada 5 segundos</option><option value="0">Não</option></select></label>
<label>Senha Wi-Fi salva<select id="clearWifi"><option value="0">Manter / substituir acima</option><option value="1">Remover · rede aberta</option></select></label>
<label>Senha NTRIP salva<select id="clearPassword"><option value="0">Manter / substituir acima</option><option value="1">Remover</option></select></label>
</div><p id="ntripSaved"></p><button id="saveNtrip">Salvar configuração no rover</button><button id="reloadNtrip">Recarregar configuração salva</button>
<small>NTRIP via TCP, sem TLS. As senhas ficam no rover e não entram no relatório. Para comparar com LoRa, use o mesmo caster, mountpoint e mensagens RTCM da base.</small>
</details></section>
<section><h2>Identificação do ensaio</h2><div class="grid">
<label>Campanha / projeto<input id="project" maxlength="100" placeholder="Teste de alcance"></label><label>Operador<input id="operator" maxlength="100"></label>
<label>Nome do ponto<input id="point" maxlength="100" placeholder="P001"></label><label>Condições / obstáculos<input id="conditions" maxlength="200" placeholder="Campo aberto, árvores…"></label>
<label>Altura antena base (m)<input id="txHeight" type="number" min="0" step="0.01"></label><label>Altura antena rover (m)<input id="rxHeight" type="number" min="0" step="0.01"></label>
<label class="wide">Observações<textarea id="notes" maxlength="1000"></textarea></label></div>
<button id="saveSettings">Salvar identificação no celular</button></section>
<section><h2>Posição do transmissor LoRa</h2><p>Usada na distância de alcance. É a posição do rádio em casa, que pode ser diferente da estação de referência NTRIP.</p>
<div class="grid"><label>Nome da base<input id="baseName" maxlength="100" placeholder="Base em casa"></label><label>Latitude (graus decimais)<input id="baseLat" type="number" min="-90" max="90" step="any"></label><label>Longitude (graus decimais)<input id="baseLon" type="number" min="-180" max="180" step="any"></label></div>
<button id="captureBase">Capturar posição atual do rover como base</button><button id="saveBase">Salvar coordenadas digitadas</button><p id="baseInfo"></p></section>
<section><h2>Coleta e pontos</h2><button id="record">Iniciar gravação contínua · aproximadamente 1 Hz</button>
<button id="savePoint">Salvar ponto agora</button><button id="test">Medir ponto parado por 10 segundos</button><p id="measurement">Sem medição em andamento.</p>
<small>É possível registrar uma falha de FIX ou de enlace. Valores indisponíveis ficam vazios; amostras antigas não são reutilizadas como uma posição nova.</small></section>
<section><h2>Perfil do enlace</h2><select id="profile"></select><button id="change">Aplicar no rover e na base</button><p id="feedback"></p><small>A negociação e o resgate interrompem temporariamente as correções. A gravação registra essas mudanças.</small></section>
<section><h2>Exportar relatório</h2><button id="exportPoints">Baixar CSV dos pontos</button><button id="exportAll">Baixar CSV completo · pontos, amostras e eventos</button><p>Campos de precisão são desvios GST informados pelo GNSS. HDOP/PDOP/VDOP são indicadores de geometria, não precisão em metros.</p><small>Dados privados, limpeza do navegador ou falta de espaço podem remover o histórico. O download gera um arquivo independente para anexar ao relatório.</small></section>
<script src="/report.js"></script></html>)HTML";
