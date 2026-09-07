# Plano de validação integrada do CanLoader

> **A validação em hardware foi iniciada, mas permanece parcial.** A sessão de
> 25 de agosto de 2026 está registrada em `Validation/evidence/`. Este documento
> também define os ensaios restantes e não constitui certificado de aprovação
> do sistema completo.

Esta campanha e suas evidências físicas atuais usam STM32F103C8Tx. O port
NUCLEO-G0B1RE possui uma matriz adicional em [STM32G0B1RE.md](STM32G0B1RE.md) e
não deve herdar resultados físicos do F103.

## 1. Objetivo

A sessão comprova, na ordem:

- build reprodutível e testes automatizados;
- mapa de memória do target;
- CAN de 29 bits e Address Claim;
- descoberta e seleção por NAME;
- update DM14/DM15/DM16 por TP;
- CRC, vetores, metadata e handoff;
- entrada remota no bootloader;
- Commanded Address persistente;
- conflito de SA com dois targets;
- interrupção, não execução parcial e recuperação.

Os testes são cumulativos. Uma falha invalida os passos dependentes até que a
causa seja corrigida e a sessão seja reiniciada em estado conhecido.

## 2. Materiais

- um ou, para o ensaio multinó, dois STM32F103C8Tx/Blue Pill;
- HSE de 8 MHz e LED PC13 ativo-baixo;
- transceptor CAN para cada STM32;
- ST-LINK e STM32CubeProgrammer CLI;
- interface USB–CAN com IDs estendidos e 500 kbit/s;
- CAN_H, CAN_L e GND comum;
- duas terminações de 120 Ω, uma em cada extremidade física;
- fonte estável e forma controlada de interromper alimentação;
- osciloscópio/analisador ou captura CAN quando disponível;
- GNU Make, GNU Arm Embedded Toolchain e Python 3.10+;
- `python-can`, `pyserial` e, em Linux, `can-utils`.

Com a rede desligada, cerca de 60 Ω entre CAN_H e CAN_L é compatível com duas
terminações de 120 Ω em paralelo. Essa medida não prova polaridade, alimentação
do transceptor ou integridade de sinal.

## 3. Registro da sessão

Antes de começar, registre:

| Campo | Valor |
|---|---|
| data/hora e operador | |
| commit/revisão do repositório | |
| MCU e identificação da placa | |
| transceptor e tensão | |
| adaptador/backend/canal | |
| sistema operacional e Python | |
| toolchain ARM | |
| bitrate medido/configurado | 500000 |
| NAME target A / preferred SA | / `0x80` |
| NAME target B / preferred SA | / `0x80` |
| NAME/SA da ferramenta | / `0xF9` |
| hashes dos `.bin` usados | |

Arquive também log `--verbose`, captura do barramento, saída de build, `.map`,
CRC dos binários e fotografias/esquema da bancada.

O diretório `Validation/` contém um modelo de sessão, um coletor do baseline de
software e um registrador de comandos. Cada execução deve receber uma pasta
própria em `Validation/evidence/`, sem reutilizar arquivos de outra revisão ou
placa. Os estados aceitos são PENDENTE, PARCIAL, APROVADO, REPROVADO e
BLOQUEADO; somente APROVADO pressupõe que todos os critérios foram atendidos e
que a evidência indicada está arquivada.

## 4. Quality gate sem hardware

O baseline pode ser executado e arquivado em uma única etapa:

```sh
./Validation/run_software_baseline.sh \
  Validation/evidence/AAAA-MM-DD-software-baseline
```

Os comandos seguintes detalham o conteúdo executado pelo coletor e continuam
úteis para diagnóstico individual.

Crie um ambiente limpo com Python 3.10 ou superior. Se a criação for
interrompida, remova apenas a `.venv` incompleta e repita o primeiro comando:

```sh
python3 --version
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e 'Tools/canloader[test]'
```

Execute:

```sh
make clean
make all
make test
```

Na árvore atual, `make test-c` executa nove binários: os seis originais e três
variantes que recompilam configuração, FSM e metadata com os parâmetros G0.
Confirme que todos foram executados, não apenas compilados:

```sh
build/tests/c/test_protocol
build/tests/c/test_j1939_address_claim
build/tests/c/test_j1939_tp
build/tests/c/test_device_config_v2
build/tests/c/test_bootloader
build/tests/c/test_metadata_image
build/tests/c/test_device_config_g0
build/tests/c/test_bootloader_g0
build/tests/c/test_metadata_image_g0
```

E rode explicitamente a suíte Python:

```sh
PYTHONPATH=Tools/canloader python -m pytest -q Tests/python
```

Na execução usada para fechar esta documentação, as nove suítes C terminaram
com `OK` e a suíte Python registrou `80 passed`. Esse baseline confirma a lógica
sem hardware; repita-o no commit entregue e arquive a saída correspondente.

Cobertura mínima esperada:

| Área | Evidência automatizada |
|---|---|
| ID/PGN/PDU1/PDU2/NAME | `test_protocol`, `test_j1939.py` |
| Address Claim e conflitos | `test_j1939_address_claim`, `test_address_claim.py` |
| TP RTS/CTS/BAM/timeout | `test_j1939_tp`, `test_transport.py` |
| DM14/15/16 | `test_protocol`, `test_protocol.py` |
| journal persistente | `test_device_config_v2` |
| FSM e owner/update | `test_bootloader` |
| mudança de SA e pedido de abort TX | `test_bootloader`; mailboxes reais nos testes G/K |
| metadata/imagem/vetores | `test_metadata_image` |
| scanner/update/retry | `test_client.py` |
| CLI e backends | `test_cli.py`, `test_bus.py` |

### 4.1 Tamanho e mapa

Registre `arm-none-eabi-size build/stm32f103c8/bootloader/CanLoader.elf`. A medição de
referência desta revisão é:

```text
text  = 16456 bytes
data  =    12 bytes
bss   =  1648 bytes
Flash = text + data = 16468 bytes
RAM estática = data + bss = 1660 bytes
margem na região de 19456 bytes = 2988 bytes (aprox. 15,4%)
```

Essa é uma fotografia da árvore usada para escrever este plano, não um valor
contratual. O critério é não exceder 19456 bytes de carga em Flash e não
sobrepor `0x08004C00`. Verifique no `.map` e nos ELF:

```sh
arm-none-eabi-size build/stm32f103c8/bootloader/CanLoader.elf
arm-none-eabi-objdump -h build/stm32f103c8/bootloader/CanLoader.elf
arm-none-eabi-objdump -h build/stm32f103c8/test-application/canloader_test_app.elf
```

Critérios:

- bootloader em `[0x08000000,0x08004C00)`;
- nenhuma seção carregável em `0x08004C00..0x08004FFF`;
- aplicação e `.isr_vector` em `0x08005000`;
- aplicação termina antes de `0x0800FC00`;
- metadata não aparece no `.bin` de aplicação.

Como referência adicional, a aplicação de teste desta árvore ocupa 7320 bytes
carregáveis (`text=7316`, `data=4`), 1296 bytes de `bss` e 1300 bytes de RAM
estática (`data+bss`); o critério continua sendo o limite do linker, não esse
valor medido.

## 5. Preparação do barramento

### 5.1 Linux/SocketCAN

```sh
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 up
ip -details -statistics link show can0
candump -L can0
```

Execute `candump` em outro terminal ou encerre-o com `Ctrl+C` antes de abrir uma
ferramenta que precise consumir os mesmos frames.

### 5.2 macOS/SLCAN

Liste portas antes e depois de conectar o adaptador:

```sh
ls /dev/cu.*
python -m serial.tools.list_ports -v
```

Prefira o dispositivo `/dev/cu.usbmodem*` ou `/dev/cu.usbserial*` que aparece
somente após conectar o adaptador. `/dev/tty.*` é a contraparte de entrada e não
é o canal recomendado pelo backend SLCAN no macOS.

Defina um array de argumentos. A expansão `"${CAN_ARGS[@]}"` funciona tanto no
`zsh` padrão do macOS quanto no `bash` e preserva cada opção como argumento
separado:

```sh
CAN_ARGS=(
  --interface slcan
  --channel /dev/cu.usbmodemXXXXXXXX
  --bitrate 500000
)
```

Substitua a porta pelo caminho realmente observado. Se o adaptador exigir
velocidade serial explícita, acrescente `--tty-baudrate 115200`; esse valor é a
velocidade da UART USB, enquanto `--bitrate 500000` é a velocidade CAN.

No Linux/SocketCAN, o array equivalente é:

```sh
CAN_ARGS=(--interface socketcan --channel can0)
```

Todos os comandos seguintes usam esse array.

No SLCAN, `--verbose` registra os frames consumidos pela própria CLI. Não abra
simultaneamente dois processos na mesma porta serial; para uma captura completa
e independente do barramento, use outro adaptador/analisador como segundo tap.

## 6. Estado inicial por SWD

O erase abaixo é intencional e remove bootloader, aplicação, configuração e
metadata do target selecionado. Confirme o ST-LINK correto e execute apenas no
ambiente de ensaio:

```sh
STM32_Programmer_CLI -c port=SWD -e all
STM32_Programmer_CLI -c port=SWD \
  -w build/stm32f103c8/bootloader/CanLoader.bin 0x08000000 -v -rst
```

Não grave a aplicação por SWD: ela deve chegar via CAN. Depois do reset, sem
metadata válida, PC13 deve alternar a cada 500 ms e o target deve permanecer em
`WAIT_HOST`.

## 7. TESTE A — Address Claim e framing

Inicie a captura antes de resetar. Espera-se, para SA `0x80` e prioridade 6:

```text
CAN-ID 0x18EEFF80, DLC 8, payload NAME little-endian
```

Critérios:

- frame marcado como extended/29-bit;
- data frame, não RTR;
- PGN normalizado `0xEE00`, DA global `0xFF`;
- NAME diferente de zero e `0xFFFFFFFFFFFFFFFF`;
- nenhum Proprietary A/DM operacional antes de 250 ms;
- novo Request global provoca retransmissão do claim.

Envie o scan:

```sh
canloader ls "${CAN_ARGS[@]}" --verbose
```

A captura deve incluir:

```text
0x18EAFFF9#00EE00FFFFFFFFFF   Request for Address Claimed do Tool default
0x18EEFF80#<NAME_LE64>        resposta do target
0x18EF80F9#0201SS0000000000   IDENTIFY dirigido, SS=session
0x18EFF980#028001SS<VALUE>    ACK dirigido
```

No SLCAN, confirme frames Lawicel `T`. Um `t` indicaria ID standard e falha.

## 8. TESTE B — INFO e identidade

Copie o NAME exibido pelo `ls`:

```sh
TARGET_NAME=0x8000000000123456   # substituir pelo valor observado

canloader info "${CAN_ARGS[@]}" \
  --address 0x80 --name "$TARGET_NAME" --verbose
```

Após erase total, a saída deve conter:

```text
Protocol version: 2
Address: 0x80
NAME: <TARGET_NAME>
Capabilities: 0xF1028038
Application base: 0x08005000
Application maximum size: 44032 bytes
Stored image size: 0 bytes
Stored image CRC: 0x00000000
Device UID: <96 bits>
```

`0xF1028038` pressupõe target `F1`, versão 2, SA `80`, flags J1939, claim e TP,
sem aplicação/update. Compare os três words UID e confirme que o NAME permanece
igual após múltiplos resets.

Teste negativo: passe um NAME diferente mantendo `--address 0x80`. A CLI deve
abortar antes de comando mutável com erro de identidade.

## 9. TESTE C — update completo sem RUN

```sh
canloader update build/stm32f103c8/test-application/canloader_test_app.bin \
  "${CAN_ARGS[@]}" --address 0x80 --name "$TARGET_NAME" --verbose
```

Critérios de fluxo:

1. START_UPDATE retorna bloco 100;
2. SET_IMAGE_CRC ecoa o CRC;
3. DM14 ERASE usa pointer `0x08005000` e page count;
4. target envia DM15 PROCEED e depois COMPLETED;
5. host fecha ERASE repetindo pointer e page count;
6. cada WRITE usa próximo pointer exato e 100 bytes, salvo o último;
7. DM16 de 100 bytes tem payload lógico 101 e 15 TP.DT;
8. target concede CTS 8 pacotes e depois 7;
9. EOM_ACK aparece antes de DM15 COMPLETED;
10. host fecha cada bloco repetindo pointer/length;
11. FINALIZE e VERIFY retornam o CRC;
12. sem `--run`, o target permanece em READY e LED apagado.

Um bloco de 100 bytes deve produzir, com Tool F9/target80:

```text
18EC80F9#1065000F0F00D700
18ECF980#110801FFFF00D700
18EB80F9#01FF......
... oito DT ...
18ECF980#110709FFFF00D700
... sete DT ...
18ECF980#1365000FFF00D700
```

Nos quatro TP.CM acima, os bytes 5..7 contêm o PGN DM16 em little-endian:
`00 D7 00`.

## 10. TESTE D — CRC independente

```sh
python -c "from pathlib import Path; import zlib; p=Path('build/stm32f103c8/test-application/canloader_test_app.bin'); d=p.read_bytes(); print(len(d), f'0x{zlib.crc32(d)&0xffffffff:08X}')"

canloader verify "${CAN_ARGS[@]}" --address 0x80 --name "$TARGET_NAME"
canloader info "${CAN_ARGS[@]}" --address 0x80 --name "$TARGET_NAME"
```

O CRC local, o retorno do update, VERIFY e Stored image CRC devem coincidir. O
tamanho persistido deve ser exatamente o tamanho do arquivo, sem padding TP ou
halfword.

## 11. TESTE E — handoff e autoboot

```sh
canloader run "${CAN_ARGS[@]}" \
  --address 0x80 --name "$TARGET_NAME" --verbose
```

O ACK deve ser observado quando o barramento permite. Em até 250 ms, a aplicação
deve executar o padrão:

1. LED ligado 80 ms;
2. desligado 100 ms;
3. ligado 80 ms;
4. desligado 740 ms.

LED continuamente aceso indica que a aplicação detectou `PRIMASK!=0` no
handoff e é falha crítica.

Pressione RESET e não envie controle por mais de 1500 ms. O bootloader deve
manter LED aceso na janela e iniciar automaticamente a mesma aplicação. O NAME
e SA anunciados pela aplicação devem ser os mesmos do bootloader.

## 12. TESTE F — aplicação, scan e reboot remoto

Com a aplicação executando:

```sh
canloader ls "${CAN_ARGS[@]}"
canloader info "${CAN_ARGS[@]}" --address 0x80 --name "$TARGET_NAME"
canloader reboot "${CAN_ARGS[@]}" \
  --address 0x80 --name "$TARGET_NAME" --verbose
```

Antes do reboot, capabilities deve conter `APPLICATION_VALID` e
`APPLICATION_RUNNING`: com defaults, `0xF102803D`. A aplicação deve ACKar
ENTER_BOOTLOADER, gravar backup registers e resetar. O bootloader deve anunciar
o mesmo NAME/SA e ficar disponível por 10 s. A CLI só conclui depois de observar
um claim novo e confirmar por IDENTIFY dirigido que `APPLICATION_RUNNING` foi
removido.

## 13. TESTE G — Commanded Address persistente

Escolha um endereço livre, por exemplo `0x81`:

```sh
canloader set-address 0x81 "${CAN_ARGS[@]}" \
  --address 0x80 --name "$TARGET_NAME" --verbose
```

A captura deve mostrar BAM de 9 bytes:

```text
18ECFFF9#20090002FFD8FE00
18EBFFF9#01<name0..6>
18EBFFF9#02<name7>81FFFFFFFFFF
18EEFF81#<NAME_LE64>
```

Critérios:

- apenas o NAME indicado muda;
- o target grava o journal e inicia novo claim sem reset obrigatório;
- a CLI só conclui depois de uma janela completa de claim, ocupante único no
  novo SA e IDENTIFY CanLoader dirigido na nova rota;
- se outro NAME menor já ocupar o novo SA, o target perde a arbitragem e a CLI
  reporta falha em vez de aceitar o claim transitório;
- a partir do novo Address Claimed, nenhuma resposta retirada da fila antiga é
  transmitida com o SA anterior;
- no bootloader, a instrumentação ou teste de port confirma abort dos três
  mailboxes HAL e limpeza da fila TX antes da reconfiguração dos filtros;
- `info --address 0x81 --name ...` retorna SA `0x81`;
- após reset físico, preferred SA continua `0x81`;
- enviar payload com NAME errado não altera o target;
- `0xFE` e `0xFF` são rejeitados pela CLI/target.

Repita o reendereçamento com a aplicação de teste em execução, usando outro SA
livre. Nesse caminho, confirme cancelamento do mailbox 0, limpeza da fila TX e
ausência de resposta antiga depois do novo claim. Um frame que já terminou de
ser transmitido antes do pedido de abort pode aparecer na captura e não
constitui, sozinho, vazamento posterior à transição.

Para os testes seguintes, defina `TARGET_SA` como o último SA efetivamente
observado; se não executar a repetição na aplicação, use `TARGET_SA=0x81`. Como
alternativa, retorne o target a `0x80` e registre essa nova transição.

## 14. TESTE H — interrupção de energia

Crie uma imagem válida preenchida até o limite atual de 43 KiB:

```sh
python -c "from pathlib import Path; p=Path('build/stm32f103c8/test-application/canloader_test_app.bin'); d=p.read_bytes(); assert len(d)<=0xAC00; Path('/tmp/canloader-interrupt.bin').write_bytes(d.ljust(0xAC00,b'\xff'))"
```

Inicie o update no SA atual e interrompa fisicamente o target depois de pelo
menos um bloco, antes de 44032 bytes:

```sh
canloader update /tmp/canloader-interrupt.bin "${CAN_ARGS[@]}" \
  --address "$TARGET_SA" --name "$TARGET_NAME" \
  --timeout 0.5 --retries 20 --verbose
```

Remova/restaure alimentação ou pressione RESET; não use apenas `Ctrl+C`, pois o
cliente tenta ABORT cooperativo. Depois do reboot:

- a aplicação parcial nunca deve executar;
- LED deve alternar em WAIT_HOST;
- o mesmo NAME deve reaparecer;
- INFO/capabilities não deve conter `APPLICATION_VALID` (tamanho e CRC
  declarados podem permanecer visíveis na metadata `UPDATING/INVALID`);
- RUN deve falhar com `INVALID_APPLICATION`.

## 15. TESTE I — recuperação single-slot

Sem SWD e sem apagar o bootloader:

```sh
canloader update build/stm32f103c8/test-application/canloader_test_app.bin \
  "${CAN_ARGS[@]}" --address "$TARGET_SA" --name "$TARGET_NAME" --run --verbose
```

O update precisa reiniciar do zero, validar, executar e voltar a dar autoboot
após reset. Isso demonstra recuperação; não demonstra rollback.

## 16. TESTE J — erros de protocolo

Use os testes automatizados ou uma ferramenta de injeção controlada para enviar:

- frame standard com bytes de IDENTIFY;
- Proprietary A para DA errado;
- versão diferente de 2;
- sessão zero ou owner diferente;
- DM14 com reserved bit zero, pointer object ou key diferente de `FFFF`;
- ERASE com `length` em bytes em vez de páginas;
- WRITE fora de ordem/partição;
- DM16 sem DM15 PROCEED;
- TP total size diferente de `DM14.length+1`;
- TP.DT repetido/fora de sequência;
- silêncio superior a 1250 ms durante TP;
- silêncio superior a 5000 ms durante update;
- CRC final incorreto.

Critérios: nenhum acesso fora da aplicação, nenhuma imagem parcial marcada
VALID, ABORT/DM15/NACK quando aplicável e retorno recuperável a WAIT_HOST.

## 17. TESTE K — dois dispositivos, mesmo preferred SA

Este teste é essencial para a pergunta “como atualizar apenas um dispositivo”.

1. grave o mesmo bootloader v2 em dois MCUs com UID96 diferentes;
2. apague o journal de ambos para preferred SA `0x80`;
3. conecte-os ao mesmo barramento;
4. capture o reset simultâneo;
5. execute `canloader ls`.

Antes de prosseguir, confirme que os NAMEs derivados são distintos. UID96
diferente torna isso provável, mas não garante unicidade depois da redução para
21 bits; uma colisão de NAME invalida o ensaio e exige provisionamento distinto.

Critérios:

- ambos começam reivindicando `0x80`;
- o menor NAME numérico permanece em `0x80`;
- o outro tenta um fallback livre, normalmente `0x81`;
- ambos ficam associados a NAMEs distintos;
- a captura mostra o conflito transitório e sua resolução;
- o target perdedor cancela respostas pendentes do SA disputado antes de
  anunciar o fallback;
- o resultado final de `ls` mostra os dois NAMEs em endereços distintos;
- cada IDENTIFY é dirigido ao SA resolvido.

Depois do `ls`, chame de B o target que perdeu `0x80`, copie o SA realmente
resolvido e o NAME exibido e selecione somente esse par:

```sh
TARGET_B_SA=0x81                     # substituir se o fallback observado diferir
TARGET_B_NAME=0x8000000000123456     # substituir pelo NAME de B
canloader update build/stm32f103c8/test-application/canloader_test_app.bin \
  "${CAN_ARGS[@]}" --address "$TARGET_B_SA" \
  --name "$TARGET_B_NAME" --run --verbose
```

Depois, verifique:

- metadata/CRC de B mudou e B executa a aplicação;
- A continua com o estado anterior;
- nenhum DM14/DM16 dirigido a B foi aceito por A;
- trocar `--name` de B pelo NAME de A com SA de B aborta no host.

Para prova forte, leia a Flash/metadata de ambos por ST-LINK após o ensaio e
compare hashes.

## 18. TESTE L — Cannot Claim

O caminho completo exige ocupar ou simular conflito em todos os endereços
`0x80..0xF7`. A suíte atual cobre disputa, um fallback e Cannot Claim para um
NAME sem AAC; ela não substitui o ensaio de exaustão da faixa AAC. Para tornar
este caso determinístico sem dezenas de ECUs físicas, crie um harness que
injete um NAME numericamente menor em cada SA candidato.

Critérios:

- após perder todos os candidatos, o target espera atraso 0..153 ms;
- transmite Address Claimed/Cannot Claim com SA `0xFE` e seu NAME;
- aparece no inventário como sem endereço operacional;
- não recebe update dirigido até adquirir endereço válido.

## 19. TESTE M — journal e reset

Alterne o endereço várias vezes e, após cada comando, faça reset físico:

```text
0x80 -> 0x81 -> 0x82 -> 0x83
```

Para testar compactação, conte os slots já usados em uma leitura SWD da página
`0x08004C00` e continue alternando entre endereços livres até provocar pelo
menos 33 gravações efetivas desde uma página vazia. Repetir o endereço atual não
cria registro novo.

Critérios:

- geração cresce e último slot válido vence;
- NAME permanece igual;
- registro anterior não é sobrescrito sem erase;
- interrupção antes do commit ignora o registro parcial;
- na 33ª gravação, quando os 32 slots já estão ocupados, a compactação apaga a
  página e preserva o novo valor se a operação concluir.

O ensaio de queda exatamente durante a compactação deve registrar a limitação:
é possível voltar aos defaults, pois existe apenas uma página de journal.

## 20. TESTE N — carga e desempenho

Meça pelo menos:

- tempo de claim até estado operacional;
- tempo de erase por quantidade de páginas;
- tempo total de update e throughput útil;
- quantidade de frames/retries;
- maior intervalo entre CTS e DT;
- utilização aproximada do barramento;
- comportamento com tráfego concorrente nas prioridades usadas;
- resposta a bus-off/desconexão/reconexão do adaptador.

Registre bitrate, tamanho da imagem e backend. Não generalize resultado de
SLCAN/macOS para SocketCAN/Linux sem medir ambos.

O tempo definido para a monografia começa no envio de `START_UPDATE` e termina
na resposta bem-sucedida de `VERIFY`. A duração total do processo da CLI inclui
Address Claim, descoberta e IDENTIFY, portanto não representa essa métrica. O
log `--verbose` atual também não possui timestamp por quadro. Use uma captura
CAN independente e temporizada para obter os dois instantes. No SLCAN, um
segundo adaptador ou analisador deve atuar como tap, pois dois processos não
podem consumir simultaneamente a mesma porta serial.

## 21. TESTE O — dados residuais após imagem menor

Este teste caracteriza a limitação de apagamento proporcional ao tamanho
declarado da imagem.

1. atualize uma imagem válida grande e arquive tamanho, CRC e hash;
2. leia por SWD os 64 KiB de Flash e preserve o dump integral;
3. atualize uma imagem válida menor;
4. faça nova leitura integral por SWD;
5. compare a região posterior à última página apagada pela imagem menor;
6. confirme por INFO e VERIFY que tamanho e CRC se referem somente à imagem
   nova.

Critérios:

- bootloader e configuração permanecem inalterados;
- a imagem menor é válida somente dentro de seu tamanho lógico;
- bytes antigos eventualmente preservados depois das páginas apagadas são
  registrados, mas não entram no CRC nem ampliam a faixa permitida ao Reset
  Handler;
- a discussão não apresenta a permanência desses bytes como vazamento de uma
  imagem aceita, nem omite a limitação de remanência física.

## 22. Resultado consolidado

O estado `PARCIAL` identifica procedimentos iniciados cujos critérios ainda não
foram completados. `BLOQUEADO` é reservado à parte que depende de equipamento,
instrumentação ou condição externa indisponível. A sessão física citada abaixo
não substitui os ensaios restantes. Nas linhas A a N, os caminhos abreviados partem de
`Validation/evidence/2026-08-25-hardware-preflight/`.

| Teste | Resultado | Evidência | Observações |
|---|---|---|---|
| quality gate/build | APROVADO | `Validation/evidence/2026-08-25-software-baseline/` | Build, seis executáveis C e 77 casos Python aprovados |
| A — claim/framing | PARCIAL | `commands/20260825T073220-0300-can-scan.log` | Dois claims estendidos observados pela CLI; faltam captura independente e estado inicial sem aplicação |
| B — INFO/identidade | PARCIAL | `commands/20260825T073230-0300-info-target-a.log`; `commands/20260825T073234-0300-info-target-b.log`; `commands/20260825T073509-0300-wrong-name-negative.log` | INFO e trava de NAME exercitados; sequência completa após erase/reset físico não foi executada |
| C — update DM/TP | PARCIAL | `commands/20260825T073342-0300-target-a-update-normal.log`; `analysis/target-a-update-normal.json` | Uma atualização de 7320 bytes concluiu; faltam leitura SWD e observação do LED |
| D — CRC | APROVADO | `commands/20260825T073459-0300-firmware-identification.log`; `commands/20260825T073342-0300-target-a-update-normal.log`; `commands/20260825T073401-0300-target-a-info-after-update.log`; `commands/20260825T073314-0300-target-a-verify-in-bootloader.log` | No bootloader residente, cuja revisão não foi confirmada, arquivo, update, VERIFY e INFO coincidiram em 7320 bytes e `0x7BB303AE` |
| E — handoff/autoboot | PARCIAL | `commands/20260825T073316-0300-target-a-run-application.log`; `commands/20260825T073324-0300-can-scan-after-roundtrip.log` | Retorno ao modo aplicação observado; LED, reset físico e registradores não foram registrados |
| F — aplicação/reboot | PARCIAL | `commands/20260825T073310-0300-target-a-reboot-to-bootloader.log`; `commands/20260825T073314-0300-target-a-verify-in-bootloader.log` | Reboot remoto e modo bootloader observados; janela completa de 10 s não foi caracterizada |
| G — Commanded Address | PENDENTE | | Nenhum endereço persistente foi alterado |
| H — interrupção | PENDENTE | | Nenhum corte de alimentação foi realizado |
| I — recuperação | PENDENTE | | Houve recuperação após imagem inválida, mas não após a interrupção definida no teste H |
| J — erros | PARCIAL | `commands/20260825T073509-0300-wrong-name-negative.log`; `commands/20260825T073648-0300-target-a-update-invalid-msp.log`; `commands/20260825T073702-0300-target-a-run-invalid-negative.log` | MSP nulo e NAME divergente exercitados; matriz de frames e falhas permanece incompleta |
| K — multinó dirigido | PARCIAL | `commands/20260825T073220-0300-can-scan.log`; `analysis/target-a-update-normal.json`; `commands/20260825T073404-0300-target-b-control-after-update.log` | Dois nós presentes e tráfego dirigido ao alvo A; não houve conflito inicial nem dump dos dois alvos |
| L — Cannot Claim | PENDENTE | | |
| M — journal | PENDENTE | | |
| N — desempenho | BLOQUEADO | `analysis/target-a-update-normal.json` | Há contagem de quadros, mas não havia captura externa com timestamps por quadro |
| O — dados residuais | BLOQUEADO | | A conexão SWD necessária à leitura da região estava indisponível |

Somente marque “aprovado” com log/captura correspondente. Uma conclusão válida
deve separar: testes automatizados, build embarcado, validação elétrica,
validação funcional e revisão normativa.
