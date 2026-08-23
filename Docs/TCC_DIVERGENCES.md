# Divergências e delimitações em relação ao TCC

O arquivo `TCCKevinLopes.pdf` apresenta objetivos e trabalho previsto; a
implementação atual fixa um primeiro recorte executável para o STM32F103C8Tx.
Esta tabela evita que expectativas de evolução do TCC sejam apresentadas como
funcionalidades já entregues.

| Tema | Intenção descrita no TCC | Implementação atual |
|---|---|---|
| Robustez/rollback | mecanismo à prova de falhas e capacidade de rollback | uma partição de aplicação; imagem parcial nunca executa e o nó é recuperável, mas a imagem anterior é perdida após erase |
| Segurança | atualização segura e evolução para SHA-256/assinatura | CRC, vetores e limites de Flash; sem autenticação, assinatura, criptografia ou anti-rollback |
| J1939 | J1939 e DM14/DM15/DM16 como referência e futura aderência maior | protocolo proprietário inspirado nos conceitos; CAN 11-bit, sem PGN/DM/TP J1939 |
| Múltiplos dispositivos | atualização individual em rede e avaliação com múltiplos nós | node ID persistente, UID, scan e update unicast estão implementados; address claiming e validação física multinó ainda estão pendentes |
| CAN e bitrate | avaliar diferentes baud rates e versões do CAN | somente bxCAN clássico a 500 kbit/s e frames de 11 bits |
| Portabilidade | separar núcleo e camada de abstração, exemplificada como `port.c/.h` | separação mais granular em `boot_can`, `boot_flash` e `boot_platform`; somente o port STM32F1 existe |
| Ferramenta host | Python 3.12 e `python-can` | pacote declara Python `>=3.10`, usa `python-can` e documenta SocketCAN e SLCAN |
| Hardware previsto | Blue Pill STM32F103C8T6, MCP2551, ST-LINK/V2 e CANable v2 | pinagem/configuração foi preparada para o STM32F103C8Tx; nenhuma combinação física foi validada nesta entrega |
| Validação experimental | bancada, erros, interrupção, robustez e desempenho | existe procedimento A–J e testes sem hardware; os ensaios físicos e as medições ainda estão pendentes |

## Decisões do primeiro target

### Mapa de memória

O TCC propõe definir o mapeamento durante o projeto. A implementação o
concretiza como 15 KiB de código do bootloader, 1 KiB de configuração persistente,
47 KiB de aplicação e 1 KiB de metadata.
A página retirada do fim da aplicação é necessária para distinguir de modo
persistente imagens `UPDATING`, `VALID` e `INVALID`.

### Recuperação em vez de A/B

Manter simultaneamente duas imagens de até 47 KiB não cabe nos 64 KiB do target.
A implementação privilegia um bootloader protegido e sempre recuperável. Essa
decisão satisfaz a exigência de não executar firmware incompleto, mas não satisfaz
rollback para a imagem anterior.

### Protocolo próprio

O TCC usa J1939 como referência conceitual e também prevê CAN clássico de 11 bits
na etapa inicial. Como J1939 normalmente estrutura identificadores estendidos e
serviços que não estão presentes aqui, declarar conformidade seria incorreto.

**J1939-inspired, not SAE J1939 compliant**

Uma futura implementação J1939 deve ser uma nova camada de transporte ou versão
de protocolo, com IDs de 29 bits, regras de endereçamento e mensagens reais do
padrão; não apenas uma renomeação dos IDs atuais.

### Validação

O TCC prevê ensaios em hardware real. Eles não foram executados durante esta
implementação/documentação. O texto correto até que o operador conclua e registre
[VALIDATION.md](VALIDATION.md) é: **Hardware validation was not performed.**
