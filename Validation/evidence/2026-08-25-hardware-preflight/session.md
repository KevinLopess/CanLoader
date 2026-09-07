# Sessão física parcial — 25 de agosto de 2026

## Identificação

| Campo | Valor |
|---|---|
| Identificador da sessão | `2026-08-25-hardware-preflight` |
| Início e fim do ensaio | `2026-08-25T07:31:28.552101-03:00` a `2026-08-25T07:37:22.696409-03:00` |
| Fechamento documental | Complementado em `2026-08-25T13:15:00-03:00`, sem novo acesso ao hardware |
| Operador | Não registrado nos metadados da sessão |
| Commit da ferramenta host e dos artefatos locais | `39492b1ad5c31735a07154a0705e499fac7d4a08` |
| Revisão do bootloader residente | Não confirmada; os alvos já estavam gravados e a conexão SWD falhou |
| Estado da árvore de código | Os diretórios de implementação, drivers e testes correspondem ao commit auditado no baseline e em sua complementação; havia alterações não commitadas em `Docs/VALIDATION.md` e em `Validation/` |
| Sistema operacional | macOS 26.6.2, build `25G83`, ARM64 |
| Python e ferramenta host | Python 3.13.15; CanLoader 0.2.0; python-can 4.6.1; pyserial 3.5 |
| Toolchain | GNU Tools for STM32 14.3.rel1; STM32CubeProgrammer 2.23.0 |
| Baseline anterior | `../2026-08-25-software-baseline/`: builds aprovados, seis executáveis C aprovados e 77 testes Python aprovados |

O diretório recebeu inicialmente o sufixo `hardware-preflight`, mas a sessão
prosseguiu depois que a comunicação CAN foi confirmada. O nome foi mantido para
não separar os registros produzidos em sequência.

## Bancada efetivamente identificada

| Item | Identificação e configuração registrada |
|---|---|
| Alvo A | Placa e MCU não confirmados por SWD; UID informado pelo protocolo: `8711424552488850066FFF52` |
| Alvo A: NAME e endereço observado | `0x80000000000E30EA`, `0x80` |
| Alvo B | Placa e MCU não confirmados por SWD; UID informado pelo protocolo: `8705484257556650066FFF52` |
| Alvo B: NAME e endereço observado | `0x80000000001719AF`, `0x81` |
| Aplicação informada pelos alvos | base `0x08005000`; tamanho máximo 44032 bytes; protocolo versão 2 |
| Transceptores | Modelo, alimentação e tensão não registrados |
| ST-LINK | Modelo não registrado; firmware `V2J48S7`; serial `0C3904049318303030303032` |
| Interface CAN | CANable2, descritor `b158aa7`, VID:PID `16D0:117E`, serial `207431855842` |
| Backend e canal | SLCAN em `/dev/cu.usbmodem2074318558421`; baudrate serial não informado explicitamente |
| Bitrate CAN | 500000 bit/s |
| Alimentação | Fonte não registrada; o ST-LINK mediu 3,17 V antes de falhar a conexão SWD |
| Topologia, GND e terminações | Não registrados |
| Captura independente | Não utilizada; os quadros disponíveis são os consumidos pela própria CLI com `--verbose` |

O STM32CubeProgrammer enumerou o ST-LINK, mas a tentativa `SWD HOTPLUG`
terminou com `Unable to get core ID` e `No STM32 target found` (código 1). Por
isso, nenhum erase, write, dump ou identificação de MCU foi realizado por SWD.
A comunicação física confirmada nesta sessão foi a comunicação CAN pela
interface SLCAN.

## Artefatos

| Artefato | Tamanho | SHA-256 | CRC-32/ISO-HDLC | Uso na sessão |
|---|---:|---|---|---|
| `../2026-08-25-software-baseline/artifacts/bootloader.raw` | 16468 bytes | `eebc7edc69570c966a4852ee1d62b411f9fa34df713968d71c98646a73ca1bb5` | `0x561C5A84` | Artefato local do baseline; não gravado por SWD nesta sessão |
| `images/reference-application.raw` | 7320 bytes | `f44aaab027cc34e6cd5fa69c9f22ce90ba1931900cb969ce72b7c7cda6c64ee0` | `0x7BB303AE` | Atualização normal e recuperação do alvo A |
| `images/invalid-msp.raw` | 7320 bytes | `71f56f8d4299b943ece1014135f439d6d4b0c3281e8be14a123ef2e47f663f92` | `0x3283DA62` | Teste negativo no alvo A; MSP alterado para `0x00000000`, Reset Handler mantido em `0x08006C39` |

Os dados do bootloader e da aplicação de referência vêm do baseline executado
imediatamente antes da sessão. O log `firmware-identification` repetiu o hash e
o CRC da aplicação utilizada no barramento. A aplicação transmitida e sua cópia
com MSP inválido estão preservadas nesta pasta; o artefato local do bootloader
não comprova a revisão do binário que já residia nos alvos.

## Verificações registradas antes dos comandos mutáveis

- [ ] polaridade e tensão dos transceptores: não registradas;
- [ ] GND comum: não registrado;
- [ ] duas terminações nas extremidades: não registradas;
- [ ] resistência CAN_H--CAN_L com a rede desligada: não registrada;
- [ ] placa e MCU confirmados por SWD: conexão SWD falhou;
- [x] ST-LINK enumerado por número de série;
- [x] interface CAN e canal confirmados por enumeração e troca de quadros;
- [x] aplicação de referência comparada com o baseline por SHA-256 e CRC-32;
- [ ] captura CAN independente: não utilizada.

Os alvos já estavam energizados e continham bootloader e aplicação no início do
registro. Portanto, esta lista não substitui uma ficha de montagem elétrica nem
comprova as verificações físicas que não foram anotadas.

## Sequência observada

### Descoberta e identidade

O primeiro `canloader ls` encontrou dois nós em modo aplicação. O alvo A
respondeu com NAME `0x80000000000E30EA` no endereço `0x80`; o alvo B, com NAME
`0x80000000001719AF` no endereço `0x81`. Os comandos INFO retornaram, para ambos,
base `0x08005000`, limite de 44032 bytes, imagem armazenada de 7320 bytes e CRC
`0x7BB303AE`. As capacidades eram `0xF102803D`, incluindo o estado de aplicação
em execução.

Um INFO solicitado para o endereço `0x80` com o NAME do alvo B foi encerrado
pelo host com código 2: o NAME havia reivindicado `0x81`, não o endereço pedido.
Não houve comando mutável nessa tentativa. VERIFY enviado diretamente enquanto
cada aplicação estava ativa também foi rejeitado, nos dois alvos, com NACK 2
(`command is not valid in the current state`), comportamento compatível com a
restrição de estado do protocolo.

### Reboot remoto e retorno à aplicação

O alvo A aceitou `ENTER_BOOTLOADER`, anunciou novamente o mesmo NAME e endereço
e passou de capacidades `0xF102803D` para `0xF1028039`. Já no bootloader, VERIFY
retornou `0x7BB303AE`; em seguida, RUN_APPLICATION foi aceito e um novo scan
identificou o alvo novamente em modo aplicação. Essa sequência comprova a
transição observável pelo protocolo. Ela não mede a janela de autoboot e não
observa LED, MSP, VTOR ou PRIMASK.

### Atualização normal do alvo A

A aplicação de 7320 bytes foi enviada ao par NAME/endereço do alvo A, sem
`--run`. O comando terminou com código 0 e informou: `Verified 7320 bytes on
0x80000000000E30EA, CRC 0x7BB303AE`. INFO posterior manteve tamanho e CRC e
mostrou capacidades `0xF1028039`, isto é, bootloader com aplicação válida e sem
o indicador de aplicação em execução. RUN_APPLICATION foi então aceito.

A análise reproduzível do log contou 1950 quadros consumidos pela CLI: 1562 TX
e 388 RX. Entre os TX dirigidos ao alvo A (`DA=0x80`) houve 375 DM14, 74 TP.CM,
1098 TP.DT e 11 Proprietary A. Os 74 blocos correspondem a 73 blocos de 100
bytes e um bloco final de 20 bytes; `73 x 15 + 3 = 1098` quadros TP.DT. Os 1558
TX dirigidos foram todos para `0x80`. O alvo B apareceu no mesmo log apenas em
três Address Claims globais; seu INFO posterior ainda indicou modo aplicação,
tamanho 7320 e CRC `0x7BB303AE`. Isso sustenta o endereçamento do tráfego, mas
não prova por leitura de Flash que o alvo B permaneceu byte a byte inalterado.

### Imagem com MSP inválido e recuperação

Depois da atualização normal, foi enviada ao alvo A uma cópia da mesma imagem
com o primeiro word substituído por zero. Os 7320 bytes chegaram ao fim da
transferência, mas FINALIZE recebeu NACK 11 (`application image is invalid`) e
o comando terminou com código 2. INFO mostrou capacidades `0xF1028038`, tamanho
7320 e CRC `0x3283DA62`. Uma tentativa de RUN_APPLICATION também recebeu NACK
11. Assim, a imagem com vetor inicial inválido não foi iniciada.

Uma nova atualização, agora com a imagem válida e `--run`, terminou com código
0, VERIFY `0x7BB303AE` e pedido de execução aceito. O scan final encontrou os
dois alvos em modo aplicação, nos mesmos endereços observados no início.

## Resultado por grupo do plano

| Teste | Estado | Evidência principal | Resultado e limite |
|---|---|---|---|
| Quality gate/build | `APROVADO` | `../2026-08-25-software-baseline/` | Builds aprovados, seis executáveis C aprovados e 77 testes Python aprovados |
| A — Claim e framing | `PARCIAL` | `commands/20260825T073220-0300-can-scan.log` | Claims e IDs estendidos foram observados; faltam captura independente, marca temporal dos 250 ms e registro Lawicel bruto `T` |
| B — INFO e identidade | `PARCIAL` | `commands/20260825T073230-0300-info-target-a.log`; `commands/20260825T073234-0300-info-target-b.log`; `commands/20260825T073509-0300-wrong-name-negative.log` | Dois UIDs/NAMEs distintos e trava NAME/endereço observados; não houve erase inicial nem repetição completa após desligamento |
| C — Update DM/TP | `PARCIAL` | `commands/20260825T073342-0300-target-a-update-normal.log`; `analysis/target-a-update-normal.json` | 7320 bytes transferidos e verificados; faltam leitura SWD das regiões e observação do LED para completar os critérios |
| D — CRC independente | `APROVADO` | `commands/20260825T073459-0300-firmware-identification.log`; `commands/20260825T073342-0300-target-a-update-normal.log`; `commands/20260825T073401-0300-target-a-info-after-update.log`; `commands/20260825T073314-0300-target-a-verify-in-bootloader.log` | No bootloader residente, cuja revisão não foi confirmada, CRC local, VERIFY e metadata coincidiram em `0x7BB303AE` |
| E — Handoff e autoboot | `PARCIAL` | `commands/20260825T073316-0300-target-a-run-application.log`; `commands/20260825T073324-0300-can-scan-after-roundtrip.log` | Transição de modo observada; LED, contexto Cortex-M e autoboot após reset sem comandos não foram medidos |
| F — Aplicação e reboot remoto | `PARCIAL` | `commands/20260825T073310-0300-target-a-reboot-to-bootloader.log`; `commands/20260825T073314-0300-target-a-verify-in-bootloader.log`; `commands/20260825T073316-0300-target-a-run-application.log`; `commands/20260825T073324-0300-can-scan-after-roundtrip.log` | Reboot remoto, remoção do estado running, VERIFY e retorno ao modo aplicação observados; a janela completa de 10 s não foi caracterizada |
| G — Commanded Address | `PENDENTE` | — | Nenhum `set-address` foi executado |
| H — Interrupção de energia | `PENDENTE` | — | Não houve corte de energia nem reset durante transferência |
| I — Recuperação single-slot | `PENDENTE` | — | Houve recuperação após imagem inválida, mas não após a interrupção definida no teste H |
| J — Erros de protocolo | `PARCIAL` | `commands/20260825T073509-0300-wrong-name-negative.log`; `commands/20260825T073245-0300-verify-target-a.log`; `commands/20260825T073248-0300-verify-target-b.log`; `commands/20260825T073648-0300-target-a-update-invalid-msp.log`; `commands/20260825T073702-0300-target-a-run-invalid-negative.log` | Casos de identidade, estado e vetor inválido passaram; a matriz completa de injeção não foi executada |
| K — Dois dispositivos | `PARCIAL` | `commands/20260825T073220-0300-can-scan.log`; `analysis/target-a-update-normal.json`; `commands/20260825T073404-0300-target-b-control-after-update.log` | Dois nós foram encontrados e o update ficou dirigido a A; conflito inicial, alteração exclusiva comprovável e dumps dos dois alvos não foram obtidos |
| L — Cannot Claim | `PENDENTE` | — | Faixa de endereços não foi exaurida |
| M — Journal e reset | `PENDENTE` | — | Alternância de endereço, reset e compactação não foram ensaiados |
| N — Carga e desempenho | `BLOQUEADO` | arquivos de `analysis/` | Há contagem de quadros, mas não há timestamps por quadro nem captura independente para tempo/throughput |
| O — Dados residuais | `BLOQUEADO` | — | Não houve sequência imagem maior/menor; a conexão SWD necessária aos dumps estava indisponível |

## Medições de atualização

Nenhum valor de desempenho é apresentado como resultado. Os tempos globais dos
três comandos foram registrados pelos wrappers — 4,853538 s no update normal,
4,749911 s no caso inválido e 3,880518 s na recuperação —, mas incluem Address
Claim, descoberta, IDENTIFY e outros controles. Como o log `--verbose` não traz
timestamp por quadro, esses valores não representam o intervalo definido entre
START_UPDATE e a resposta bem-sucedida de VERIFY.

| Repetição | Imagem | Resultado funcional | Quadros analisados | Tempo START_UPDATE--VERIFY | Vazão útil |
|---:|---|---|---:|---|---|
| 1 | válida, 7320 bytes | VERIFY `0x7BB303AE` | 1950 | não medido | não calculada |
| 2 | MSP inválido, 7320 bytes | FINALIZE rejeitado, NACK 11 | 1949 | não aplicável | não calculada |
| 3 | válida, recuperação, 7320 bytes | VERIFY `0x7BB303AE` e RUN solicitado | 1938 | não medido | não calculada |

## Desvios e limitações

- a conexão SWD falhou; não existem dumps das partições, leitura independente
  da metadata, confirmação do MCU ou inspeção do contexto de handoff;
- placa, transceptores, fonte, GND, topologia, terminações e resistência do
  barramento não foram registrados;
- não houve captura CAN independente. Os logs contêm somente quadros consumidos
  pela CLI e não possuem timestamps individuais;
- o ensaio normal usou um único tamanho de imagem, uma única repetição e o
  backend SLCAN no macOS;
- os dois alvos já continham a mesma aplicação no início. Por isso, o estado
  informado pelo alvo B e a ausência de comandos dirigidos a ele não substituem
  uma comparação de Flash antes/depois;
- não foram executados corte de energia, mudança persistente de endereço,
  conflito de Address Claim desde o reset, Cannot Claim, compactação do journal,
  carga concorrente ou análise de dados residuais;
- não houve fotografia, vídeo do LED, medição elétrica ou observação do botão
  de reset.

## Encerramento

| Campo | Valor |
|---|---|
| Estado final do alvo A | Identificado por `ls` em modo aplicação, NAME `0x80000000000E30EA`, endereço `0x80` |
| Estado final do alvo B | Identificado por `ls` em modo aplicação, NAME `0x80000000001719AF`, endereço `0x81` |
| Endereço persistido | Não ensaiado; apenas os endereços observados antes e depois foram iguais |
| Aplicação válida/executando | Informada pelo protocolo nos dois alvos ao final; não observada por LED ou SWD |
| Arquivos de captura | Logs `--verbose` em `commands/`; sem captura independente |
| Fotografias/esquema | Não registrados |
| Manifesto SHA-256 | Gerado após o fechamento desta ficha em 2026-08-25 |
| Responsável pela conferência | Não registrado |
