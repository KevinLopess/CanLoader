# CanLoader

Bootloader para microcontroladores STM32 que atualiza o firmware de **uma placa
escolhida** em uma rede CAN compartilhada, enquanto as demais continuam em
operação. A placa é selecionada pela identidade (NAME) do SAE J1939, e não por
um endereço fixo; a imagem é transferida em blocos, gravada sem passar inteira
pela RAM e só é executada depois de conferidos o tamanho, o CRC e os vetores de
inicialização.

O projeto reúne três programas:

- o **bootloader**, em C, residente no início da Flash de cada placa;
- uma **aplicação de referência**, usada para exercitar a atualização e o
  retorno remoto ao bootloader;
- a **ferramenta `canloader`**, em Python, que descobre as placas e conduz a
  atualização a partir do computador.

Desenvolvido como Trabalho de Conclusão de Curso de Engenharia de Computação na
UTFPR, Campus Pato Branco (2026). A versão [`v1.0.0`](https://github.com/KevinLopess/CanLoader/tree/v1.0.0)
corresponde ao código avaliado na monografia.

**Demonstração em vídeo:** <https://youtu.be/uh3T_99d-og>

## Sumário

- [Recursos](#recursos)
- [Alvos suportados](#alvos-suportados)
- [Início rápido](#início-rápido)
- [Uso da ferramenta](#uso-da-ferramenta)
- [Como a atualização funciona](#como-a-atualização-funciona)
- [Mapas de memória](#mapas-de-memória)
- [Integração em uma aplicação](#integração-em-uma-aplicação)
- [Indicação por LED](#indicação-por-led)
- [Validação](#validação)
- [Limitações](#limitações)
- [Estrutura do repositório](#estrutura-do-repositório)
- [Documentação](#documentação)
- [Como citar](#como-citar)

## Recursos

- CAN clássico a 500 kbit/s, com quadros estendidos de 29 bits.
- Descoberta das placas por *Request* / *Address Claimed* e identificação
  dirigida.
- Seleção do destinatário pelo NAME, mesmo quando o endereço (SA) muda.
- Transferência dirigida com DM14, DM15 e DM16 sobre o transporte J1939
  (RTS/CTS), um bloco por vez.
- Aceitação da imagem condicionada a tamanho, CRC-32/ISO-HDLC e validação dos
  vetores iniciais (MSP e *Reset Handler*).
- Metadados persistentes: uma imagem incompleta não é executada após uma
  interrupção, e um novo envio pela rede recupera a placa.
- Entrada remota no bootloader a partir da aplicação em execução.
- Mudança persistente do endereço preferido por *Commanded Address* (BAM).
- Núcleo comum sem dependência de HAL, com um adaptador por microcontrolador.
- Sem sistema operacional, sem alocação dinâmica.
- Ferramenta para SLCAN (macOS) e SocketCAN (Linux).
- Testes de software em C e em Python, executados sem hardware.

> **Alcance normativo.** O CanLoader implementa um perfil próprio que usa
> mecanismos de J1939-21 e J1939-81 e um subconjunto de DM14/DM15/DM16 baseado
> em material público de J1939-73. Não é certificado pela SAE e não deve ser
> apresentado como implementação integral dessas normas. Sessão, CRC,
> finalização e troca de modo são comandos próprios no PGN *Proprietary A*
> (`0xEF00`).

## Alvos suportados

| Item | STM32F103C8Tx (Blue Pill) | NUCLEO-G0B1RE |
| --- | --- | --- |
| Núcleo | Cortex-M3 | Cortex-M0+ |
| Clock | 72 MHz, PCLK1 36 MHz | 64 MHz, PCLK1 64 MHz |
| Controlador CAN | bxCAN | FDCAN1 em modo clássico |
| *Bit timing* | prescaler 4, BS1 13, BS2 4, SJW 1 | prescaler 8, TSEG1 13, TSEG2 2, SJW 2 |
| Pinos CAN | PA11 RX, PA12 TX | PC4 RX, PC5 TX (AF3) |
| LED | PC13, ativo em nível baixo | PA5 (LD4), ativo em nível alto |
| Flash | página de 1 KiB, programação de 2 bytes | página de 2 KiB, programação de 8 bytes |
| Bloco de dados | 100 bytes | 96 bytes |

O microcontrolador precisa de um transceptor CAN externo (a bancada usa o
SN65HVD230). Ligue CAN_H, CAN_L e a referência de terra, com 120 Ω em cada
extremidade física do barramento. Os pinos de TX e RX não podem ser ligados
diretamente ao par diferencial.

Antes de gravar a NUCLEO, leia [Docs/STM32G0B1RE.md](Docs/STM32G0B1RE.md).

## Início rápido

### Pré-requisitos

- GNU Make e `arm-none-eabi-gcc`. O Makefile procura primeiro o toolchain do
  STM32CubeIDE e, depois, `arm-none-eabi-*` no `PATH`.
- Python 3.10 ou superior.
- ST-LINK com STM32CubeIDE ou STM32CubeProgrammer, apenas para a instalação
  inicial do bootloader.
- Um adaptador USB–CAN compatível com SLCAN ou SocketCAN.

### 1. Compilar

```sh
make all                      # bootloader e aplicação de teste de todos os alvos
make TARGET=stm32g0b1re all   # somente a NUCLEO-G0B1RE
make bootloader               # somente o bootloader do alvo padrão (F103)
```

| Comando | Saída |
| --- | --- |
| `make bootloader` | `build/<alvo>/bootloader/CanLoader.{elf,bin,hex,map,list}` |
| `make test-application` | `build/<alvo>/test-application/canloader_test_app.{elf,bin,hex,map}` |
| `make APP_VARIANT=2 test-application` | mesma aplicação com outro padrão de LED, em `test-application-2/` |

### 2. Gravar o bootloader (uma única vez)

Use as configurações `CanLoader STM32F103C8Tx.launch` e
`CanLoader NUCLEO-G0B1RE.launch` do STM32CubeIDE. O passo a passo está em
[Docs/CUBEIDE_LAUNCH.md](Docs/CUBEIDE_LAUNCH.md). Depois disso, a aplicação é
substituída somente pela rede.

### 3. Instalar a ferramenta

```sh
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e Tools/canloader
canloader --version
```

### 4. Descobrir as placas e atualizar uma delas

```sh
PORT=/dev/cu.usbmodemXXXX     # porta serial do adaptador SLCAN

canloader ls --interface slcan --channel "$PORT" --bitrate 500000
```

```text
ADDR  NAME                MFG   FUNC  CANLOADER  MODE
0x80  0x80000000000E30EA     0     0  yes        application
0x81  0x80000000001663E2     0     0  yes        application
0x82  0x80000000001719AF     0     0  yes        application
```

```sh
canloader update build/stm32f103c8/test-application/canloader_test_app.bin \
  --interface slcan --channel "$PORT" --bitrate 500000 \
  --address 0x82 --name 0x80000000001719AF --run
```

`--address` é a rota observada no momento; `--name` é a identidade. Se a placa
perder uma disputa de endereço e mudar de SA, execute `ls` de novo e use o
endereço anunciado para o mesmo NAME.

## Uso da ferramenta

### Comandos

| Comando | Efeito |
| --- | --- |
| `canloader ls` | Solicita *Address Claimed* e identifica os nós CanLoader. |
| `canloader info` | Lê modo, memória, UID, NAME, endereço e dados da imagem. |
| `canloader update IMAGEM.bin` | Abre a sessão, apaga, escreve, finaliza e verifica. Com `--run`, executa a aplicação. |
| `canloader verify` | Pede à placa que recalcule e valide a imagem gravada. |
| `canloader run` | Executa a aplicação, se a imagem for válida. |
| `canloader reboot` | Pede à aplicação que reinicie no bootloader. |
| `canloader set-address NOVO_SA` | Envia *Commanded Address* ao NAME selecionado. |
| `canloader activity` | Lê os contadores de atividade CAN da aplicação de teste. |

Parâmetros comuns: `--address`, `--name`, `--source-address`, `--source-name`,
`--timeout`, `--transport-timeout`, `--erase-timeout`, `--scan-timeout`,
`--retries` e `--verbose`. A referência completa está em
[Tools/canloader/README.md](Tools/canloader/README.md).

### macOS, com SLCAN

No macOS não há interface `can0`: o canal é a porta serial do adaptador.
Compare `ls /dev/cu.*` antes e depois de conectá-lo e prefira `/dev/cu.*` a
`/dev/tty.*`.

Todos os quadros do protocolo são estendidos. Em uma captura SLCAN, aparecem
como quadros `T`; quadros `t`, de 11 bits, são tráfego alheio.

### Linux, com SocketCAN

```sh
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 up

canloader ls --interface socketcan --channel can0
```

A ferramenta usa quadros CAN brutos e implementa a reivindicação de endereço e
o transporte em espaço de usuário. Não compartilhe o mesmo endereço de origem
entre ela e um *socket* `CAN_J1939` do kernel.

## Como a atualização funciona

```text
Request / Address Claimed + IDENTIFY      descoberta e conferência de NAME e SA
  -> START_UPDATE(tamanho)                abertura da sessão
  -> SET_IMAGE_CRC(crc32)                 CRC esperado da imagem
  -> DM14 ERASE / DM15                    apaga só as páginas que a imagem ocupa
  -> para cada bloco:
       DM14 WRITE / DM15 PROCEED
       DM16 (direto, 1 a 7 bytes; ou via TP RTS/CTS, 8 a 100 bytes)
       DM15 COMPLETED                     bloco gravado e relido
  -> FINALIZE                             tamanho, CRC na Flash e vetores
  -> VERIFY
  -> RUN (opcional)
```

- A placa informa o tamanho do bloco na resposta a `START_UPDATE`: 100 bytes
  no F103 e 96 bytes no G0. Um bloco de 100 bytes vira uma mensagem DM16 de
  101 bytes, transportada em 15 quadros, em duas janelas CTS.
- A escrita é estritamente sequencial e só é aceita na região de aplicação.
- Antes do apagamento, os metadados passam a indicar atualização em andamento.
  A partir desse ponto, a imagem anterior deixa de existir: uma falha deixa a
  placa no bootloader, à espera de um novo envio.
- O marcador de validade só é gravado depois de conferidos o tamanho, o CRC
  recalculado sobre a Flash e os vetores iniciais.

O contrato completo das mensagens está em [Docs/PROTOCOL.md](Docs/PROTOCOL.md).

## Mapas de memória

### STM32F103C8Tx (64 KiB)

| Região | Intervalo | Tamanho |
| --- | ---: | ---: |
| Código do bootloader | `0x08000000..0x08004BFF` | 19 KiB |
| Configuração (NAME e SA) | `0x08004C00..0x08004FFF` | 1 KiB |
| Aplicação | `0x08005000..0x0800FBFF` | 43 KiB (44032 bytes) |
| Metadados | `0x0800FC00..0x0800FFFF` | 1 KiB |

### STM32G0B1RE (512 KiB)

| Região | Intervalo | Tamanho |
| --- | ---: | ---: |
| Código do bootloader | `0x08000000..0x080077FF` | 30 KiB |
| Configuração (NAME e SA) | `0x08007800..0x08007FFF` | 2 KiB |
| Aplicação | `0x08008000..0x0807F7FF` | 478 KiB (489472 bytes) |
| Metadados | `0x0807F800..0x0807FFFF` | 2 KiB |

A aplicação deve ser ligada no início da sua região: `0x08005000` no F103 e
`0x08008000` no G0. Uma imagem ligada para outro endereço é recusada pela
validação dos vetores. Os valores vêm dos cabeçalhos de cada alvo e são
conferidos pelos arquivos de ligação; a ferramenta os lê da placa. Detalhes em
[Docs/MEMORY_MAP.md](Docs/MEMORY_MAP.md).

## Integração em uma aplicação

Para ser atualizada a partir do seu estado de execução, a aplicação precisa:

1. ser ligada no endereço da região de aplicação e apontar o VTOR para ele;
2. usar o mesmo NAME e o mesmo endereço preferido do bootloader, e executar a
   reivindicação de endereço;
3. responder a `IDENTIFY` e `INFO`;
4. aceitar `ENTER_BOOTLOADER` (`0x33`) no PGN *Proprietary A* dirigido: gravar
   a assinatura `0xB007/0x4C44` nos registradores de *backup*, tentar entregar
   a confirmação e reiniciar.

O bootloader consome essa assinatura uma única vez e permanece disponível por
10 s. A aplicação de referência em [TestApplication/](TestApplication/)
implementa esses quatro pontos e serve de modelo.

```sh
canloader reboot --interface slcan --channel "$PORT" \
  --address 0x80 --name 0x80000000000E30EA
```

## Indicação por LED

| Estado | LED da placa |
| --- | --- |
| Janela de início, com imagem válida | aceso por até 1,5 s (até 10 s após pedido da aplicação) |
| Aguardando a ferramenta | alterna a cada 500 ms |
| Preparação, apagamento, recepção ou verificação | alterna a cada 125 ms |
| Imagem pronta, antes do salto | apagado |
| Erro de inicialização | alterna a cada 100 ms |
| Aplicação de referência (variante 1) | duas piscadas curtas por segundo |
| Aplicação de referência (variante 2) | aceso continuamente |
| Aplicação de referência (variante 3) | três piscadas curtas por segundo |
| Aplicação iniciada com interrupções mascaradas | aceso continuamente |

Na variante 2, o LED aceso coincide com o sinal de falha na passagem para a
aplicação. Para distinguir os dois casos, confirme com `canloader ls` que a
placa responde em modo aplicação.

## Validação

### Testes de software

```sh
make test                # portabilidade, testes C e testes Python
make check-portability   # falha se HAL/LL vazar para o núcleo comum
make test-c              # testes nativos, em build/tests/c/
make test-python         # ferramenta: protocolo, transporte, cliente e CLI
```

Na versão `v1.0.0`: 10 executáveis de teste em C e 86 casos `pytest`
aprovados. A cobertura de código não foi medida.

### Ensaios em bancada

Duas Blue Pill e uma NUCLEO-G0B1RE no mesmo barramento, com um adaptador
CANable V2.0 Pro por SLCAN. Cada sessão está registrada em
[Validation/evidence/](Validation/evidence/), com a saída completa de cada
comando.

| Ensaio | Resultado |
| --- | --- |
| Atualização dirigida das três placas, com as outras duas enviando mensagens a cada 100 ms | Três atualizações concluídas, CRC conferido, contadores de erro CAN em zero |
| NAME de uma placa com o endereço de outra | Recusado pela ferramenta antes de qualquer operação |
| Interrupção por encerramento da ferramenta, corte de alimentação e desconexão USB | Placa de volta no bootloader, execução recusada e aplicação recuperada por novo envio |
| Imagem com ponteiro de pilha inicial nulo (ensaio preliminar) | Transferida, mas recusada na finalização |

| Medida | Valor |
| --- | --- |
| Atualização completa, com descoberta e controles | 4,6 a 6,3 s |
| Vazão útil | 6,1 a 7,8 kB/s |
| Apagamento da Flash | cerca de 22 ms por página |
| Bootloader | 16548 bytes (F103) e 18968 bytes (G0) |

Esses resultados são funcionais: uma atualização por placa e uma ocorrência de
cada interrupção, em um único alvo, com tempos medidos no computador. O plano e
os critérios estão em [Docs/VALIDATION.md](Docs/VALIDATION.md).

## Limitações

- **Sem autenticação.** O CRC detecta corrupção acidental, não uma imagem
  adulterada; endereços e mensagens podem ser falsificados por um participante
  malicioso.
- **Imagem única.** Não há reversão à versão anterior: depois do apagamento, a
  recuperação depende de um novo envio.
- **Proteção por software.** A escrita é restrita à região de aplicação pelo
  protocolo, não por proteção de hardware da Flash.
- **Sem certificação SAE** e sem CAN FD.
- **NAME de desenvolvimento.** É derivado do identificador único do STM32 e não
  garante unicidade global; em produção, as identidades precisam ser atribuídas
  de forma controlada.
- **Avaliação restrita à bancada**, com poucas repetições.

A lista completa, com riscos e fronteiras de conformidade, está em
[Docs/LIMITATIONS.md](Docs/LIMITATIONS.md).

## Estrutura do repositório

```text
Application/             máquina de estados, configuração, metadados, imagem e salto
Protocol/                J1939, Address Claim, transporte, comandos CanLoader e CRC
Portable/Inc/            contratos de CAN, Flash e plataforma
Portable/stm32f103c8/    adaptador HAL / bxCAN / Cortex-M3
Portable/stm32g0b1re/    adaptador HAL / FDCAN / Cortex-M0+
Config/                  mapa, NAME padrão, limites e temporizadores
Targets/<alvo>/          inicialização, startup e arquivo de ligação de cada alvo
TestApplication/         aplicação de referência, compilada para cada alvo
Tools/canloader/         pacote Python e linha de comando
Tests/                   testes C e Python, sem hardware
Validation/              scripts de registro e evidências dos ensaios
Docs/                    protocolo, arquitetura, memória e histórico
```

A HAL e o CMSIS são usados somente na inicialização e nos adaptadores de cada
alvo. A portabilidade vem dos contratos em `Portable/Inc`: um novo
microcontrolador exige implementar esses contratos, revisar o *startup* e o
arquivo de ligação e repetir os ensaios.

## Documentação

| Documento | Conteúdo |
| --- | --- |
| [IMPLEMENTACAO_BOOTLOADER.md](Docs/IMPLEMENTACAO_BOOTLOADER.md) | Descrição técnica completa da implementação |
| [PROTOCOL.md](Docs/PROTOCOL.md) | Contrato das mensagens no barramento |
| [ARCHITECTURE.md](Docs/ARCHITECTURE.md) | Arquitetura e portabilidade |
| [MEMORY_MAP.md](Docs/MEMORY_MAP.md) | Mapas de memória e formatos persistentes |
| [STM32G0B1RE.md](Docs/STM32G0B1RE.md) | Adaptação para a NUCLEO-G0B1RE |
| [CUBEIDE_LAUNCH.md](Docs/CUBEIDE_LAUNCH.md) | Gravação e depuração pelo STM32CubeIDE |
| [VALIDATION.md](Docs/VALIDATION.md) | Plano de validação |
| [LIMITATIONS.md](Docs/LIMITATIONS.md) | Limitações, riscos e conformidade |
| [MIGRATION_V1_TO_V2.md](Docs/MIGRATION_V1_TO_V2.md) | Mudança do protótipo de 11 bits para o formato atual |
| [TCC_DIVERGENCES.md](Docs/TCC_DIVERGENCES.md) | Relação entre o código e a monografia |
| [HISTORY.md](Docs/HISTORY.md) | Histórico de desenvolvimento e dos commits |

## Como citar

> LOPES, Kevin Azevedo. **Desenvolvimento de um *bootloader* em rede CAN para
> atualização de *firmware* em microcontroladores STM32**. 2026. Trabalho de
> Conclusão de Curso (Bacharelado em Engenharia de Computação) – Universidade
> Tecnológica Federal do Paraná, Pato Branco, 2026.

Orientador: Prof. Dr. Gustavo Weber Denardin.

Os *drivers* em [Drivers/](Drivers/) são da STMicroelectronics e da Arm e
mantêm as licenças indicadas em seus diretórios.
