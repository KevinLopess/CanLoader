# Limitações, riscos e fronteiras de conformidade

## 1. Estado normativo

CanLoader não é uma implementação certificada de SAE J1939 e não passou por
um programa formal de conformance/interoperability.

A implementação aceita somente o formato atual: não negocia outro formato, não
aceita frames CAN standard como comandos e não oferece compatibilidade
selecionável em runtime. Host, bootloader e aplicação precisam usar o mesmo
contrato. O valor interno de versão é 2, sem representar uma segunda versão
lançada do CanLoader.

O projeto usa:

- CAN clássico com Extended Frame Format de 29 bits, PDU1/PDU2 e TP coerentes
  com a camada de enlace J1939-21;
- NAME, Request, Address Claimed, disputa e Commanded Address coerentes com a
  gestão de rede J1939-81;
- formatos DM14/DM15/DM16 e parte do fluxo de acesso à memória baseados em uma
  revisão pública histórica de J1939-73;
- Proprietary A para tudo que é específico do ciclo de atualização.

As revisões atuais completas da SAE são licenciadas e não acompanham o
repositório. Antes de afirmar conformidade, seria necessário adquirir as
edições aplicáveis, revisar cada timer/valor reservado/estado e executar testes
de interoperabilidade. A documentação deliberadamente usa “perfil” ou
“subconjunto”, nunca “J1939-73 compliant”.

## 2. Subset DM e extensões

O target aceita somente:

- DM14 Direct Memory Addressing;
- `Key/User_Level=0xFFFF`;
- ERASE, WRITE, STATUS, close Operation Completed e Operation Failed como
  cancelamento best-effort da operação DM corrente;
- DM16 dirigido, direto ou por TP RTS/CTS;
- DM15 PROCEED, BUSY, COMPLETED e FAILED.

O campo de comprimento DM14/DM15 fica limitado a 1784 bytes; no fluxo de
update, a implementação restringe cada WRITE ao bloco anunciado (100 bytes no
F103, 96 no G0) e o ERASE ao número exato de unidades necessário para a imagem.

READ, Directed Spatial Addressing, Boot Load DM, EDCP Generation, DM18, seed-key
e chave longa não são implementados.

Em DM15, bytes 2..4 carregam o enum `boot_error` CanLoader, byte 5 vale zero e
Seed vale `0xFFFF`. Isso é um namespace diagnóstico proprietário deste perfil,
não um Error Indicator/EDCP normativo atribuído pela SAE. Um tester genérico
J1939 não deve inferir semântica padrão desses bytes.

O close Operation Completed repete pointer e length da operação anterior. Ele
fica sob o timeout global de update de 5 s; não há o timer DM separado de 100 ms
presente em algumas descrições históricas. Como não há ACK positivo, o host
envia `1 + retries` cópias e o target ignora silenciosamente duplicatas exatas;
perder todas as cópias ainda bloqueia a operação seguinte.

## 3. Segurança

CRC-32 detecta corrupção acidental; não autentica firmware nem remetente. Não
há:

- assinatura digital ou secure boot;
- MAC por mensagem;
- criptografia ou confidencialidade;
- proteção contra replay;
- anti-rollback/version monotônica;
- autenticação de Commanded Address;
- seed-key DM implementado;
- armazenamento protegido de chaves;
- autorização por NAME.

Qualquer participante com acesso ao barramento pode tentar interromper a rede,
reivindicar endereço, reendereçar o target ou instalar uma imagem cujo CRC ele
próprio calculou. O v2 deve ser usado em bancada ou rede fisicamente confiável.
Produção requer threat model, chaves, verificação de assinatura antes de marcar
VALID e política de recuperação.

## 4. Single-slot e disponibilidade

O layout não é A/B. Antes de apagar a aplicação, a metadata é marcada
`UPDATING`, impedindo boot de imagem parcial. Porém, após o erase, a imagem
anterior não existe mais.

Uma queda de energia durante update deixa o target recuperável pela CAN, mas
exige reenvio integral da imagem. Se CAN, transceptor, clock, Flash ou
alimentação estiverem defeituosos, a recuperação pode exigir SWD.

Não há resume persistente por offset: sessão, owner e bytes comprometidos vivem
em RAM. Após reset ou timeout, o update reinicia em `START_UPDATE` e ERASE.

## 5. Persistência de configuração

O journal NAME/SA usa uma página com 32 slots. Ele reduz erase a cada mudança,
mas quando enche apaga a página inteira. Falha de energia durante esse erase
pode eliminar o último registro válido; o dispositivo volta ao NAME derivado do
UID e preferred SA `0x80`.

O default tem `Manufacturer Code=0` e campos de função/sistema zero. Ele serve
somente para desenvolvimento e não garante atribuição global correta. O
repositório não oferece ainda comando remoto de provisionamento do NAME;
Commanded Address altera apenas o preferred SA.

O Identity Number default reduz `CRC32(UID96)` aos 21 bits disponíveis. Dois
UIDs diferentes podem colidir; portanto, o valor derivado não substitui
provisionamento nem uma verificação de unicidade na fabricação.

Não há wear leveling entre páginas, contador de erase ou proteção por write
option bytes. Reendereçamento frequente não é um caso de uso previsto.

## 6. Identidade e conflito

O host bloqueia o target por NAME, mas o bootloader associa a sessão mutável ao
Source Address e ao byte de sessão; ele não persiste o NAME do Tool no contexto.
Uma rede hostil pode falsificar NAME/SA. Dois ECUs provisionados com o mesmo NAME
são indistinguíveis e constituem erro de fabricação/configuração.

O fallback AAC do target é restrito à faixa de projeto `0x80..0xF7`, não a toda
a faixa dinâmica possível de uma instalação. Preferred SA do target (`0x80`) e
do Tool (`0xF9`) precisam ser verificados contra o Digital Annex e outros ECUs
da rede real.

O NAME default do host é constante (`0xA000000000000001`). Duas ferramentas
simultâneas com esse default conflitam; cada uma precisa de NAME e SA exclusivos.
Ao perder seu SA para NAME menor, o host AAC procura fallback e revalida o
target. Isso não migra uma sessão de update já pertencente ao SA antigo; o fluxo
deve ser reiniciado.

Ao mudar de SA, o bootloader descarta sua fila TX e solicita cancelamento dos
três mailboxes HAL; a aplicação de teste descarta a fila e solicita cancelamento
do único mailbox que usa, o mailbox 0. Isso evita reutilizar deliberadamente o
SA antigo em respostas pendentes, mas uma solicitação de abort não pode retirar
do barramento um frame cuja transmissão física já terminou. No bootloader,
falha imediata da HAL ao aceitar o abort leva a `ERROR`; a aplicação direta por
registradores não possui confirmação equivalente de erro do pedido `ABRQ0`.

## 7. TP e capacidade de rede

O receptor embarcado suporta uma sessão TP por vez. A biblioteca conhece o
limite clássico de 1785 bytes, mas o buffer DM16 alocado é dimensionado para o
bloco de update de 100 bytes mais o marcador, não para mensagens arbitrárias de
1785 bytes.

DM15 não transporta pointer nem a sessão proprietária. O host serializa as
operações e valida SA, DA, status e length, mas uma duplicata extremamente
tardia de outra operação com o mesmo length não pode ser correlacionada de modo
absoluto somente pelos campos DM15. Essa é uma limitação do perfil atual, não um
mecanismo de autenticação ou anti-replay.

Limitações adicionais:

- ETP e payloads acima de 1785 bytes não são suportados;
- CAN FD e fast packet não são suportados;
- firmware não usa BAM, pois não haveria flow control/ACK;
- BAM recebido é aceito apenas para Commanded Address de 9 bytes;
- o target não transmite DM16 nem implementa TP sender geral;
- janela CTS fixa máxima de 8;
- o receptor limita cada concessão a essa janela, porém a camada TP não recebe
  confirmação de que o CTS já deixou o mailbox bxCAN. Um emissor que despeje
  TP.DT além da concessão antes de receber o CTS seguinte pode ter esses frames
  aceitos se já estiverem na FIFO; emissores conformes devem aguardar cada CTS;
- timeout TP do target fixo em 1250 ms;
- sequência inesperada pode gerar reason genérico de recursos, não toda a
  taxonomia de ABORT conhecida pelo host;
- frames TP fora da expectativa podem ser ignorados sem ABORT;
- uma fila TX de oito frames pode descartar uma resposta se estiver cheia.

O sistema não foi dimensionado para alto bus load. Erase de Flash e CRC são
síncronos; durante essas operações, latência CAN aumenta. Antes de uso em rede
veicular, medir ocupação, prioridade, deadlines e comportamento bus-off.

## 8. Timers e relógio

Timers implementados são escolhas do perfil/target:

| Timer | Valor |
|---|---:|
| Address Claim | 250 ms |
| Cannot Claim | pseudoaleatório 0..153 ms |
| progresso TP target | 1250 ms |
| update/close sem atividade | 5000 ms |
| boot window normal | 1500 ms |
| boot solicitado pela app | 10000 ms |
| atraso normal RUN/reset | 50 ms |
| limite absoluto RUN/reset | 250 ms |

O tick é de 32 bits e comparações usam subtração modular; durações curtas
atravessam wrap corretamente. Não existe sincronização temporal entre ECUs.

O cliente tem defaults próprios (`timeout=0,5 s`, erase `4 s`, retries 3), que
não substituem os timers do target. Links lentos, adaptadores SLCAN com buffering
ou barramento congestionado podem exigir ajuste no host.

## 9. Hardware e camada física

O firmware não detecta automaticamente bitrate, terminação, polaridade ou
topologia. O target está fixo em 500 kbit/s. É obrigatório usar transceptor CAN,
GND comum e terminação correta.

Não foram implementados:

- detecção/recovery avançada de bus-off além do ABOM/HAL disponível;
- monitor de tensão antes de erase/programação;
- brown-out transaction log dedicado;
- watchdog de atualização;
- redundância da metadata;
- proteção do bootloader por option bytes/RDP/WRP configurada pelo projeto;
- diagnóstico detalhado de erros físicos CAN.

Clones de Blue Pill podem anunciar geometria de Flash diferente ou conter MCU
não genuíno. O port exige pelo menos 64 KiB detectados, mas isso não qualifica o
componente nem garante endurance.

## 10. Imagem e handoff

A validação confere tamanho, CRC, MSP e Reset Handler. Ela não analisa:

- todas as entradas da vector table;
- compatibilidade de ABI/periféricos;
- versão mínima/máxima do hardware;
- dependências de configuração;
- assinatura do fabricante;
- comportamento após começar a aplicação.

O ACK de RUN/ENTER_BOOTLOADER é *best effort*: o firmware espera 50 ms e TX
ocioso quando possível, mas força jump/reset em 250 ms. O host pode observar
timeout apesar de o dispositivo ter executado a ação.

A aplicação precisa ser linkada em `0x08005000`. Um `.bin` não carrega endereço
de destino; cabe ao processo de build garantir a relocação correta.

## 11. Host e adaptadores

Python 3.10+ é exigido. No macOS, SLCAN usa `/dev/cu.*`; não há `can0` nativo.
Nem todo adaptador USB-CAN implementa corretamente IDs estendidos ou todos os
bitrates. A CLI não configura terminação e não eleva privilégios.

O backend raw implementa J1939 em user space. No Linux, usar simultaneamente
SocketCAN raw e sockets `CAN_J1939` com o mesmo NAME/SA pode causar disputa e
sessões TP concorrentes.

O scanner coleta durante uma janela finita; uma ECU silenciosa, em Cannot Claim
ou atrasada pode não aparecer. `ls` descobre dispositivos J1939 e depois tenta
IDENTIFY; ausência de resposta não prova que o ECU não existe.

`--address` identifica a rota atual, não a identidade permanente. A opção
`--name` torna a expectativa explícita; quando ela é omitida, o cliente ainda
resolve o claim naquele endereço e trava o NAME observado antes de operações
mutáveis. Isso reduz erro operacional, mas não autentica o target.

## 12. Cobertura de testes

Testes nativos e Python cobrem lógica sem hardware, mas mocks não reproduzem:

- arbitragem bit a bit real;
- erro de ACK, error passive e bus-off;
- jitter/latência do adaptador;
- falha de energia no meio de um halfword físico;
- endurance de Flash;
- EMI, transientes e queda de alimentação;
- boot em temperatura/tensão extremas;
- interoperabilidade com ECUs J1939 de terceiros.

O status “build e testes passam” não substitui a sessão em placa de
[VALIDATION.md](VALIDATION.md).

## 13. Riscos prioritários antes de produção

| Prioridade | Risco | Mitigação necessária |
|---:|---|---|
| 1 | firmware não autenticado | secure boot e assinatura de manifest/imagem |
| 2 | perda de energia remove imagem anterior | A/B ou recovery image imutável |
| 3 | NAME/endereçamento de laboratório | provisionamento de produção e atribuições SAE |
| 4 | subset DM não validado contra edição atual | revisão licenciada e conformance test |
| 5 | erase síncrono em rede carregada | medir latência e redesenhar agendamento |
| 6 | página única de configuração/metadata | redundância e estratégia de endurance |
| 7 | falta de WRP/RDP formal | política de option bytes e processo de fábrica |
| 8 | ausência de watchdog/brown-out auditado | supervisão de energia e reset |

## 14. Uso aceitável desta revisão

Esta revisão é apropriada para protótipo acadêmico/TCC e bancada controlada,
desde que:

- o operador conheça a natureza single-slot;
- o barramento seja confiável e fisicamente controlado;
- exista ST-LINK para recuperação;
- cada target tenha NAME distinto;
- toda aplicação seja construída para o mapa v2;
- capturas e testes em hardware sejam registrados;
- nenhuma alegação de segurança ou certificação seja feita.
