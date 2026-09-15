# Perfis de campo e troca remota pelo rover

Status: implementação inicial realizada. Ver `GUIA_CAMPO_RTCM.md` para uso,
parâmetros efetivamente implementados, testes e limitações. O texto abaixo
preserva a proposta original; detalhes futuros, como descarte por época MSM
e persistência remota, não devem ser confundidos com recursos já implementados.

## Objetivo

Usar RTK_FAST (referência do teste de aproximadamente 600 m) e comparar
perfis para alcance de quilômetros e obtenção de RTK FIX em até 10 s após
parar. A meta deve ser medida em campo; não é garantia de aquisição a frio.

## Perfis propostos

Todos inicialmente com frequência de 915 MHz, potência configurada de 22 dBm,
cabeçalho explícito e CRC ligado, como no firmware atual.

| ID | Nome | SF | BW kHz | CR | Preâmbulo |
|---|---|---:|---:|---|---:|
| 0 | RTK_FAST | 5 | 500 | 4/5 | 8 |
| 1 | RTK_FAST_P12 | 5 | 500 | 4/5 | 12 |
| 2 | RTK_BALANCED | 6 | 500 | 4/5 | 12 |
| 3 | RTK_RANGE | 7 | 500 | 4/5 | 12 |
| 4 | RTK_RANGE_250 | 7 | 250 | 4/5 | 12 |
| 5 | RTK_RANGE_SF8 | 8 | 250 | 4/5 | 12 |
| 6 | RTK_RANGE_SF9 | 9 | 250 | 4/5 | 12 |
| 7 | RTK_RANGE_CR46 | 8 | 250 | 4/6 | 12 |

Preservar RTK_FAST exatamente para comparação de parâmetros RF. A adição
de janelas de controle altera a capacidade global e deve ser medida.
Os nomes BALANCED e RANGE já existem; documentar a mudança de preâmbulo.
Definir uma única tabela versionada em Common para ambas as pontas.
Confirmar o chip instalado antes de habilitar combinações de parâmetros.

## Interface próxima ao rover

Proposta: página local Wi-Fi no ESP32 rover, com seleção de perfil, estado
da transação, perfil confirmado na base, RSSI/SNR, idade do último RTCM e FIX.
Bluetooth SPP continua transportando NMEA para o aplicativo GNSS.
A página deve mostrar confirmação RF, não apenas aceitação local do comando.
Disponibilizar os mesmos controles por USB. Avaliar coexistência Wi-Fi/BT
sob carga. Não depende de internet no telefone.

## Controle por LoRa

1. A base anuncia periodicamente o perfil ativo e uma janela de escuta,
   inclusive se estiver sem NTRIP. O anúncio termina antes da janela começar.
2. O rover envia o pedido nessa janela usando o perfil atual.
3. Base valida versão da tabela, identidade do enlace, ID do perfil e ID da
   transação; termina o TX em andamento e responde no perfil antigo.
4. Negociam uma tentativa temporária, com prazos relativos ao fim dos
   pacotes (TX_DONE/RX_DONE), guardas e timeouts calculados pelo airtime.
   Não comparar millis() absolutos entre equipamentos.
5. No perfil candidato, trocam sondas e confirmações nos dois sentidos.
   Receber apenas um ACK no perfil antigo não conclui a mudança.
6. A base é autoridade sobre o perfil confirmado. Rover só mostra sucesso
   após ler esse estado da base no perfil novo. ACKs finais perdidos exigem
   consulta/reconciliação; repetir comandos deve ser idempotente.

Mensagens de controle separadas do stream RTCM, com versão, identidade do
enlace, transação e perfil. Não alimentar parser RTCM nem contador de sequência
RTCM com esses pacotes. Provisionar chave compartilhada e autenticar comandos
de alteração para que outra instalação não mude o perfil da base.

Dimensionar as janelas em função do airtime real dos pacotes de controle,
tempo de troca TX/RX e processamento UART. Medir impacto no RTCM útil.
Proposta inicial de anúncio a cada 1 s, sujeita ao orçamento de airtime.

## Recuperação e persistência

Se a tentativa falhar, voltar ao perfil anterior após prazo limitado.
Não depender somente de uma confirmação final: perda assimétrica pode deixar
um equipamento acreditando que a troca terminou e o outro em recuperação.

Para recuperar também após reinício ou perda total do perfil ativo, reservar
encontros periódicos em um perfil de resgate fixo, escolhido e testado antes
da saída. A base anuncia seu estado nesse perfil e aceita pedido autenticado
de recuperação; o rover permanece nele até encontrar uma janela.
Esse mecanismo tem custo de airtime e interrupção RTCM a medir. Não assumir
que RTK_FAST serve como resgate além dos 600 m observados.

Se nenhum perfil suportar o enlace no ponto, será necessário se aproximar;
software não garante entrega de um comando sem conectividade RF.

Trocas de teste ficam em RAM inicialmente. Persistência deve ser uma ação
separada e reconciliável: testar reinício de cada ponta e de ambas, incluindo
queda de energia durante gravação. Reinício entra em descoberta/resgate para
consultar a base, evitando depender de gravações simultâneas nas duas pontas.

## Correções prévias necessárias

- Timeout e recuperação para TX_RESULT e CONFIG_RESULT, correlacionados por
  sequência UART; conferir configuração efetiva por STATUS_RESP.
- Base processar RX_PACKET; rover processar TX_RESULT e enviar controles.
- Retirar o limite de TX fixo de 500 ms como pressuposto universal: dimensionar
  pelo airtime dos perfis habilitados mais margem.
- Rever fila de 500 ms; não simplesmente aumentar buffer. Medir carga por tipo,
  preservar mensagens/épocas completas e descartar dados vencidos coerentemente.
- Separar períodos de negociação dos testes de tempo para FIX.
- Priorizar serviço de rádio para não perder janelas durante conexão NTRIP,
  OLED, Bluetooth ou escrita GNSS.

## Validação antes do campo

Testes da máquina de estados com relógio e transporte simulados: perda de cada
mensagem, duplicatas, atraso, transação antiga, comando inválido, autenticação
inválida, CONFIG BUSY, timeout UART, reinício de cada ponta e falha de confirmação
final. Verificar convergência via resgate e ausência de sucesso falso.

Compilar ambos ESP32 e STM32; depois testar em bancada os dois sentidos RF,
perfil por perfil, troca repetida sob RTCM e coexistência da página com NMEA BT.

## Ensaio de campo

Registrar perfil, distância, antenas/alturas, obstáculos, estado ao chegar,
tempo até FIX após parar, permanência em FIX, RTCM válido por segundo, maior
lacuna de correção, idade da correção, RSSI/SNR e descartes da base.
Repetir as paradas para distinguir resultado ocasional de comportamento estável.
Registrar separadamente duração da troca de perfil e tempo até FIX.
Começar por FAST, FAST_P12, RANGE e RANGE_250; habilitar perfis mais lentos
conforme volume RTCM e capacidade medida.
