# Relação do CanLoader com o TCC

## 1. Objetivo deste registro

Este arquivo separa três coisas que não devem ser confundidas na escrita do
TCC II:

1. requisitos e intenções apresentados no trabalho acadêmico;
2. decisões concretas da implementação CanLoader;
3. itens ainda não demonstrados experimental ou normativamente.

O formato atual foi criado para aproximar a implementação do recorte J1939 do
TCC. Ele substituiu um protótipo interno de 11 bits, mas essa evolução não
transforma automaticamente o projeto em produto certificado SAE.

## 2. Matriz de aderência

| Tema do TCC | Situação atual | Evidência/limite |
|---|---|---|
| bootloader via CAN | implementado | update completo por CAN clássico a 500 kbit/s |
| STM32F103/Blue Pill | implementado como primeiro target | HAL/CMSIS/linker específicos; ensaiado em duas Blue Pill em 25/08, 29–30/09 e 04/11/2026 |
| NUCLEO-G0B1RE | port implementado | build, linker, FDCAN, Flash e aplicação de referência; ensaiado em bancada em 29–30/09 e 04/11/2026, com a cobertura descrita em [STM32G0B1RE.md](STM32G0B1RE.md) |
| separação hardware/protocolo | implementado | `Portable/Inc`, ports STM32F1/STM32G0 e módulos de protocolo independentes |
| J1939 de 29 bits | implementado no framing | CAN EFF, PGN, PDU1/PDU2 e prioridade 6 |
| gestão de nós J1939 | implementado como perfil | NAME, Request, Address Claimed, disputa, Cannot Claim e Commanded Address |
| DM14/DM15/DM16 | implementado como subset | ERASE/WRITE/STATUS/close; sem READ, EDCP ou seed-key |
| transporte multipacket | implementado | TP RTS/CTS para DM16; BAM para Commanded Address |
| múltiplos dispositivos | implementado e ensaiado | scanner por NAME, resolução de SA e bloqueio do alvo; três placas no mesmo barramento, cada uma atualizada com as outras transmitindo (29/09/2026) |
| entrada remota no bootloader | implementado | aplicação v2, backup registers e janela de 10 s |
| integridade da imagem | implementado | CRC-32/ISO-HDLC, metadata e vetores |
| não executar imagem parcial | implementado | marker `UPDATING/INVALID` antes do jump |
| rollback | não implementado | single-slot perde a imagem antiga após erase |
| segurança/autenticidade | não implementado | sem assinatura, MAC, criptografia ou anti-rollback |
| portabilidade a outro MCU | demonstrada em código para dois alvos | F103 e G0B1RE compilam; não implica suporte automático a outros STM32 |
| diferentes bitrates/CAN FD | não implementado | CAN clássico fixo em 500 kbit/s |
| validação experimental | realizada em parte | registros em `Validation/evidence/`; estado por ensaio na Seção 22 de [VALIDATION.md](VALIDATION.md) |

## 3. O que “atende à parte J1939” significa

É tecnicamente defensável afirmar que o v2 implementa um perfil de atualização
sobre mecanismos J1939 porque o wire format contém:

- identificadores estendidos de 29 bits;
- PGNs PDU1/PDU2 normalizados;
- NAME de 64 bits e Source Address;
- Address Claimed e Request;
- resolução de disputa pelo NAME;
- Commanded Address por TP BAM;
- TP.CM/TP.DT com RTS, CTS, EOM_ACK e ABORT;
- PGNs DM14 `0xD900`, DM15 `0xD800` e DM16 `0xD700`;
- Proprietary A `0xEF00` para funções específicas do projeto.

Não é tecnicamente defensável escrever que o CanLoader é “totalmente conforme
à SAE J1939” ou “certificado J1939-73”. As edições atuais completas são
licenciadas, o subset DM foi construído com apoio de revisão pública histórica
e existem escolhas deliberadamente proprietárias.

Formulação recomendada para o TCC:

> O CanLoader implementa um perfil de bootloading em CAN clássico baseado
> nas camadas de enlace e gestão de rede SAE J1939, utiliza um subconjunto de
> DM14/DM15/DM16 para acesso à memória e reserva um PGN Proprietary A para o
> ciclo de vida específico da imagem. Não foi realizada certificação formal de
> conformidade SAE.

## 4. Extensões CanLoader que precisam ser declaradas

O PGN Proprietary A carrega IDENTIFY, INFO, manifest, sessão, CRC total,
finalização, verificação, handoff, entrada no bootloader e aborto. Essa escolha
evita falsamente atribuir ao padrão mensagens que pertencem ao produto.

Também são regras próprias do perfil:

- bloco WRITE anunciado pelo target: 100 B no F103 e 96 B no G0;
- ERASE com length em unidades anunciadas por INFO: 1024 B no F103 e 2048 B no G0;
- ownership por SA/sessão;
- close repetindo pointer/length e timeout global de 5 s;
- códigos NACK `0..17`;
- uso dos bytes 2..4 de DM15 para `boot_error` CanLoader, byte 5 zero;
- seleção da faixa AAC `0x80..0xF7`;
- preferred SA default `0x80` e Tool `0xF9`;
- NAME de desenvolvimento derivado do UID96;
- limpeza da fila TX e cancelamento dos mailboxes antes de aplicar um novo SA;
- boot windows de 1,5 s e 10 s.

A higiene de TX na troca de endereço é necessária para a propriedade multinó:
o bootloader cancela os três mailboxes bxCAN administrados pela HAL, enquanto a
aplicação de referência cancela o mailbox 0 que usa diretamente. Assim, uma
resposta que ainda estava pendente não reaparece com a identidade de rota
anterior depois do novo Address Claimed.

Em particular, o conteúdo diagnóstico de DM15 não deve ser chamado de EDCP
normativo. Trata-se de extensão documentada, com Seed `0xFFFF`.

## 5. Mudança v1 → v2 no contexto acadêmico

O v1 foi uma prova funcional baseada em IDs standard e transporte próprio. O
v2 altera o objeto técnico do TCC II:

- remove IDs `0x600/0x580/0x700 + node`;
- remove `node_id`, `PROBE`, `SET_NODE_ID`, `BEGIN_BLOCK` e DATA próprios;
- introduz NAME/SA e descoberta baseada em claim;
- introduz TP clássico e DM14/15/16;
- permite identificar e atualizar um target específico em rede multinó;
- desloca a aplicação de `0x08004000` para `0x08005000`;
- aumenta o bootloader total para 20 KiB.

Essa é uma evolução incompatível, não uma opção de runtime. O histórico e as
razões estão em [MIGRATION_V1_TO_V2.md](MIGRATION_V1_TO_V2.md).

## 6. Mapa de memória e trade-off

O mapa concreto v2 é:

- 19 KiB de código do bootloader;
- 1 KiB de journal NAME/SA;
- 43 KiB de aplicação;
- 1 KiB de metadata.

Na versão `v1.0.0`, compilada em 30 de setembro de 2026, o bootloader ocupa
16548 bytes de Flash (`text=16536`, `data=12`) e deixa 2908 bytes,
aproximadamente 14,9%, na região de código. No STM32G0B1RE, ocupa 18968 dos
30720 bytes. Esses valores são uma fotografia do build; o linker de 19 KiB é o
requisito. O custo de
J1939/TP/claim reduziu a aplicação de 47 KiB no v1 histórico para 43 KiB no v2.

Duas imagens completas de 43 KiB não cabem em 64 KiB junto do bootloader. Por
isso a propriedade garantida é recuperação: uma imagem parcial não executa e o
bootloader permanece disponível. Rollback requer outro MCU, Flash externa,
compressão com garantias adicionais ou partições menores.

## 7. Segurança versus integridade

O TCC pode demonstrar robustez contra corrupção acidental por:

- CRC da imagem;
- CRC do cabeçalho e dos registros de configuração;
- markers gravados em ordem monotônica;
- readback após Flash;
- limites rígidos de região;
- validação do MSP/Reset Handler.

Isso não demonstra atualização segura contra atacante. CRC não substitui
assinatura. Uma conclusão correta deve listar secure boot, autenticação do Tool,
anti-rollback e proteção de chaves como trabalhos futuros ou requisitos de
produção.

## 8. Evidências necessárias para o TCC II

Além de testes automatizados, a entrega acadêmica previa registrar os itens
abaixo. A coluna de estado reflete as sessões de 25/08, 29–30/09 e 04/11/2026.

| Evidência | Estado |
|---|---|
| 1. diagrama físico, transceptores e terminação | registrado (foto e diagrama na monografia) |
| 2. captura de Address Claim e Request | parcial: observados nos logs `--verbose` da CLI, sem captura independente |
| 3. conflito de dois preferred SA com NAMEs distintos | observado em 04/11: a NUCLEO perdeu `0x80` para a Blue Pill de NAME menor e ficou em `0x81` |
| 4. `canloader ls` mostrando múltiplos NAMEs | registrado (três NAMEs) |
| 5. update de apenas um NAME e prova de que o outro não altera Flash | parcial: os outros participantes continuaram executando e transmitindo; a Flash deles não foi comparada |
| 6. captura TP de um bloco de 100 bytes com duas janelas CTS | parcial: visível nos logs `--verbose`, sem captura independente |
| 7. ERASE/WRITE/FINALIZE/VERIFY completos | registrado nos dois alvos |
| 8. interrupção de energia e recuperação | registrado: uma ocorrência na Blue Pill A, durante a transferência, além de SIGKILL da ferramenta e desconexão USB |
| 9. aplicação executando e comando remoto de retorno | registrado nos dois alvos |
| 10. Commanded Address e persistência após reset | pendente |
| 11. erros de CRC, sequência, sessão e timeout | parcial: testes automatizados; em bancada, só MSP nulo (25/08) e NAME divergente |
| 12. tamanho do binário, uso de RAM e tempo/throughput medidos | registrado (30/09 e 04/11; tempos com instantes do computador) |

A formulação correta é: a atualização dirigida, o retorno remoto e a
recuperação por novo envio foram demonstrados em bancada nas condições
ensaiadas; os itens parciais e pendentes acima continuam sem evidência física.

## 9. Referências e transparência

O texto acadêmico deve citar as páginas oficiais das edições usadas como
referência e registrar que o conteúdo integral não está redistribuído:

- SAE J1939/21_202205, Data Link Layer;
- SAE J1939/81_202504, Network Management;
- SAE J1939/73_202412, Application Layer — Diagnostics;
- J1939 Digital Annex vigente na data da entrega;
- documentação e implementação J1939 do kernel Linux como referência pública;
- revisão pública histórica J1939-73 usada para conferir DM14/15/16.

Uma revisão licenciada futura pode exigir ajustes de timers, valores reservados
ou semântica DM. Essa possibilidade deve aparecer como limitação, não ser
ocultada por nomenclatura.
