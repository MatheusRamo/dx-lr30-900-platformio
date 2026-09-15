# Teste do rover com NTRIP direto

O receptor permite escolher **LORA** ou **NTRIP** na página de campo. A seleção
e a configuração ficam salvas no ESP32 e são restauradas ao ligar. A primeira
inicialização, sem configuração NTRIP, usa LoRa.

## Atualização e acesso

Para esta alteração, grave **somente o ESP32 do ReceptorESP32** usando Upload
no PlatformIO. Não é necessário atualizar o transmissor nem os STM32.
A página e seu JavaScript estão embutidos no firmware: não há upload separado
de sistema de arquivos.

1. Conecte o celular ao Wi-Fi **RTK-ROVER**, senha **12345678**, ou à senha
   personalizada que você salvou. Senhas antigas inválidas são substituídas
   pelo padrão 12345678 ao iniciar.
2. Abra **http://192.168.4.1** e recarregue a página depois de atualizar.
   Não limpe os dados do navegador: o histórico de campo está nele.
3. Em **Origem das correções → Configurar NTRIP direto**, informe SSID e senha
   de uma rede **2,4 GHz com internet**, host do caster sem `http://`, porta
   (normalmente 2101), mountpoint, usuário e senha NTRIP.
4. Deixe GGA habilitado se o caster precisa da posição do rover, por exemplo
   um serviço VRS. O rover envia GGA com posição válida e recente a cada 5 s.
5. Toque em **Salvar configuração no rover**. Depois selecione **NTRIP direto**
   e toque em **Aplicar origem no rover**. Salvar os parâmetros sozinho não
   muda um rover que estava em LoRa.
6. Confira Wi-Fi conectado, NTRIP `STREAM`, taxa RTCM maior que zero,
   `rtcm_crc_ok`, idade das correções e FIX/FLOAT. `STREAM` significa que o
   caster aceitou a conexão; sozinho não comprova RTCM válido nem RTK FIX.

O ESP32 mantém seu ponto de acesso enquanto se conecta à rede com internet.
Conectar o celular ao RTK-ROVER **não compartilha automaticamente os dados
móveis do celular com o receptor**. Uma montagem simples em campo usa um
segundo telefone como hotspot e o primeiro para a página/Bluetooth. Em casa,
use o Wi-Fi do roteador como internet do rover. A associação a outro canal
Wi-Fi pode causar uma reconexão breve da página; confirme a leitura antes de
iniciar uma medição.

Os campos de senha vazios mantêm as senhas salvas. Para removê-las, use a opção
correspondente. Isso permite rede Wi-Fi aberta ou caster anônimo. As senhas
não são devolvidas no status, nem incluídas no CSV ou eventos do caderno.
A senha do AP RTK-ROVER é independente da senha do Wi-Fi com internet; ainda
pode ser alterada por USB com `WIFI_PASS=nova_senha`, de 8 a 63 caracteres.

Se a página não abrir, teste primeiro **http://192.168.4.1/ping**. A resposta
esperada é `RTK-ROVER OK`. Esqueça no celular uma associação antiga da rede
RTK-ROVER e conecte novamente com `12345678` se o telefone tiver guardado outra
senha. No modo LoRa, o ESP inicia o ponto de acesso no canal 1 e desativa a
economia de energia do Wi-Fi para evitar que DHCP e HTTP parem durante o teste.

O Bluetooth clássico aparece como **RTK-ROVER** e usa o PIN **1234** do próprio
pareamento Bluetooth. Esse PIN não é a antiga `pairKey` dos comandos pelo rádio,
que foi removida. O Bluetooth fornece somente as sentenças NMEA; toda a
configuração e o relatório ficam na página Wi-Fi.

## Comparação LoRa × NTRIP

Use o mesmo ponto, antena GNSS, caster, mountpoint e conjunto de mensagens
RTCM. Na base, confira o filtro RTCM; um filtro diferente faz a comparação
incluir outras diferenças além do rádio. Se o provedor limitar conexões
simultâneas da conta, desconecte a base do caster durante o ensaio direto.

1. Com LoRa selecionado, identifique o ponto e faça a medição de 10 segundos.
2. Selecione NTRIP e espere aparecerem **novas mensagens RTCM válidas**.
3. Faça outra medição de 10 segundos no mesmo ponto, anotando a origem no nome
   ou nas observações, se desejar. A origem também é registrada automaticamente.
4. Volte para LoRa pela mesma seleção. Não é necessário regravar para alternar.
5. Exporte o CSV dos pontos e o CSV completo no celular.

A troca não reinicia o LC29H nem apaga seu estado RTK. Assim, um FIX que já
existia pode continuar por algum tempo depois da troca ou perda de correções.
`fix_present_at_start` e `first_fix_ms=0` indicam FIX pré-existente; não são
uma medição de aquisição a partir de estado sem FIX. O ensaio de 10 segundos
é uma janela de observação, não uma garantia de que o GNSS irá fixar nesse prazo.

O teste direto remove o transporte RTCM por LoRa do caminho. Se o resultado
melhorar, isso ajuda a investigar perdas, capacidade ou atraso do enlace.
Se continuar ruim, ainda é preciso avaliar o fluxo RTCM, internet, antena,
visibilidade do céu, configuração do GNSS e distância à estação de referência.

## Comportamento e relatório

- Só a origem selecionada alimenta a UART do GNSS. No modo NTRIP, o rádio
  pode continuar recebendo controle/telemetria, mas seu RTCM é ignorado.
  O teste direto funciona sem resposta do modem LoRa.
- Não há fallback automático para LoRa. Falhas Wi-Fi/caster ficam visíveis
  e o cliente tenta reconectar. Uma falha NTRIP não pode parecer sucesso LoRa.
- Trocar a origem ou salvar nova configuração NTRIP enquanto ela está ativa
  abre uma nova `correction_session`, zera os contadores de RTCM encaminhado
  e descarta mensagens incompletas da origem anterior. O GNSS permanece ligado.
- A medição impede alterações pelos seus próprios botões. Uma mudança feita
  por outra aba, reinicialização ou sessão de correção diferente marca o ensaio
  como `incomplete`. Mudanças do perfil de rádio não invalidam um ensaio NTRIP.
- A distância do CSV continua sendo até a **posição cadastrada do transmissor
  LoRa**; ela não passa a representar a distância ao caster ou à estação VRS.

Campos novos no relatório, versão 3: `correction_source`, `correction_session`,
`correction_source_elapsed_ms`, `radio_corrections_enabled`,
`test_source_changed`, host/porta/mountpoint NTRIP, estado e erro do cliente,
código HTTP, bytes e taxa do corpo recebido (B/s), tentativas, falhas,
descarte de dados atrasados, GGA enviados e sua idade, conexão/IP/RSSI do
Wi-Fi do rover. Tentativas/falhas/GGA acumulam desde o boot; contadores RTCM
e taxa do corpo NTRIP reiniciam ao aplicar origem/configuração ativa.

Erros úteis: `AUTH_REJECTED` (HTTP 401/403), `CASTER_REJECTED` (outro código),
`TCP_CONNECT_FAILED`, `WIFI_DISCONNECTED`, `NO_DATA_10S`, `RESPONSE_TIMEOUT`,
`NO_VALID_RTCM_15S`, `NOT_RTCM_STREAM`, `BACKLOG_DISCARDED` e `QUEUE_OVERFLOW`.
O texto do erro fica na telemetria; o cliente continua tentando.

## Implementação e validação

Cliente NTRIP por TCP sem TLS, com autenticação Basic quando há usuário,
requisição HTTP/1.0 e leitura incremental de respostas ICY/HTTP, incluindo
transferência HTTP chunked. Não há descoberta automática de mountpoints.
DNS/conexão/leitura/envio GGA usam uma tarefa FreeRTOS separada, iniciada apenas
quando o modo NTRIP é utilizado. A fila de transporte é limitada a 6 blocos de
512 bytes; dados com mais de 500 ms na
fila são descartados e a conexão reiniciada. Somente frames RTCM completos
com CRC24Q válido são encaminhados ao GNSS.

A rota de estado envia os 136 valores de telemetria em formato compacto e o
JavaScript da página restaura os nomes antes de exibir ou gravar. Assim, o CSV
mantém todas as colunas e o ESP usa menos memória durante cada atualização.

Referências do protocolo e da combinação AP/STA:
[BKG NTRIP](https://igs.bkg.bund.de/ntrip/about) e
[exemplo AP/STA da Espressif](https://github.com/espressif/esp-idf/blob/master/examples/wifi/softap_sta/README.md).

Validação automatizada: compilação PlatformIO do receptor; testes nativos
de protocolo, NTRIP e isolamento das origens em `Tests/run_tests.cmd`; testes
da página/IndexedDB/CSV em `npm test` na pasta `Tests`. Esses testes não
substituem o ensaio físico com seu Wi-Fi, caster e LC29H.
