# Modem binário DX-LR20/LR30 (STM32F103 + SX1262)

Firmware PlatformIO para a placa DX-PJ26/DX-LR30. O STM32 funciona como modem
binário genérico: recebe pacotes explícitos do ESP32 pela USART1, transmite o
payload no SX1262 e devolve eventos de TX/RX. Não interpreta RTCM nem usa
comandos AT, delimitadores de linha ou silêncio da UART.

## Hardware preservado

- USART1 TX: PA9 -> ESP32 GPIO16
- USART1 RX: PA10 <- ESP32 GPIO17
- USART1: 115200 8N1
- SX1262: NSS PA4, SCK PA5, MISO PA6, MOSI PA7, NRST PA3, BUSY PA2
- RF: RXEN PA1, TXEN PA0, DIO1 PC15/EXTI

## Protocolo UART v1

Todos os campos multibyte são little endian.

```text
A5 5A | VERSION | TYPE | SEQ u16 | LENGTH u16 | PAYLOAD | CRC16 u16
```

O CRC16-CCITT usa valor inicial `0xFFFF`, polinômio `0x1021` e cobre de
`VERSION` até o fim de `PAYLOAD` (não cobre SYNC).

Tipos implementados:

- `01/81`: `HELLO_REQ/RESP`
- `02/82`: `RADIO_CONFIG_SET/RESULT`
- `03/83`: `RADIO_STATUS_REQ/RESP`
- `10/90`: `RADIO_TX_PACKET/RESULT`
- `91`: `RADIO_RX_PACKET`
- `7F`: `ERROR`

`RADIO_CONFIG_SET` contém, nesta ordem: frequência u32, bandwidth u32, SF u8,
denominador do coding rate u8, potência i8, preâmbulo u16 e CRC u8 (14 bytes).

`RADIO_RX_PACKET` contém RSSI i16, SNR i8, tamanho RF u8 e o payload RF.

Há no máximo um pacote RF em voo. O resultado só é enviado após `TX_DONE` (ou
timeout/erro). RX UART usa interrupção + ring buffer; TX UART usa ring buffer +
interrupção; o parser e os eventos DIO1 são processados no laço principal.

## Perfil padrão

- 915000000 Hz
- BW 500 kHz
- SF5
- CR 4/5
- preâmbulo 8
- header explícito
- CRC LoRa ligado
- 22 dBm

Frequência e potência permanecem configuráveis pela mensagem binária.

## Compilar e gravar

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run --target upload
```

O build exporta `firmware.hex` na raiz. O mesmo firmware é usado no modem da
base e no modem do rover; o comportamento TX/RX é comandado pelos ESP32.
