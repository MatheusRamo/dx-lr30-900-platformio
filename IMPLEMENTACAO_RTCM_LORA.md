# Implementação RTCM/LoRa

> Atualização de setembro/2026: os perfis de campo, controle remoto e transporte
> atual estão descritos em `GUIA_CAMPO_RTCM.md`. As seções abaixo documentam a
> versão anterior (fila de chunks de 500 ms e rover sem controle remoto).

O código compartilhado pelos ESP32 fica em `Common/`:

- `Lr30Link`: framing UART binário v1 e CRC16-CCITT;
- `Rtcm3`: parser incremental RTCM3 e CRC24Q;
- `RtcmRadio`: cabeçalho RF `RT`, sessão, sequência e chunks de até 240 bytes;
- `GnssMonitor`: parser mínimo GGA/RMC sem buffers dinâmicos no caminho GNSS;
- `UartProtocol`: codec portátil usado pelos testes automatizados.

## ESP32 Base

O `TransmissorESP32` preserva Wi-Fi, NTRIP, NVS e Bluetooth `RTK-BASE`. Frames
RTCM só entram no rádio após CRC24Q válido e filtro. Os comandos são `STATUS`,
`CONFIG`, `SAVE`, `RECONNECT`, `RADIO`, `RTCM`, `STATS`, `STATS_RESET`, os
campos Wi-Fi/NTRIP já existentes, `LORA_PROFILE=` e `RTCM_FILTER=`. Senhas são
sempre ocultadas na saída.

A fila RF tem 32 posições fixas, descarta o item mais antigo quando cheia e
reinicia sessão/backlog se o item mais antigo passar de 500 ms. O agregador
fecha em 240 bytes ou após 25 ms sem novos bytes. O STM32 aceita somente um
pacote em voo e o próximo envio aguarda `RADIO_TX_RESULT`.

## ESP32 Rover

O `ReceptorESP32` valida magic/version/session/sequence, rejeita duplicatas,
contabiliza perdas e reseta o parser RTCM nas descontinuidades. Somente frames
RTCM completos com CRC24Q válido são escritos na UART1 do LC29H (GPIO25/26,
115200). GGA/RMC alimentam status/OLED; todo NMEA bruto segue pelo Bluetooth
SPP `RTK-ROVER` para o SW Maps. Diagnóstico fica na USB (`STATUS` e
`STATS_RESET`) para não contaminar o stream NMEA Bluetooth.

## Testes

Em Windows com Visual Studio Community 18:

```powershell
cd Tests
.\run_tests.cmd
```

Em sistemas com `g++`, também é possível usar `pio run` dentro de `Tests` e
executar o binário nativo gerado. Os testes cobrem CRC16, CRC24Q, framing UART
com bytes especiais, RTCM fragmentado, stream atravessando pacotes RF, perda,
duplicata, wrap 65535->0, troca de sessão, corrupção e resincronização.
