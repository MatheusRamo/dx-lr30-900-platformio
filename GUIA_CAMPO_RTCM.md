# Perfis e controle remoto RTCM/LoRa

Para testar **NTRIP direto no receptor**, consulte [GUIA_NTRIP_ROVER.md](GUIA_NTRIP_ROVER.md).
Essa atualização exige gravar somente o ESP32 do receptor; a preparação completa
abaixo se aplica à instalação inicial do sistema LoRa.

## Preparar uma vez, antes de sair

1. Gravar os dois ESP32 atualizados e o firmware STM32 atualizado nos dois
   modems. O rover agora usa `huge_app.csv`: gravar pelo PlatformIO para incluir
   a tabela de partições, não apenas um binário de aplicativo avulso.
2. A rede Wi-Fi do rover começa com SSID `RTK-ROVER` e senha `12345678`.
3. No rover enviar `NET_INFO` se quiser confirmar a senha atual.
4. Conectar o celular nessa rede e abrir **http://192.168.4.1**. Manter a conexão
   mesmo quando o celular avisar que a rede está sem internet.
5. Com base e rover próximos, confirmar perfil, recepção RTCM, Bluetooth/NMEA
   e uma troca de ida e volta para RTK_FAST. Se aparecer "Aguardando confirmação",
   aguardar o reencontro automático e conferir o perfil exibido.

O Bluetooth `RTK-ROVER` continua reservado ao NMEA para o aplicativo GNSS.
O controle dos perfis fica na página Wi-Fi. A senha pode ser alterada pela USB
com `WIFI_PASS=nova_senha` (mínimo de 8 caracteres); depois da alteração, o rover reinicia o ponto de
acesso com a nova senha.

## Perfis

| ID | Perfil | SF | BW kHz | CR | Preâmbulo |
|---|---|---:|---:|---|---:|
| 0 | RTK_FAST | 5 | 500 | 4/5 | 8 |
| 1 | RTK_FAST_P12 | 5 | 500 | 4/5 | 12 |
| 2 | RTK_BALANCED | 6 | 500 | 4/5 | 12 |
| 3 | RTK_RANGE | 7 | 500 | 4/5 | 12 |
| 4 | RTK_RANGE_250 | 7 | 250 | 4/5 | 12 |
| 5 | RTK_RANGE_SF8 | 8 | 250 | 4/5 | 12 |
| 6 | RTK_RANGE_SF9 | 9 | 250 | 4/5 | 12 |
| 7 | RTK_RANGE_CR46 | 8 | 250 | 4/6 | 12 |

Frequência 915 MHz, potência configurada 22 dBm, cabeçalho explícito e CRC
permanecem iguais. Confirmar o hardware efetivo do módulo antes de ensaiar.
RTK_FAST preserva os parâmetros RF do ensaio de 600 m; as janelas de controle
e o transporte por mensagem alteram a capacidade útil comparada ao firmware antigo.

## Usar no campo

Selecionar o perfil e tocar em **Aplicar no rover e na base**. A página distingue
pedido pendente de confirmação recebida pelo LoRa. As mudanças são temporárias,
sem gravação automática em flash. A base continua autoridade sobre o perfil.

O rover inicia em FAST e descobre a base; a base inicia no perfil salvo nas
configurações anteriores. O comando USB/Bluetooth `SAVE` existente na base
também salva o perfil confirmado atual. Não existe botão de persistência remota
nesta versão; um reinício do rover é reconciliado pelos anúncios/resgate.

A base anuncia uma janela de controle aproximadamente a cada 1,5 s após o
anúncio anterior; um pacote RTCM em voo pode postergar o anúncio. A tentativa
no perfil novo dura até 8 s sem confirmação. O pedido no rover expira em 60 s,
incluindo tentativas e resgate. Receber TX_DONE do modem nunca basta para
mostrar sucesso da troca.

Para reencontro, a base visita **RTK_RANGE_SF8** por aproximadamente **3,5 s a
cada 30 s**. O rover procura esse perfil após 5 s sem anúncios autenticados.
Isso interrompe temporariamente RTCM em outros perfis e precisa ser avaliado
no RTK real. Sem perdas adicionais, a descoberta costuma caber em uma janela
de cerca de 35 s; não é garantia sob perda de enlace.

Se nem o perfil de resgate alcançar o ponto, será necessário se aproximar.
O perfil SF9 não implica que o resgate SF8 terá o mesmo alcance.

## Coleta do relatório no celular

A página não usa mais a memória limitada do ESP32 para guardar o histórico.
Ela salva no IndexedDB do navegador do celular. Cada leitura de aproximadamente
1 Hz vira uma amostra; pontos, eventos, pedidos de troca e falhas de conexão
ficam no mesmo banco. O CSV é gerado diretamente no telefone.

Antes de sair, abra a página, salve a identificação da campanha e cadastre a
posição do transmissor LoRa. Se o rover estiver ao lado da base, use **Capturar
posição atual**; depois deixe o rover parado e confirme a posição. A distância
é uma estimativa horizontal pela fórmula de Haversine entre o rádio e o ponto
do rover; ela não é necessariamente a distância até a estação NTRIP.

Informe ponto, projeto, operador, obstáculos e alturas das antenas. Use
**Iniciar gravação contínua** para coletar o percurso, **Salvar ponto agora**
para um registro instantâneo ou **Medir ponto parado por 10 segundos** para o
ensaio de FIX.

Há dois downloads: **CSV dos pontos**, para a tabela principal, e **CSV
completo**, com amostras contínuas, eventos, leituras offline e telemetria.
Exporte antes de limpar o navegador ou trocar de telefone. O firmware pausa a
coleta e informa o erro se o armazenamento do celular falhar.

O relatório inclui coordenadas, altitude MSL e separação do geoide; FIX/FLOAT,
satélites, HDOP, PDOP, VDOP e idade da correção; precisão GST do LC29H quando
GST e GGA pertencem à mesma época; perfil, SF, BW, CR, preâmbulo, potência,
RSSI, SNR, perdas e idade do rádio; RTCM válido, CRC, bytes, taxa, lacunas e
telemetria autenticada da base; além de reinícios, falhas, observações,
posição da base, distância e instalação.

Se o GNSS não enviar GST, a precisão fica vazia. HDOP/PDOP/VDOP não são
convertidos indevidamente em metros. O parser preserva GGA, GST, GSA e RMC
para auditoria. O GST fornece RMS e desvios padrão conforme a especificação
Quectel do LC29H. [Especificação LC29H](https://www.quectel.com/content/uploads/2022/02/Quectel_LC29H_SeriesLC79HAL_GNSS_Protocol_Specification_V1.4.pdf)

O CSV usa aspas e UTF-8 com BOM; valores que parecem fórmulas começam com
apóstrofo para não serem executados por uma planilha.

## Medição de parada

Após terminar a troca e parar no ponto, tocar em **Iniciar medição**. A janela
dura 10 s. São registrados primeiro FIX observado, amostras GGA em FIX, RTCM
completo recebido, perdas de pacotes, maior intervalo sem RTCM e último RSSI/SNR.

- Primeiro FIX = 0: uma GGA recente já indicava FIX ao iniciar.
- Primeiro FIX = -1 no CSV: nenhum FIX observado durante a janela.
- FIX é obtido do campo de qualidade GGA, não inferido de RSSI ou CRC.
- Um único FIX não demonstra estabilidade; comparar também amostras em FIX.
- A medida de lacuna RTCM começa no início do teste, não inclui a lacuna anterior.
- O cronômetro usa o instante de recepção da GGA pelo ESP32 e tem a resolução
  temporal da saída de 1 Hz; não mede o instante interno exato de fixação.
- A medição é gravada diretamente no celular. Se a página for fechada durante
  ela, o ponto é marcado como `interrupted_page_closed`; tela bloqueada ou perda
  de Wi-Fi gera amostras offline e/ou ensaio incompleto.

Começar por FAST, FAST_P12, RANGE e RANGE_250. Testar SF8/SF9 depois de observar
o volume RTCM e os descartes na base. Perfis lentos não garantem mais FIX.

## Transporte e recuperação

- Fila de 12 mensagens RTCM completas, até 1029 bytes cada; transmissão em
  fragmentos de até 240 bytes úteis. O primeiro fragmento sinaliza um início
  de mensagem, facilitando recuperar o parser após uma perda.
- Mensagens aguardando por mais de 2 s são descartadas individualmente; em
  saturação sai a mais antiga. Não há redução automática por época MSM nem
  conversão MSM7 para MSM4. Se os descartes crescerem, adequar o filtro/origem.
- Mudança do perfil físico limpa o backlog e inicia nova sessão de transporte.
- UART continua em 115200 para manter esta variável constante no ensaio.
- Configuração do modem exige CONFIG_RESULT e STATUS_RESP correspondentes.
  Há retries, timeout de TX e verificação do modem a cada 10 s.
- Timeout RF no STM32 usa airtime calculado mais 150 ms; não fica preso em
  500 ms, que é insuficiente para alguns pacotes dos novos perfis.
- Conexão NTRIP ocorre em tarefa separada. Saída Bluetooth também usa tarefa
  e fila própria; sob congestionamento, descarta linhas NMEA inteiras em vez
  de bloquear o caminho RTCM.
- Controle RC usa a tabela de perfis e o ID de transação, sem pareamento ou
  autenticação, para simplificar os testes de campo.

## Comandos USB do rover

`STATUS`, `NET_INFO`, `WIFI_PASS=nova_senha`, `PROFILE_LIST`,
`PROFILE=RTK_RANGE_250`, `STATS_RESET`.

A base mantém comandos NTRIP/filtro/status. A troca direta unilateral com
`LORA_PROFILE=` foi substituída pelo controle coordenado a partir do rover.

## Verificação realizada e pendências físicas

Testes nativos: oito perfis, perda de mensagens, confirmação final ausente,
duplicatas, atraso na disponibilização do rádio, falha de TX, reinício de
base/rover, reinício durante negociação, reconciliação, wrap de millis e
rejeição de mensagens inválidas e recuperação.

Teste JavaScript da página: opções, pedido POST, token, bloqueio de controles
durante negociação/medição e indicação de falta de conexão.

Testes de armazenamento: recarga do navegador, persistência do IndexedDB,
amostras offline, CSV completo, cálculo de distância, ensaio de 10 segundos,
reinício de página e erro de cota.

Os testes não substituem ensaio físico de UART/SPI/IRQ, reinício dos modems,
coexistência Wi-Fi/Bluetooth, retomada NTRIP, carga RTCM e RF em campo. Não houve
gravação automática de placas nem medição de alcance/RTK nesta implementação.

```powershell
cd Tests
.\run_tests.cmd
node field_page_test.cjs
node field_storage_test.cjs
```
