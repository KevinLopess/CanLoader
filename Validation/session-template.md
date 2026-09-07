# Sessão de validação física

## Identificação

| Campo | Valor |
|---|---|
| Identificador da sessão | `[PREENCHER]` |
| Início e fim, com fuso horário | `[PREENCHER]` |
| Operador | `[PREENCHER]` |
| Commit da ferramenta host e dos artefatos locais | `[PREENCHER]` |
| Revisão do bootloader residente | `[PREENCHER OU NÃO CONFIRMADA]` |
| Estado da árvore de código | `[PREENCHER]` |
| Sistema operacional | `[PREENCHER]` |
| Python e canloader | `[PREENCHER]` |
| Toolchain e STM32CubeProgrammer | `[PREENCHER]` |

## Bancada

| Item | Identificação e configuração |
|---|---|
| Alvo A: placa, MCU e UID96 | `[PREENCHER]` |
| Alvo A: NAME e endereço inicial | `[PREENCHER]` |
| Alvo B: placa, MCU e UID96 | `[PREENCHER OU NÃO UTILIZADO]` |
| Alvo B: NAME e endereço inicial | `[PREENCHER OU NÃO UTILIZADO]` |
| Transceptor(es) e tensão | `[PREENCHER]` |
| ST-LINK: modelo, firmware e serial | `[PREENCHER]` |
| Interface CAN: modelo, firmware e serial | `[PREENCHER]` |
| Backend, canal e baudrate serial | `[PREENCHER]` |
| Bitrate CAN | `500000 bit/s` |
| Fonte e tensão medida | `[PREENCHER]` |
| Terminações | `[PREENCHER]` |
| Resistência CAN_H--CAN_L desligado | `[PREENCHER]` |
| Equipamento de captura | `[PREENCHER]` |

## Artefatos

| Artefato | Tamanho | SHA-256 | CRC-32/ISO-HDLC |
|---|---:|---|---|
| Bootloader | `[PREENCHER]` | `[PREENCHER]` | `[PREENCHER]` |
| Aplicação de referência | `[PREENCHER]` | `[PREENCHER]` | `[PREENCHER]` |
| Outras imagens | `[PREENCHER]` | `[PREENCHER]` | `[PREENCHER]` |

## Verificações antes da energização

- [ ] polaridade e tensão dos transceptores conferidas;
- [ ] GND comum conferido;
- [ ] duas terminações nas extremidades físicas;
- [ ] resistência equivalente registrada com a rede desligada;
- [ ] ST-LINK e alvo confirmados pelo número de série;
- [ ] interface CAN e canal confirmados;
- [ ] hashes comparados com o baseline;
- [ ] captura independente iniciada, quando aplicável.

## Resultados

| Teste | Estado | Evidência | Resultado obtido e discussão curta |
|---|---|---|---|
| Quality gate/build | `PENDENTE` |  |  |
| A — Claim e framing | `PENDENTE` |  |  |
| B — INFO e identidade | `PENDENTE` |  |  |
| C — Update DM/TP | `PENDENTE` |  |  |
| D — CRC independente | `PENDENTE` |  |  |
| E — Handoff e autoboot | `PENDENTE` |  |  |
| F — Aplicação e reboot remoto | `PENDENTE` |  |  |
| G — Commanded Address | `PENDENTE` |  |  |
| H — Interrupção de energia | `PENDENTE` |  |  |
| I — Recuperação single-slot | `PENDENTE` |  |  |
| J — Erros de protocolo | `PENDENTE` |  |  |
| K — Dois dispositivos | `PENDENTE` |  |  |
| L — Cannot Claim | `PENDENTE` |  |  |
| M — Journal e reset | `PENDENTE` |  |  |
| N — Carga e desempenho | `PENDENTE` |  |  |
| O — Dados residuais | `PENDENTE` |  |  |

## Medições de atualização

Os instantes devem vir de uma captura CAN independente e temporizada. A duração
global do comando host e o log `--verbose` sem timestamp não substituem o
intervalo entre `START_UPDATE` e a resposta bem-sucedida de `VERIFY`.

| Repetição | Imagem (bytes) | Início | Fim | Tempo (s) | Vazão útil (byte/s) | Quadros | Retransmissões | Observação |
|---:|---:|---|---|---:|---:|---:|---:|---|
| 1 |  |  |  |  |  |  |  |  |
| 2 |  |  |  |  |  |  |  |  |
| 3 |  |  |  |  |  |  |  |  |
| 4 |  |  |  |  |  |  |  |  |
| 5 |  |  |  |  |  |  |  |  |

## Desvios e limitações da sessão

`[REGISTRAR FALHAS DE EQUIPAMENTO, ALTERAÇÕES DO PROCEDIMENTO, DADOS AUSENTES E
CONDIÇÕES QUE LIMITAM A GENERALIZAÇÃO DOS RESULTADOS]`

## Encerramento

| Campo | Valor |
|---|---|
| Estado final de cada alvo | `[PREENCHER]` |
| Endereço persistido | `[PREENCHER]` |
| Aplicação válida/executando | `[PREENCHER]` |
| Arquivos de captura | `[PREENCHER]` |
| Fotografias/esquema | `[PREENCHER]` |
| Manifesto SHA-256 gerado em | `[PREENCHER]` |
| Responsável pela conferência | `[PREENCHER]` |
