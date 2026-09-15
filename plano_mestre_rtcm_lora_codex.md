# Plano Mestre — Transporte RTCM via LoRa com ESP32 + STM32 + DX-LR30

## 1. Objetivo

Refatorar os três firmwares do projeto:

1. STM32F103C8T6 da board DX-PJ26 / DX-LR30
2. ESP32 transmissor
3. ESP32 receptor

para criar um transporte RTCM3 binário eficiente através de LoRa.

O sistema final deve realizar:

```text
IBGE NTRIP
     ↓
ESP32 transmissor
     ↓
RTCM3 válido
     ↓
STM32
     ↓
SX1262 / LoRa
     ↓
STM32
     ↓
ESP32 receptor
     ↓
RTCM3 válido
     ↓
LC29H(DA)
     ↓
RTK Fixed
```

### Regras principais

- Não utilizar Base64.
- Não utilizar strings para transportar RTCM.
- Não utilizar `\r\n` para delimitar payload binário.
- Não utilizar timeout/silêncio da UART para determinar o tamanho de um pacote.

---

## 2. Hardware existente — NÃO alterar os pinos

### ESP32 ↔ STM32, tanto TX quanto RX

```text
CABO 1:
ESP32 GPIO17 (TX) → STM32 PA10 (RX)

CABO 2:
STM32 PA9 (TX) → ESP32 GPIO16 (RX)
```

Alterar baud rate:

```text
ANTES:
9600

NOVO:
115200
```

### Rover ESP32 ↔ LC29H

Manter:

```text
CABO 1:
LC29H TXD → ESP32 GPIO25 (RX)

CABO 2:
ESP32 GPIO26 (TX) → LC29H RXD
```

Configuração:

```text
UART1
115200
8N1
```

Essa ligação já foi validada bidirecionalmente.

### OLED rover

Manter:

```text
SDA → GPIO21
SCL → GPIO22
```

---

## 3. Nova responsabilidade do STM32

O STM32 deve deixar de funcionar como um modem baseado em texto.

Remover do caminho principal conceitos como:

```text
RX|RSSI=-70|SNR=8|DATA=...
OK|...
STATUS|...
payload finalizado por silêncio
```

O STM32 passará a trabalhar com **frames binários explícitos**.

O STM32 não precisa saber o que é:

```text
RTCM 1005
RTCM 1077
RTCM 1087
RTCM 1097
RTCM 1127
CRC24Q
NTRIP
```

Ele só precisa saber:

```text
ESP32 pediu para transmitir N bytes
        ↓
SX1262 transmite N bytes

SX1262 recebeu N bytes
        ↓
STM32 entrega N bytes ao ESP32
```

Isso torna o firmware do STM32 um modem genérico e reutilizável.

---

## 4. Protocolo binário ESP32 ↔ STM32

Criar um protocolo UART versionado.

Formato:

```text
┌────────┬────────┬─────────┬────────┬────────┬────────┐
│ SYNC   │ VERSION│ TYPE    │ SEQ    │ LENGTH │ PAYLOAD│
│ 2 B    │ 1 B    │ 1 B    │ 2 B    │ 2 B    │ N B    │
└────────┴────────┴─────────┴────────┴────────┴────────┘
                                             │
                                             ▼
                                           CRC16
                                            2 B
```

Definição:

```text
SYNC[0] = 0xA5
SYNC[1] = 0x5A

VERSION = 0x01
```

Campos multibyte devem usar **little endian explicitamente**.

Não fazer cast direto de `struct packed` para buffer. Criar funções claras de serialização e desserialização.

CRC:

```text
CRC16-CCITT
```

calculado sobre:

```text
VERSION
TYPE
SEQ
LENGTH
PAYLOAD
```

Não incluir `SYNC` no CRC.

---

## 5. Tipos de mensagens UART

Definir inicialmente:

```text
0x01  HELLO_REQ
0x81  HELLO_RESP

0x02  RADIO_CONFIG_SET
0x82  RADIO_CONFIG_RESULT

0x03  RADIO_STATUS_REQ
0x83  RADIO_STATUS_RESP

0x10  RADIO_TX_PACKET
0x90  RADIO_TX_RESULT

0x91  RADIO_RX_PACKET

0x7F  ERROR
```

`RADIO_TX_PACKET` contém exatamente o payload que deverá ir para o SX1262.

Nenhum parsing RTCM no STM32.

---

## 6. Flow control ESP32 → STM32

Não permitir que o ESP32 simplesmente despeje pacotes mais rápido que o SX1262 consegue transmitir.

Fluxo:

```text
ESP32
   │
   │ RADIO_TX_PACKET
   ▼
STM32
   │
   │ transmite SX1262
   ▼
LoRa
   │
TX_DONE
   │
   ▼
STM32
   │
   │ RADIO_TX_RESULT
   ▼
ESP32
   │
   └── envia próximo pacote
```

Isso cria backpressure real.

O ESP32 mantém a fila grande.

O STM32 não precisa manter dezenas de pacotes na RAM.

Inicialmente permitir no máximo:

```text
1 pacote RF em voo
```

Depois pode ser ampliado.

---

## 7. Firmware STM32 deve ser totalmente não bloqueante

Remover do hot path:

```cpp
delay(...)
esperas de silêncio
loops bloqueantes aguardando pacote
```

Utilizar:

```text
UART RX interrupt/DMA
       ↓
ring buffer
       ↓
parser UART

DIO1 IRQ
       ↓
flag/evento
       ↓
processamento no main loop
```

Quando ocorrer:

```text
SX1262 TX_DONE
```

enviar `RADIO_TX_RESULT`.

No rover, após:

```text
SX1262 RX_DONE
```

copiar imediatamente:

```text
payload
RSSI
SNR
```

e produzir:

```text
RADIO_RX_PACKET
```

para o ESP32.

Depois retornar imediatamente para RX contínuo.

---

## 8. Configuração LoRa para RTCM

O perfil atual:

```text
BW = 125 kHz
SF = 9
CR = 4/6
```

não é adequado para o stream que medimos.

O NTRIP MGBH0 chegou tipicamente a:

```text
~1,4–1,5 kB/s
```

e apresentou picos de aproximadamente:

```text
~2,4 kB/s
```

O perfil inicial para RTCM deverá ser:

```text
Nome: RTK_FAST

Frequency:  915000000
BW:         500 kHz
SF:         5
CR:         4/5
Preamble:   8
CRC LoRa:   ENABLED
Power:      22 dBm
Header:     Explicit
```

O SX1262 deve permanecer configurável.

Criar também perfis:

```text
RTK_FAST
SF5 / BW500 / CR4/5

RTK_BALANCED
SF6 / BW500 / CR4/5

RTK_RANGE
SF7 / BW500 / CR4/5
```

O default deve ser:

```text
RTK_FAST
```

Com aproximadamente 220–240 bytes úteis por transmissão, SF5/BW500 fornece vários kB/s de capacidade teórica e deixa margem para o RTCM medido.

A regulamentação/frequência/potência final deve permanecer configurável e será validada separadamente.

---

## 9. Não fragmentar RTCM individualmente no protocolo RF

Não criar:

```text
RTCM message ID + fragment 1/5 + fragment 2/5
```

Vamos criar um transporte de **stream RTCM**.

Isso é mais eficiente.

O transmissor:

```text
RTCM 1005
RTCM 1077
RTCM 1087
RTCM 1097
...

        ↓

stream binário contínuo

        ↓

chunks LoRa
```

Exemplo:

```text
RTCM stream:

[D3 ........ CRC][D3 ................ CRC][D3 .... CRC]
                      │
                      ▼

chunk 100
chunk 101
chunk 102
chunk 103
```

Uma mensagem RTCM pode atravessar dois pacotes LoRa.

Isso é aceitável.

---

## 10. Cabeçalho RF

Cada payload LoRa deve possuir:

```text
BYTE 0   MAGIC0
BYTE 1   MAGIC1
BYTE 2   VERSION
BYTE 3-4 SESSION_ID
BYTE 5-6 PACKET_SEQUENCE
BYTE 7   PAYLOAD_LENGTH
BYTE 8   FLAGS
BYTE 9... RTCM STREAM DATA
```

Sugestão:

```text
MAGIC = 'R','T'
VERSION = 1
```

O `SESSION_ID` muda sempre que o transmissor reiniciar.

Pode ser gerado a partir de:

```text
esp_random()
```

do ESP32.

`PACKET_SEQUENCE` incrementa a cada pacote RF.

Exemplo:

```text
session 4812
seq 100
seq 101
seq 102
seq 103
...
```

Se o rover receber:

```text
100
101
103
```

saberá imediatamente que:

```text
pacote 102 foi perdido
```

---

## 11. Tamanho do payload

Definir:

```cpp
RADIO_STREAM_CHUNK_MAX = 240;
```

Com cabeçalho de aproximadamente 9 bytes:

```text
9 + 240 = 249 bytes
```

fica abaixo do limite de 255 bytes do SX1262.

Não usar Base64.

Comparação aproximada:

```text
Base64:
240 bytes → ~320 bytes ❌

Binário:
240 bytes → 240 bytes ✅
```

Isso representa economia de cerca de 33%.

---

## 12. Agregador de RTCM no transmissor

Não transmitir um pacote LoRa para cada pequeno pedaço recebido pelo TCP.

Criar um agregador:

```text
RTCM válido
     ↓
radioStreamBuffer[240]
     ↓
encheu?
 ┌───┴───┐
SIM     NÃO
 ↓        ↓
TX     aguarda
```

Transmitir quando:

```text
buffer == 240 bytes
```

OU quando ocorrer pequeno período de inatividade:

```text
20–30 ms desde o último byte
```

Isso permite enviar pacotes grandes sem adicionar muita latência.

---

## 13. Parser RTCM3 no ESP32 transmissor

O transmissor deve interpretar a estrutura RTCM somente o suficiente para:

```text
encontrar frame
obter comprimento
obter message type
verificar CRC24Q
filtrar
contabilizar
```

Formato:

```text
D3
│
├── 10 bits length
│
├── payload
│
└── CRC24Q 3 bytes
```

Comprimento:

```cpp
payloadLength =
    ((buffer[1] & 0x03) << 8) |
     buffer[2];

frameLength = payloadLength + 6;
```

Tipo RTCM:

```cpp
messageType =
    ((frame[3] << 4) |
     (frame[4] >> 4)) & 0x0FFF;
```

Somente RTCM com CRC24Q válido deverá entrar no transporte LoRa.

---

## 14. RTCM Filter

Implementar filtro configurável.

Estrutura:

```cpp
enum class RtcmFilterMode {
    ALL,
    ALLOW_LIST
};
```

Inicialmente:

```text
ALL
```

Mas permitir pelo Bluetooth algo como:

```text
RTCM_FILTER=ALL
```

ou:

```text
RTCM_FILTER=1005,1077,1087,1097,1127,1230
```

Isso será extremamente importante se precisarmos reduzir largura de banda.

O filtro deve acontecer **antes do LoRa**.

---

## 15. Não usar ARQ para RTCM inicialmente

Não implementar retransmissão RF nessa primeira versão.

Motivo:

Para RTCM:

```text
dado novo e fresco
```

é mais importante que:

```text
dado antigo retransmitido
```

Já teremos:

```text
LoRa FEC
+
LoRa CRC
+
packet sequence
+
RTCM CRC24Q
```

Se perder um pacote:

```text
descarta RTCM afetado
↓
continua com o próximo
```

Não parar o stream esperando retransmissão.

ARQ poderá ser implementado futuramente para mensagens específicas, se necessário.

---

## 16. ESP32 rover — recepção RF

Receber:

```text
RADIO_RX_PACKET
```

do STM32.

Extrair:

```text
RSSI
SNR
RF payload
```

Validar:

```text
magic
version
session
sequence
```

Se:

```text
seqAtual != seqAnterior + 1
```

incrementar:

```text
lostRadioPackets
```

e avisar o parser RTCM:

```text
stream discontinuity
```

---

## 17. Parser RTCM no rover

O rover recebe chunks, por exemplo:

```text
RF packet 100:
D3 00 A0 ...

RF packet 101:
... continuação ...

RF packet 102:
... CRC D3 00 ...
```

O parser deve tratar isso como um stream contínuo.

Quando encontrar um RTCM completo:

```text
CRC24Q válido?
```

Se sim:

```cpp
GNSSSerial.write(frame, frameLength);
```

Se não:

```text
descartar
resincronizar procurando próximo 0xD3
```

**Nunca mandar um RTCM incompleto para o LC29H.**

---

## 18. Tratamento de perda

Se perder um pacote LoRa:

```text
SEQ 100
SEQ 101
SEQ 103
```

fazer:

```text
RTCM parser reset
       ↓
examinar payload do pacote 103
       ↓
procurar próximo D3
       ↓
reconstruir próxima mensagem válida
```

Assim o sistema se recupera automaticamente.

---

## 19. UART LC29H

Manter:

```cpp
HardwareSerial GNSSSerial(1);

GNSSSerial.begin(
    115200,
    SERIAL_8N1,
    25,
    26
);
```

Sentido físico:

```text
LC29H TXD → ESP32 GPIO25
ESP32 GPIO26 → LC29H RXD
```

Não modificar.

---

## 20. NMEA no rover

Enquanto RTCM entra:

```text
ESP32 GPIO26 → LC29H
```

o LC29H continuará retornando NMEA:

```text
LC29H → GPIO25
```

Criar parser mínimo de:

```text
GGA
RMC
```

Do `$GNGGA`, extrair:

```text
latitude
longitude
quality
satellites
HDOP
altitude
age of differential corrections
```

Interpretar pelo menos:

```text
quality 1 → GNSS standalone
quality 4 → RTK Fixed
quality 5 → RTK Float
```

---

## 21. Bluetooth no rover

Adicionar Bluetooth Classic SPP:

```text
RTK-ROVER
```

Enviar o NMEA recebido do LC29H diretamente para o Bluetooth.

Isso permitirá:

```text
LC29H
 ↓
ESP32
 ↓ Bluetooth SPP
SW Maps
```

Também permitir comandos de diagnóstico, mas **não misturar comandos de controle no stream NMEA enviado ao SW Maps**.

Se necessário, separar modo:

```text
SWMAPS
DEBUG
```

ou criar somente saída NMEA para essa conexão inicialmente.

---

## 22. Bluetooth no transmissor

Preservar o que já implementamos:

```text
RTK-BASE
```

Manter:

```text
Wi-Fi configuration
NTRIP configuration
NVS
STATUS
CONFIG
SAVE
RECONNECT
```

Adicionar novos comandos:

```text
RADIO
RTCM
STATS
STATS_RESET

LORA_PROFILE=RTK_FAST
LORA_PROFILE=RTK_BALANCED
LORA_PROFILE=RTK_RANGE

RTCM_FILTER=ALL
RTCM_FILTER=1005,1077,1087,1097,1127,1230
```

Nunca imprimir:

```text
Wi-Fi password
NTRIP password
```

em texto claro.

---

## 23. STATUS do transmissor

O comando:

```text
STATUS
```

deve retornar aproximadamente:

```text
========== RTK BASE ==========

WiFi:
  Status: OK
  RSSI: -34 dBm

NTRIP:
  Status: OK
  Mount: MGBH0

RTCM:
  RX: 1514 B/s
  Valid: 8421
  CRC Error: 0
  Filtered: 0

Radio:
  Profile: RTK_FAST
  Queue: 2
  TX: 1482 B/s
  Packets: 3821
  Errors: 0

UART STM32:
  Baud: 115200

==============================
```

---

## 24. STATUS do rover

Deve existir estrutura interna para mostrar:

```text
========== RTK ROVER ==========

Radio:
  Packets RX: 3821
  Lost: 3
  Loss: 0.08 %
  RSSI: -72 dBm
  SNR: 8.5 dB

RTCM:
  Frames: 8543
  CRC OK: 8543
  CRC Error: 0
  RX: 1460 B/s

GNSS:
  Fix: RTK FIXED
  Satellites: 29
  HDOP: 0.51
  Age RTCM: 1.0 s

===============================
```

---

## 25. OLED rover

Substituir as informações de mensagens de teste por informações úteis.

Sugestão:

```text
┌─────────────────────┐
│ RTK FIXED      SAT29│
│ HDOP 0.51  AGE 1.0s │
│                     │
│ RSSI -72   SNR 8.5  │
│ RTCM 1.46kB/s       │
│ LOSS 0.08%          │
└─────────────────────┘
```

Atualizar no máximo algumas vezes por segundo.

Não redesenhar o OLED para cada byte RTCM.

---

## 26. Memória e performance

No caminho RTCM:

### NÃO usar

```cpp
String
std::string crescendo continuamente
malloc/free por pacote
```

### Usar

```text
uint8_t buffers fixos
ring buffers
arrays
queues de tamanho fixo
```

Limites recomendados:

```text
RTCM_MAX_FRAME_SIZE = 1029
RADIO_CHUNK_MAX = 240
UART frame max ≈ 300
```

No ESP32 pode haver filas maiores.

No STM32 manter buffers pequenos.

---

## 27. Filas

### Base ESP32

Criar fila para RF:

```text
NTRIP
 ↓
RTCM parser
 ↓
filter
 ↓
stream aggregator
 ↓
Radio TX Queue
 ↓
STM32
```

Sugestão inicial:

```text
32 pacotes
```

Se fila encher:

```text
não bloquear indefinidamente
```

Incrementar:

```text
radioQueueDrops
```

e descartar dados antigos de maneira controlada.

Para RTCM é melhor perder dados antigos que aumentar indefinidamente:

```text
Age of Differential
```

---

## 28. Priorizar frescor

Esse é um requisito arquitetural.

Nunca permitir:

```text
NTRIP produz 2 kB/s
Radio transmite 1 kB/s

fila:
1 s
2 s
5 s
10 s
30 s...
```

Se isso ocorrer, RTK estará recebendo correções antigas.

Criar proteção:

```text
MAX_QUEUE_AGE_MS
```

sugestão:

```text
500 ms
```

Se a fila ultrapassar esse limite:

```text
descartar backlog
resetar stream
iniciar com dados RTCM atuais
```

Registrar:

```text
staleDrops
```

---

## 29. Reconexão NTRIP

Manter reconexão automática.

Não usar loop bloqueante.

Backoff aproximadamente:

```text
1 s
2 s
5 s
10 s
30 s máximo
```

Quando reconectar:

```text
reset RTCM parser
reset radio stream aggregator
novo session id opcional
```

---

## 30. RTCM statistics

Criar tabela:

```cpp
struct RtcmTypeStats {
    uint16_t type;
    uint32_t frames;
    uint32_t bytes;
};
```

Pelo Bluetooth:

```text
RTCM
```

mostrar algo como:

```text
TYPE   FRAMES   BYTES
1005      121    2300
1077      121   58432
1087      121   53218
1097      121   56112
1127      121   60210
1230       12     924
```

Isso será extremamente útil para posteriormente otimizar o tráfego.

---

## 31. Estrutura de código

Evitar deixar tudo dentro de `main.cpp`.

No ESP32 criar algo aproximadamente assim:

```text
src/
  main.cpp

lib/
  Lr30Link/
    Lr30Link.h
    Lr30Link.cpp

  Rtcm3/
    Rtcm3Parser.h
    Rtcm3Parser.cpp
    Crc24Q.h
    Crc24Q.cpp

  RtcmRadio/
    RtcmRadioProtocol.h
    RtcmRadioProtocol.cpp

  NtripClient/
    NtripClient.h
    NtripClient.cpp

  BaseConfig/
    BaseConfig.h
    BaseConfig.cpp
```

No rover:

```text
lib/
  Lr30Link
  Rtcm3
  RtcmRadio
  GnssMonitor
```

Compartilhar código comum entre base e rover quando possível.

---

## 32. Firmware STM32

Antes de modificar, o Codex deve localizar no projeto existente:

```text
UART PA9/PA10
parser AT
SX1262 TX
SX1262 RX
DIO1 interrupt
configuração LoRa
timeout de payload
```

Não reescrever o driver SX1262 que já funciona sem necessidade.

Preservar o driver RF e substituir principalmente:

```text
interface textual
```

por:

```text
interface binária
```

---

## 33. Compatibilidade temporária

Não é necessário manter:

```text
AT+FREQ
AT+SF
AT+BW
...
```

entre ESP32 e STM32 se isso complicar o código.

O ESP32 poderá oferecer ao usuário comandos humanos por Bluetooth:

```text
LORA_PROFILE=RTK_FAST
```

e internamente converter isso para:

```text
RADIO_CONFIG_SET
```

binário.

Separação:

```text
HUMANO
Bluetooth textual
      ↓
ESP32
      ↓
PROTOCOLO BINÁRIO
      ↓
STM32
```

Isso é muito mais limpo.

---

## 34. Testes automatizados

Criar testes para código independente de hardware.

Obrigatórios:

```text
CRC16 UART
CRC24Q RTCM

UART frame:
encode/decode

Payload contendo:
0x00
0x0A
0x0D
0xA5
0x5A
0xFF

RTCM:
frame dividido em vários buffers

Radio stream:
frame RTCM atravessando dois pacotes

packet loss

packet duplicate

sequence wrap:
65535 → 0

session change

CRC RTCM inválido
```

O parser deve conseguir resincronizar depois de corrupção/perda.

---

## 35. Critérios de aceitação

O Codex não deve considerar a implementação terminada apenas porque compila.

A implementação será considerada funcional quando:

```text
STM32 TX compila
STM32 RX compila

ESP32 Base compila
ESP32 Rover compila

UART ESP32↔STM32:
115200 binário

NTRIP:
conecta automaticamente

RTCM:
CRC24Q validado

Radio:
transporta bytes binários sem Base64

Rover:
reconstrói RTCM

LC29H:
recebe somente frames RTCM CRC válidos

Bluetooth Base:
STATUS funciona

Bluetooth Rover:
NMEA disponível para SW Maps
```

---

## 36. Teste final físico

O teste definitivo será:

```text
CELULAR
  │
 Hotspot
  │
  ▼
ESP32 BASE
  │
NTRIP IBGE/MGBH0
  │
RTCM
  ▼
STM32
  │
LR30
  │
)))))))) 915 MHz ))))))))
  │
LR30
  ▼
STM32
  │
ESP32 ROVER
  │
RTCM
  ▼
LC29H
  │
FLOAT
  ▼
FIXED
```

Enquanto isso:

```text
LC29H
 ↓ NMEA
ESP32 Rover
 ↓ Bluetooth
SW Maps
```

Nenhum notebook deverá ser necessário.

---

# Decisões arquiteturais obrigatórias

## 1. Não usar Base64

Agora que controlamos o firmware STM32, não existe motivo para transformar dados binários em texto.

## 2. Não fragmentar cada RTCM individualmente no protocolo de rádio

Transportar um stream binário sequenciado e deixar o parser RTCM do rover reconstruir as mensagens reduz overhead e simplifica o transporte.

## 3. O STM32 deve ser um modem, não um receptor GNSS

Toda lógica de:

- NTRIP
- RTCM3
- CRC24Q
- filtros
- estatísticas
- monitoramento GNSS

fica nos ESP32.

O STM32 permanece responsável por:

- comunicação UART binária com ESP32;
- configuração do SX1262;
- transmissão RF;
- recepção RF;
- RSSI/SNR;
- sinalização de TX_DONE/RX_DONE;
- backpressure.

---

# Arquitetura anterior vs nova arquitetura

## Arquitetura anterior

```text
ESP32
 ↓ String
"Mensagem 123"
 ↓
timeout UART
 ↓
STM32
 ↓
LoRa
```

## Nova arquitetura

```text
ESP32
 ↓
frame binário + length + CRC
 ↓ UART 115200
STM32
 ↓
payload binário
 ↓
SX1262
```

Essa será a fundação do link RTK definitivo.
