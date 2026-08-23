# Firmware DX-LR20/LR30 900 MHz

Firmware para comunicacao LoRa ponto a ponto usando um **STM32F103C8T6** e um
transceptor da familia **SX1262/LLCC68**. O projeto recebe dados pela USART1,
transmite o bloco recebido pelo radio e escreve na UART os pacotes LoRa
recebidos, incluindo RSSI e SNR.

O codigo original do fabricante foi migrado do Keil MDK/ARMCC para
**VS Code + PlatformIO + GCC ARM**, sem dependencia do Keil.

## Estado do projeto

- Build validado para o STM32F103C8T6.
- Framework STM32Cube HAL.
- Compilacao e link sem erros ou avisos.
- Geracao automatica de `firmware.hex` na raiz do projeto.
- Upload e depuracao configurados para ST-Link/SWD.
- Teste funcional completo no hardware deve ser feito pelo usuario.

Ultimo build de referencia:

| Recurso | Uso | Disponivel |
| --- | ---: | ---: |
| Flash | 16.324 bytes (24,9%) | 64 KiB |
| RAM | 3.208 bytes (15,7%) | 20 KiB |

## Funcionalidades

- Radio LoRa em 915 MHz com parametros configuraveis.
- Ponte bidirecional UART para LoRa.
- Recepcao UART por interrupcao, byte a byte.
- Empacotamento por inatividade da UART ou limite de 255 bytes.
- Fila circular de 2.000 bytes para os blocos recebidos.
- Controle externo de RX/TX do modulo de radio.
- Tratamento de `TX_DONE` e `RX_DONE` pelo DIO1/EXTI.
- Saida serial de pacotes recebidos com RSSI e SNR.
- Modo opcional de teste de onda continua (CW).
- Saida Intel HEX pronta para gravacao.

## Fluxo de funcionamento

```mermaid
flowchart LR
    PC[Dispositivo serial] -->|UART 9600 8N1| UART[USART1]
    UART --> BUFFER[Buffer e fila circular]
    BUFFER --> RADIO[SX1262 / LLCC68]
    RADIO -->|LoRa 915 MHz| AIR[Enlace RF]
    AIR --> RADIO
    RADIO -->|DIO1 / EXTI| IRQ[Processamento de IRQ]
    IRQ -->|RSSI, SNR e dados| UART
```

Na inicializacao, o firmware configura o clock, UART, temporizadores, SPI,
GPIOs e radio. No laco principal, `Data_Processing()` envia pela interface LoRa
os blocos recebidos na UART, enquanto `DX_Lora_RadioIrqProcess()` processa os
eventos sinalizados pelo DIO1.

## Hardware suportado

### Microcontrolador

| Item | Configuracao |
| --- | --- |
| MCU | STM32F103C8T6 |
| Arquitetura | Arm Cortex-M3 |
| Clock do sistema | 72 MHz |
| Cristal externo | HSE de 8 MHz |
| Flash | 64 KiB |
| RAM | 20 KiB |
| Alimentacao logica | 3,3 V |

O alvo usado pelo PlatformIO e `genericSTM32F103C8`, com a definicao CMSIS
`STM32F103xB` e startup de media densidade `startup_stm32f103xb`.

### Radio

O HAL do radio foi preparado para modulos baseados em SX1262 ou LLCC68. Confirme
o modelo, o circuito de RF e os limites de potencia do seu modulo antes de
transmitir.

### Pinagem do radio

| Sinal | STM32 | Direcao no STM32 | Funcao |
| --- | --- | --- | --- |
| `NSS` | PA4 | Saida | Chip select do SPI1 |
| `SCK` | PA5 | Saida | Clock do SPI1 |
| `MISO` | PA6 | Entrada | Dados SPI do radio para o STM32 |
| `MOSI` | PA7 | Saida | Dados SPI do STM32 para o radio |
| `NRST` | PA3 | Saida | Reset do radio |
| `BUSY` | PA2 | Entrada | Estado de ocupado do radio |
| `DIO1` | PC15 | Entrada/EXTI | Interrupcoes do radio |
| `RXEN` | PA1 | Saida | Habilitacao do caminho de recepcao RF |
| `TXEN` | PA0 | Saida | Habilitacao do caminho de transmissao RF |

O DIO1 usa interrupcao por borda de subida na linha `EXTI15_10`. O clock correto
do GPIOC e habilitado pelo firmware.

### UART

| Sinal | STM32 | Configuracao |
| --- | --- | --- |
| TX | PA9 | USART1 TX |
| RX | PA10 | USART1 RX |
| Formato | - | 9600 bps, 8 bits, sem paridade, 1 stop bit |

Use um conversor USB/UART de **3,3 V**. Nao aplique sinais de 5 V diretamente
sem confirmar a tolerancia eletrica de toda a placa.

### ST-Link/SWD

| ST-Link | STM32F103C8T6 |
| --- | --- |
| SWDIO | PA13 |
| SWCLK | PA14 |
| NRST | NRST (recomendado) |
| GND | GND |
| VTref/3.3V | 3,3 V da placa |

Compartilhe o GND entre placa, ST-Link e adaptador UART.

## Configuracao LoRa padrao

Os parametros principais ficam em `LR_driver/UserConfig.h` e na funcao
`LoraInit()` de `LR_driver/UserConfig.c`.

| Parametro | Valor atual |
| --- | --- |
| Frequencia | 915.000.000 Hz |
| Tipo de pacote | LoRa |
| Bandwidth | 125 kHz |
| Spreading Factor | SF9 |
| Coding Rate | 4/6 |
| Preambulo | 8 simbolos |
| Header | Explicito |
| CRC do pacote | Desabilitado |
| Inversao de IQ | Desabilitada |
| Payload maximo | 255 bytes |
| Potencia solicitada | 22 dBm |
| Ramp time | 3.400 us |
| Regulador do radio | DC-DC |
| Modo RX | Single mode, reaberto apos cada evento |

Os dois radios precisam usar a mesma frequencia, bandwidth, spreading factor,
coding rate, formato de header, CRC, IQ e preambulo.

> **Atencao:** 915 MHz e 22 dBm podem nao ser permitidos em todas as regioes ou
> em todas as condicoes de operacao. E responsabilidade do integrador cumprir a
> regulamentacao local, limites de potencia, duty cycle e homologacao aplicavel.

## Protocolo UART

### Transmissao UART para LoRa

Com `TEST` igual a `0`, todo bloco recebido na USART1 e enviado como payload
LoRa. Nao existe um protocolo de comandos adicional.

Um bloco e fechado quando ocorre uma destas condicoes:

1. O buffer chega a 255 bytes.
2. A UART fica aproximadamente 5 ms sem receber outro byte.

O timeout e implementado pelo TIM3. Os blocos aguardam em uma fila circular de
2.000 bytes e sao transmitidos quando o radio esta disponivel.

Exemplo enviado para a UART:

```text
Ola LoRa!
```

O conteudo desse bloco sera usado diretamente como payload do pacote LoRa.

### Recepcao LoRa para UART

Um pacote recebido e impresso no formato:

```text
RX|RSSI=-72|SNR=9|DATA=Ola LoRa!
```

Os bytes do payload sao escritos como caracteres. Dados binarios podem conter
caracteres nao imprimiveis; para transportar binario de forma legivel, adicione
uma codificacao como hexadecimal ou Base64.

### Mensagem de inicializacao

Ao iniciar, o firmware escreve uma mensagem semelhante a:

```text
**********************************************
-->Power On
-->V1.2.35
-->DX SX1262(LLCC68) TEST :0 915000000
**********************************************
```

## Modo de teste CW

Em `LR_driver/UserConfig.h`:

```c
#define TEST 0
```

- `TEST = 0`: operacao normal UART/LoRa.
- `TEST = 1`: ativa transmissao de onda continua com
  `sx126x_set_tx_cw()`. Nesse modo, dados enviados pela UART sao interpretados
  como frequencia em MHz pela funcao `Hz_set()`.

O modo CW transmite continuamente e deve ser usado somente em bancada, com
carga, atenuacao e instrumentacao adequadas, respeitando a regulamentacao RF.

## Estrutura do repositorio

```text
.
|-- Core/                   # Clock, interrupcoes, HAL config e syscalls GCC
|-- Driver/                 # Drivers UART, SPI, TIM2/TIM3 e DIO1
|-- LR_driver/              # Integracao da aplicacao com o SX1262/LLCC68
|   |-- src/                # Driver base Semtech SX126x
|   `-- LICENSE.txt         # Licenca do driver Semtech
|-- Main/                   # main(), callbacks UART/TIM e laco principal
|-- quequ/                  # Fila circular usada pela recepcao UART
|-- scripts/                # Exportacao automatica do Intel HEX
|-- platformio.ini          # Configuracao principal do PlatformIO
|-- README.md               # Esta documentacao
`-- firmware.hex            # Artefato gerado localmente (ignorado pelo Git)
```

## Pre-requisitos

### Opcao recomendada: VS Code

1. Instale o [Visual Studio Code](https://code.visualstudio.com/).
2. Instale a extensao **PlatformIO IDE**.
3. Instale o driver do ST-Link usado na sua maquina.

Na primeira compilacao, o PlatformIO precisa de acesso a internet para baixar a
plataforma STM32, o STM32CubeF1 e o GCC ARM.

### Opcao por linha de comando

Instale o PlatformIO Core conforme a documentacao oficial ou use o Core que
acompanha a extensao do VS Code.

Verifique a instalacao:

```bash
pio --version
```

## Compilacao

### Pelo VS Code

1. Clone o repositorio.
2. Abra a pasta que contem `platformio.ini` no VS Code.
3. Aguarde a inicializacao do PlatformIO.
4. Execute **PlatformIO: Build** ou use o icone de check na barra inferior.

### Pelo terminal

Linux, macOS ou Windows com `pio` no `PATH`:

```bash
pio run
```

No PowerShell, usando o Core instalado pela extensao:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

O build gera os artefatos intermediarios em `.pio/build/fw/` e exporta o arquivo
final diretamente para:

```text
firmware.hex
```

As versoes principais estao fixadas ou controladas por `platformio.ini`:

| Componente | Versao validada |
| --- | --- |
| PlatformIO Core | 6.1.19 |
| Plataforma `ststm32` | 19.7.1 |
| STM32CubeF1 | 1.8.6 |
| GCC ARM Embedded | 7.2.1 |

O ambiente recebeu o nome curto `fw` para evitar o limite historico de tamanho
de caminhos do Windows quando o repositorio esta dentro de pastas longas.

### Limpar e recompilar

```bash
pio run --target clean
pio run
```

### Compilacao detalhada

```bash
pio run --verbose
```

## Gravacao

Conecte o ST-Link pela interface SWD e execute:

```bash
pio run --target upload
```

No PowerShell com o Core da extensao:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run --target upload
```

Tambem e possivel gravar `firmware.hex` usando STM32CubeProgrammer, OpenOCD ou
outra ferramenta compativel com o STM32F103C8T6.

Antes de gravar, confirme:

- MCU STM32F103C8T6 selecionado.
- Alimentacao de 3,3 V e GND comum.
- SWDIO e SWCLK conectados corretamente.
- Pino BOOT0 em nivel baixo para iniciar pela Flash apos a gravacao.
- Antena ou carga RF adequada conectada ao modulo.

## Monitor serial

O `platformio.ini` configura automaticamente 9600 bps:

```bash
pio device monitor
```

Para sair do monitor serial do PlatformIO, use `Ctrl+C`.

Nao mantenha outro programa usando a mesma porta serial durante o upload ou o
monitoramento.

## Depuracao

O projeto usa `debug_tool = stlink`. No VS Code:

1. Conecte o ST-Link pela interface SWD.
2. Abra a aba **Run and Debug**.
3. Selecione o ambiente PlatformIO `fw`.
4. Inicie a depuracao.

Para uma sessao de debug mais legivel, altere temporariamente em
`platformio.ini`:

```ini
build_type = debug
```

Retorne para `release` antes de gerar o firmware de distribuicao.

## Personalizacao

### Alterar a frequencia

Edite `LR_driver/UserConfig.h`:

```c
#define LORA_FRE 915000000
```

O valor esta em hertz. Recompile os dois lados do enlace com configuracoes
compativeis.

### Alterar parametros LoRa

Edite a funcao `LoraInit()` em `LR_driver/UserConfig.c`:

```c
params.bw = SX126X_LORA_BW_125;
params.sf = SX126X_LORA_SF9;
params.cr = SX126X_LORA_CR_4_6;
```

Revise tambem preambulo, CRC, header, IQ, potencia e ramp time.

### Alterar o baud rate

Edite a chamada em `Main/main.c`:

```c
UsartInit(9600);
```

Atualize tambem `monitor_speed` em `platformio.ini`.

## Solucao de problemas

### `pio` nao e reconhecido

Use o terminal do PlatformIO no VS Code ou invoque o executavel diretamente no
PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

### Falha ao baixar pacotes

Verifique internet, proxy, firewall e permissoes de escrita na pasta
`.platformio` do usuario. Depois execute novamente `pio run`.

### Erro `No such file or directory` durante a compilacao no Windows

O caminho completo pode ter ultrapassado o limite aceito pelo toolchain. Mova o
repositorio para um caminho mais curto, por exemplo `C:\dev\dx-lora`. O nome do
ambiente `fw` ja foi mantido curto para reduzir esse risco.

### ST-Link nao detecta o MCU

- Confira GND, SWDIO, SWCLK, NRST e tensao de referencia.
- Reduza a frequencia SWD na ferramenta de gravacao.
- Tente conectar mantendo NRST em nivel baixo.
- Confirme que o driver do ST-Link esta instalado.
- Verifique BOOT0 e a alimentacao da placa.

### Firmware grava, mas nao inicia

- Confirme cristal HSE de 8 MHz.
- Confirme BOOT0 em nivel baixo.
- Observe a mensagem de inicializacao em PA9 a 9600 bps.
- Confira se o firmware foi gerado para STM32F103C8T6, nao para F103ZE.

### Radio nao responde

- Verifique NSS, SCK, MISO, MOSI, NRST e BUSY.
- Confirme alimentacao e GND do modulo.
- Confira se `BUSY` retorna ao nivel baixo.
- Confirme o modelo SX1262/LLCC68 e o controle externo RXEN/TXEN.

### Transmite, mas nao recebe

- Verifique DIO1 em PC15 e a borda de subida da interrupcao.
- Confirme que os dois radios usam os mesmos parametros LoRa.
- Confira antena, frequencia e caminho RXEN/TXEN.
- Observe RSSI e SNR recebidos na UART.

### Dados UART sao divididos em varios pacotes

O firmware fecha o bloco apos aproximadamente 5 ms sem novos bytes. Envie o
conteudo sem pausas maiores ou ajuste a configuracao do TIM3 em
`Driver/driver_timer.c`.

## Validacao do HEX

No PowerShell, calcule o SHA-256 do firmware gerado:

```powershell
Get-FileHash .\firmware.hex -Algorithm SHA256
```

O hash muda sempre que o conteudo do firmware muda. Publique o hash junto aos
binarios de uma GitHub Release para permitir verificacao da integridade.

## Publicacao no GitHub

Arquivos gerados e locais ja estao cobertos pelo `.gitignore`, incluindo
`.pio/`, `firmware.hex` e configuracoes geradas do VS Code.

Fluxo inicial sugerido:

```bash
git init
git add .
git commit -m "Migra firmware para PlatformIO"
git branch -M main
git remote add origin https://github.com/SEU_USUARIO/SEU_REPOSITORIO.git
git push -u origin main
```

Distribua `firmware.hex` preferencialmente como artefato de uma **GitHub
Release**, acompanhado da versao, hardware alvo, configuracao RF e hash SHA-256.

## Licenca

O driver SX126x derivado da Semtech possui sua propria licenca em
`LR_driver/LICENSE.txt`.

O restante do projeto nao possui atualmente uma licenca global declarada. Antes
de permitir redistribuicao ou contribuicoes de terceiros, confirme que voce tem
direito de relicenciar o codigo original do fabricante e adicione uma licenca na
raiz do repositorio. A publicacao no GitHub, por si so, nao concede permissao de
uso, modificacao ou redistribuicao.

## Avisos

- Este firmware controla equipamento de radiofrequencia.
- Nao transmita sem antena ou carga apropriada.
- Nao exceda os limites eletricos do STM32 ou do modulo RF.
- Confirme a regulamentacao aplicavel a frequencia e potencia usadas.
- Teste o firmware em bancada antes de qualquer uso operacional.
