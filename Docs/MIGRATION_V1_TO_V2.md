# Migração do protótipo de 11 bits para o protocolo atual

## 1. Decisão de formato único

As designações v1 e v2 foram usadas internamente durante o desenvolvimento e
não correspondem a lançamentos do CanLoader. O formato atual substitui
integralmente o protótipo de 11 bits. Firmware, aplicação de referência e
pacote Python não negociam nem aceitam os frames preliminares.

A ruptura foi escolhida para o TCC II porque manter duas pilhas simultâneas
aumentaria filtros, estados, testes e consumo de Flash, além de deixar ambíguo
qual protocolo forneceu identidade e autorização. A migração do próprio
bootloader é feita por SWD/ST-LINK; o protocolo de update continua impedido de
gravar sua partição.

> O CanLoader não é anunciado como produto certificado SAE. J1939-21/J1939-81 dão a
> base de enlace, endereçamento e TP; DM14/15/16 são um subconjunto baseado em
> material público histórico de J1939-73; ciclo de vida, sessão e erros internos
> permanecem extensões CanLoader.

## 2. Comparação direta

| Tema | v1 | v2-only |
|---|---|---|
| CAN-ID | standard 11 bits | extended 29 bits, CAN clássico |
| Identidade | `node_id` 1..127 | NAME J1939 de 64 bits |
| Rota | node embutido nos IDs | Source Address reivindicado |
| Target default | node 1 | preferred SA `0x80` |
| Host | sem identidade explícita | SA `0xF9` e NAME próprios |
| Descoberta | `PROBE` broadcast proprietário | Request `0xEA00` + Address Claimed `0xEE00` + IDENTIFY dirigido |
| Seleção | `--node` | `--name` para identidade e `--address` para rota |
| Reendereçamento | `SET_NODE_ID` | Commanded Address `0xFED8` via BAM |
| Controle | `0x600+node` | Proprietary A `0xEF00`, PDU1 dirigido |
| Resposta | `0x580+node` | Proprietary A `0xEF00`, sentido inverso |
| Erase/write | comandos próprios | DM14 `0xD900`, DM15 `0xD800`, DM16 `0xD700` |
| Fragmentação | DATA próprio, 5 bytes/frame | TP.CM/TP.DT RTS/CTS |
| Bloco | até 100 bytes | 100 bytes, último menor |
| Sessão | byte no protocolo inteiro | byte apenas em Proprietary A; DM key não é sessão |
| Versão | `1` | `2` |
| Aplicação | base `0x08004000`, 47 KiB | base `0x08005000`, 43 KiB |
| Config persistente | registro único de node | journal de NAME/preferred SA |

## 3. Motivações técnicas

### 3.1 Identidade separada da rota

No v1, alterar o node também alterava a identidade percebida. Dois targets com
o mesmo node eram indistinguíveis. No v2, NAME identifica o ECU e Source Address
é somente a rota atual. O host primeiro observa a associação NAME↔SA e bloqueia
o NAME antes de qualquer operação mutável.

O target anuncia preferred SA `0x80`, espera 250 ms e resolve conflito pelo
menor NAME numérico. Como seu NAME tem `AAC=1`, o perdedor percorre
sequencialmente `0x80..0xF7`; sem endereço disponível, anuncia Cannot Claim com
SA `0xFE` e não aceita update dirigido.

O NAME de laboratório deriva seu Identity Number do UID96 e usa Manufacturer
Code zero. Isso é determinístico para testes, não uma atribuição SAE de
produção.

### 3.2 Transporte reutilizável

O v1 tinha contador e fragmentação próprios. O v2 usa TP clássico:

- payloads até 8 bytes seguem em frame direto;
- DM16 com mais de 7 bytes crus usa RTS/CTS;
- a janela CTS do target é de no máximo 8 pacotes e diminui conforme o máximo
  do sender e os pacotes restantes;
- o TP alvo expira após 1250 ms sem progresso;
- EOM_ACK confirma remontagem, não programação da Flash;
- DM15 informa o resultado da operação de memória;
- Commanded Address, por ser global e ter 9 bytes, usa BAM.

O limite TP é 1785 bytes lógicos e o de DM16 é 1784 bytes crus, mas o updater
continua com 100 bytes para limitar RAM e simplificar retry/idempotência.

### 3.3 Separação de padrão e extensão

DM14/15/16 modelam acesso à memória; eles não carregam manifest CanLoader. O
PGN Proprietary A carrega:

- `IDENTIFY` e `INFO`;
- `START_UPDATE` e tamanho total;
- `SET_IMAGE_CRC`;
- `FINALIZE` e `VERIFY`;
- `RUN_APPLICATION` e `ENTER_BOOTLOADER`;
- `ABORT` e códigos NACK próprios.

O byte de sessão pertence a esse envelope. `DM14.Key/User_Level=0xFFFF`
expressa que o subset não implementa seed-key; não é um token de sessão.

Os bytes DM15 2..4 carregam, nesta implementação, o `boot_error` CanLoader de
24 bits; byte 5 é zero e Seed é `0xFFFF`. Essa convenção é diagnóstico
proprietário dentro do perfil e não deve ser interpretada como EDCP ou Error
Indicator atribuído pela SAE.

## 4. Alteração do mapa de memória

O build v2 atual excede a região antiga de 15 KiB de código em 1108 bytes. A
partição reservada ao bootloader aumentou de 16 para 20 KiB:

| Região | v1 | v2 |
|---|---:|---:|
| código | 15 KiB até `0x08003BFF` | 19 KiB até `0x08004BFF` |
| configuração | `0x08003C00` | `0x08004C00` |
| aplicação | `0x08004000`, 47 KiB | `0x08005000`, 43 KiB |
| metadata | `0x0800FC00` | `0x0800FC00` |

O build v2 medido usa `text=16456`, `data=12`, `bss=1648`; `text+data=16468`
deixa 2988 bytes na região de código de 19456 bytes. A medida deve ser repetida
a cada build de liberação.

Consequência operacional: qualquer aplicação compilada para `0x08004000`
precisa ser relinkada para `0x08005000`. O bootloader v2 rejeita seus vetores e
o host rejeita imagem maior que 44032 bytes.

## 5. Configuração persistente

O registro v1 de node não é interpretado no v2. A página agora contém journal
v2 de 32 slots × 32 bytes com magic `CLCF`, versão 2, geração, NAME, preferred
SA, byte inverso, flags, CRC e commit gravado por último.

Ao instalar o bootloader v2 por SWD, recomenda-se apagar toda a Flash para não
confundir dados antigos com uma aplicação relocada. Na ausência de registro v2
válido, o firmware cria defaults em RAM e reivindica `0x80`; o registro só é
gravado quando o endereço é alterado/provisionado.

## 6. Novo fluxo on-wire

```text
Tool                                                Target
 |--- Request(Address Claimed) ---------------------->|
 |<-- Address Claimed(NAME, SA) ----------------------|
 |--- IDENTIFY/INFO Proprietary A ------------------->|
 |--- START_UPDATE(size, session) ------------------->|
 |<-- ACK(block_size=100) ----------------------------|
 |--- SET_IMAGE_CRC(crc, session) ------------------->|
 |--- DM14 ERASE(APP_BASE, pages, key=FFFF) -------->|
 |<-- DM15 PROCEED -----------------------------------|
 |<-- DM15 COMPLETED ou FAILED -----------------------|
 |--- DM14 OPERATION_COMPLETED(pointer/pages) ------>|
 |                                                    |
 |    para cada bloco:                                |
 |--- DM14 WRITE(pointer, bytes, key=FFFF) ---------->|
 |<-- DM15 PROCEED(bytes) ----------------------------|
 |--- DM16 direto (1..7) ou TP RTS/CTS (8..100) ---->|
 |<-- TP EOM_ACK apenas no caminho multipacket -------|
 |<-- DM15 COMPLETED ou FAILED -----------------------|
 |--- DM14 OPERATION_COMPLETED(pointer/bytes) ------>|
 |                                                    |
 |--- FINALIZE(crc) --------------------------------->|
 |--- VERIFY ---------------------------------------->|
 |--- RUN opcional --------------------------------->|
```

O close DM14 v2 repete exatamente pointer e length da operação encerrada. Não
há timer separado de 100 ms implementado; operação e close ficam sob o timeout
global de inatividade do update de 5000 ms.

## 7. Mudanças na CLI

Exemplos v1 inválidos:

```text
--node 1
set-node
CAN-ID 0x601/0x581/0x701
```

Equivalentes v2:

```sh
canloader ls --interface slcan --channel /dev/cu.usbmodem...
canloader info --interface slcan --channel /dev/cu.usbmodem... \
  --address 0x80 --name 0x8000000000123456
canloader set-address 0x81 --interface slcan \
  --channel /dev/cu.usbmodem... \
  --address 0x80 --name 0x8000000000123456
```

No macOS o canal continua sendo a porta `/dev/cu.*`, não `can0`. SLCAN precisa
aceitar IDs estendidos; no protocolo Lawicel são frames `T`.

O NAME padrão do host é `0xA000000000000001`, uma identidade de laboratório
constante. Em uma rede com mais de uma ferramenta, cada host deve receber NAME
e SA próprios via `--source-name` e `--source-address`.

## 8. Mudanças na aplicação

A aplicação compatível v2 precisa:

1. ser linkada e ter vetores em `0x08005000`;
2. carregar o mesmo journal NAME/preferred SA;
3. executar Address Claim e responder ao Request correspondente;
4. responder a `IDENTIFY` e `INFO` pelo PGN `0xEF00`;
5. aceitar `ENTER_BOOTLOADER=0x33` dirigido;
6. gravar `0xB007`/`0x4C44` nos backup registers;
7. tentar transmitir ACK antes de resetar;
8. deixar que o bootloader consuma a assinatura e abra a janela de 10 s.

Ela não precisa aceitar DM14/15/16 enquanto executa. Update de Flash pertence
ao bootloader.

## 9. Mudanças em filtros e drivers

- `boot_can_frame_t` agora distingue ID standard/extended;
- o port transmite apenas `CAN_ID_EXT` e rejeita standard/RTR;
- filtros bxCAN aceitam destinos local e global; software valida PGN, SA e DA;
- o filtro precisa ser reconfigurado se Address Claim mudar o SA;
- bibliotecas e mocks devem preservar todos os 29 bits;
- um adaptador que trunca para 11 bits é incompatível.

## 10. Substituição dos testes

Vetores v1 de `0x581`, `0x601`, `0x701`, DATA de cinco bytes e node ID não são
evidência do v2. A suíte nova deve cobrir:

- PDU1/PDU2, PGN normalizado e IDs de 29 bits;
- NAME little-endian, Request, claim, disputa e Cannot Claim;
- descoberta de dois targets e bloqueio pelo NAME correto;
- Commanded Address por BAM e persistência em journal;
- TP RTS/CTS, múltiplas janelas, EOM_ACK, ABORT e timeout;
- DM14 ERASE/WRITE/STATUS/close, DM15 e DM16 direto/multipacket;
- limites 7, 8, 100, 1784 e 1785 bytes;
- rejeição de origem, destino, sessão, pointer, tamanho e sequência;
- mapa `0x08005000/0xAC00` e aplicação relocada;
- queda de energia nas transições da metadata;
- SLCAN `T` e SocketCAN extended.

## 11. Procedimento de migração

1. arquivar host, bootloader e formato persistente v1 para rastreabilidade;
2. compilar bootloader e aplicação v2 e conferir linkers/tamanho;
3. apagar a Flash inteira por SWD;
4. gravar `CanLoader.bin` em `0x08000000`;
5. energizar uma rede de bancada terminada corretamente;
6. executar `canloader ls` e registrar NAME/SA;
7. confirmar via `info` base `0x08005000`, máximo `44032` e versão 2;
8. enviar a aplicação v2 e executar `verify`/`run`;
9. testar `reboot` e nova descoberta do mesmo NAME;
10. testar conflito de SA e update dirigido em rede com dois targets.

## 12. Critério de conclusão

A migração só está completa quando:

- nenhum componente transmite ou aceita o wire format v1;
- `PROTOCOL_VERSION=2` em C e Python;
- aplicação e linkers usam `0x08005000`;
- `ls` usa Request/Address Claimed e IDENTIFY dirigido;
- update usa DM14/15/16 e TP;
- reendereçamento usa Commanded Address;
- operação mutável exige NAME esperado no host;
- testes e capturas mostram apenas IDs estendidos no fluxo v2;
- documentação distingue padrão, subset histórico e extensões proprietárias;
- não existe alegação de certificação SAE.
