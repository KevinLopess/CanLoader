# Limitações conhecidas

## Recuperação single-slot, não rollback

O STM32F103C8Tx foi particionado com uma única área de aplicação. Antes de
apagá-la, o bootloader substitui a metadata anterior por uma metadata
`UPDATING`; somente CRC e vetores válidos permitem programar o marker `VALID`.
Uma interrupção deixa o dispositivo no bootloader e permite reenviar firmware,
mas a imagem anterior deixa de existir depois do erase.

Portanto, o mecanismo oferece **recuperação** contra execução parcial, não
rollback. Não existe slot A/B, cópia de backup ou restauração automática da
versão anterior.

## Integridade não é autenticidade

CRC-32/ISO-HDLC detecta corrupção acidental de transmissão ou Flash. Como o CRC
não usa segredo, um emissor com acesso ao barramento pode transmitir outra
imagem e calcular um CRC coerente. Esta versão não possui:

- assinatura digital ou cadeia de confiança;
- MAC, autenticação de host ou controle de acesso;
- criptografia/confidencialidade;
- anti-rollback ou contador monotônico;
- versão de imagem fornecida pelo host; `image_version` é gravado como zero.

O bootloader protege limites de memória, mas não torna segura uma rede CAN
hostil. Não use a implementação atual como raiz de confiança ou mecanismo de
secure boot.

## CAN clássico e protocolo proprietário

A implementação usa somente CAN clássico, payload máximo de 8 bytes e IDs
padrão de 11 bits. Não existe CAN FD, bit-rate switching, IDs estendidos ou
payload de 64 bytes.

**J1939-inspired, not SAE J1939 compliant**. Não existem PGNs, priority/source
address J1939, address claiming, DM14/DM15/DM16 reais, BAM ou RTS/CTS J1939.
Ferramentas J1939/ISOBUS não devem interpretar os IDs do CanLoader como PGNs.

O bitrate de 500 kbit/s e os timings estão fixos na configuração CubeMX do
target F1. Não há autodetecção ou negociação de bitrate.

## Um nó e uma sessão por instância

O node ID do firmware é definido em compile time; o cliente seleciona um nó por
execução. A máquina de estados mantém uma única sessão de update e um único
bloco ativo. Não há atualizações paralelas, retomada persistente de offset ou
gerenciador de múltiplas sessões.

Vários dispositivos podem ser compilados com IDs distintos no mesmo barramento,
mas esse cenário ainda precisa de validação em hardware. Broadcast é limitado a
DISCOVER/INFO e não possui janela aleatória, escalonamento ou coleta de respostas.
Um burst de respostas é possível; node IDs duplicados tornam respostas
indistinguíveis. A CLI atual não enumera nós por broadcast.

## Transporte e disponibilidade

- no F1, o bloco negociado de 100 bytes requer 20 frames DATA de 5 bytes;
- há ACK por bloco, sem janela ou controle de fluxo dinâmico;
- o host reenvia o bloco inteiro depois de falhas retryable;
- não existe retomada após reset: uma nova sessão começa do bloco zero;
- o bootloader processa no máximo oito frames recebidos por chamada de task;
- o buffer depende da FIFO bxCAN e da velocidade com que o laço principal roda;
- `RUN_APPLICATION` prioriza disponibilidade: depois de 250 ms faz o jump mesmo
  se o ACK ainda não tiver sido aceito/transmitido;
- bus-off, congestionamento, perda prolongada e carga com múltiplos nós não
  foram caracterizados em hardware;
- a CLI suporta inicialmente o backend SocketCAN, embora `python-can` permita
  injetar outros backends.

O host usa por padrão 0,5 s para ACK, 4 s para ERASE e três retransmissões; o
firmware encerra uma transferência inativa após 5 s. Esses valores não foram
caracterizados sob carga de barramento real.

## Persistência e desgaste

A metadata ocupa uma única página, sem cópia redundante. As três unidades de
marker independentes tornam as transições monotônicas depois do erase, e estados
parciais falham de modo seguro, mas não corrigem bit rot nem recuperam um
cabeçalho corrompido.

Cada atualização apaga a página de metadata e as páginas cobertas pelo novo
arquivo. Não há wear leveling nem contagem de ciclos. A durabilidade deve ser
avaliada conforme o perfil real de atualizações e os limites do componente.

## Target e portabilidade

Somente o port STM32F103C8Tx/bxCAN/Flash F1 está implementado. A checagem em
runtime garante pelo menos 64 KiB de Flash, mas não transforma outro STM32 em
target suportado. Um STM32G0 exige novo projeto CubeMX, HAL/CMSIS, startup,
linker, mapa, FDCAN, Flash e handoff, conforme
[ARCHITECTURE.md](ARCHITECTURE.md).

A palavra de capabilities informa o código do target, mas o arquivo `.bin` não
possui manifesto e a CLI não consegue provar que ele foi compilado para esse
MCU. Conferir target, origem da aplicação e linker do artefato continua sendo
responsabilidade operacional; CRC coerente não detecta firmware para o modelo
errado.

A unidade de marker acompanha a unidade mínima de programação configurada. Em um
novo MCU, o layout persistente e sua compatibilidade precisam ser revistos antes
de reutilizar metadata criada por outra família.

## Validação pendente

Hardware validation was not performed. Até a execução documentada da sessão
[A–H](VALIDATION.md), permanecem sem evidência de bancada:

- timings elétricos do barramento, transceptor e terminações;
- gravação e readback da Flash real;
- reset/perda de energia em diferentes instruções de erase/programação;
- entrega do ACK antes do jump;
- handoff, VTOR, MSP, IRQs e padrões de LED no MCU real;
- operação com mais de um nó e barramento carregado;
- desempenho, taxa de erro e comportamento em outros bitrates.

Os testes C usam ports simulados e os testes Python usam barramento injetado;
eles verificam regras lógicas, não a plataforma física.

As diferenças entre este escopo e as intenções do texto acadêmico estão
registradas em [TCC_DIVERGENCES.md](TCC_DIVERGENCES.md).
