# Ensaio piloto de atualização com três placas

Sessão: `2026-09-29-222857-bancada-guiada`. Coleta em 29/09/2026, com horários e fuso nos logs.

## Montagem e procedimento

O autor informou duas Blue Pill e uma NUCLEO-G0B1RE conectadas. O computador enumerou um CANable2, serial 207431855842, na porta `/dev/cu.usbmodem2074318558421`. Foi utilizado SLCAN com CAN clássico a 500 kbit/s. Esta sessão não é uma validação de CAN FD. O USB também enumerou uma interface STM32 STLink; nenhuma operação SWD foi executada.

Foram realizadas descoberta, consultas de informações, duas rodadas iniciais de contadores, uma atualização por placa e consultas das três placas após cada atualização. Os alvos foram selecionados por endereço e NAME. Os binários foram copiados para `imagens/` antes do envio. Todos os 32 comandos registrados terminaram com código zero.

As imagens reenviadas têm tamanho e CRC coincidentes com os metadados informados antes do ensaio. O procedimento demonstra retransmissão, verificação e retorno à aplicação; não demonstra troca entre duas versões de comportamento distinto. A revisão exata do bootloader residente não foi identificada por leitura independente.

## Resultados das atualizações

| Alvo lógico | Endereço | NAME | Imagem | CRC verificado | Tempo total do comando | Resultado |
|---|---|---|---:|---|---:|---|
| Blue Pill A | 0x80 | 0x80000000000E30EA | 7776 bytes | 0x62443C8D | 5.695 s | CRC conferido; aplicação observada posteriormente |
| NUCLEO | 0x81 | 0x80000000001663E2 | 14204 bytes | 0x1835F363 | 6.266 s | CRC conferido; aplicação observada posteriormente |
| Blue Pill B | 0x82 | 0x80000000001719AF | 7776 bytes | 0x62443C8D | 5.640 s | CRC conferido; aplicação observada posteriormente |

Os tempos acima incluem abertura da interface, descoberta, transição de modo, controles da ferramenta, transferência, verificação e solicitação de execução. Não são tempos exclusivos da transferência e não foram utilizados para calcular vazão útil. São três ensaios individuais, um por placa, sem estimativa de confiabilidade estatística.

## Comunicação antes e depois

| Consulta de atividade | TX enfileiradas | RX periódicas válidas | Envios adiados | Observações de perda no FIFO |
|---|---:|---:|---:|---:|
| 03-activity-1-bluepill-a | 1566 | 3103 | 0 | 0 |
| 03-activity-1-bluepill-b | 1603 | 3211 | 32 | 0 |
| 03-activity-1-nucleo | 1604 | 3169 | 0 | 0 |
| 03-activity-2-bluepill-a | 1669 | 3309 | 0 | 0 |
| 03-activity-2-bluepill-b | 1706 | 3417 | 32 | 0 |
| 03-activity-2-nucleo | 1708 | 3375 | 0 | 0 |
| 04-pos-bluepill-a-activity-bluepill-a | 288 | 585 | 0 | 0 |
| 04-pos-bluepill-a-activity-bluepill-b | 2450 | 4882 | 32 | 0 |
| 04-pos-bluepill-a-activity-nucleo | 2420 | 4768 | 0 | 0 |
| 05-nucleo-activity-bluepill-a | 550 | 1090 | 0 | 0 |
| 05-nucleo-activity-bluepill-b | 2719 | 5380 | 32 | 0 |
| 05-nucleo-activity-nucleo | 133 | 270 | 0 | 0 |
| 06-bluepill-b-activity-bluepill-a | 806 | 1583 | 0 | 0 |
| 06-bluepill-b-activity-bluepill-b | 203 | 406 | 0 | 0 |
| 06-bluepill-b-activity-nucleo | 390 | 762 | 0 | 0 |

Em todas as consultas, TEC e REC foram zero e os indicadores de bus-off, estado passivo e advertência estavam desativados. As observações de perda no FIFO também foram zero. Antes da sua atualização, a Blue Pill B tinha 32 tentativas de envio adiadas, valor já presente na primeira leitura e que não aumentou nas leituras seguintes; esse campo não é uma contagem de quadros perdidos. Após reiniciar sua aplicação, o contador passou a zero.

Os contadores das aplicações selecionadas reiniciaram após suas atualizações e voltaram a registrar transmissão enfileirada e recepção. Nos demais participantes, os valores consultados continuaram avançando. Isso sustenta atividade observada antes e depois; sem captura independente contínua, não se conclui ausência de pausas intermediárias. As leituras dos campos são sequenciais, e os deltas não devem atravessar o reinício de uma aplicação.

A descoberta final encontrou as três identidades nos mesmos endereços e em modo application. Não foi feita comparação integral da Flash dos participantes não selecionados.

## Texto factual aproveitável na reescrita

Foi realizado um ensaio piloto com duas placas Blue Pill e uma NUCLEO-G0B1RE conectadas ao mesmo barramento CAN clássico, configurado em 500 kbit/s. As três aplicações foram identificadas pela ferramenta e apresentaram mensagens periódicas. Cada dispositivo foi selecionado individualmente para o reenvio da aplicação correspondente. As três operações concluíram a transferência e a verificação do CRC, sendo posteriormente observado o retorno dos participantes ao modo aplicação e a recepção de mensagens periódicas. As consultas realizadas nos outros participantes após cada atualização mostraram avanço dos contadores de atividade. A campanha compreendeu uma tentativa por placa e não incluiu captura independente contínua do barramento.

## Próximas etapas

- Confirmar qual interrupção o autor já testou: alimentação, comunicação, encerramento da ferramenta ou reset.
- Documentar fotografia, modelos dos transceptores, alimentação e terminações da montagem.
- Planejar e registrar um ensaio de interrupção e recuperação com fase da interrupção identificada.
- Considerar uma segunda aplicação compatível e distinguível para demonstrar mudança funcional de versão.
- Após fixar o procedimento, executar as repetições escolhidas e registrar inclusive as falhas.
- Reescrever resultados, método e conclusão com o mesmo escopo de evidências; corrigir o posicionamento das figuras diagnosticado na revisão.

## Evidências e rastreabilidade

Os arquivos `commands/*.json` contêm argumentos, horários, duração total, retorno e caminho do log integral. `sessao.json` identifica a revisão local e os hashes dos binários encontrados; `estado-git.txt` e `diff-local.patch` registram alterações locais dos arquivos rastreados. Esses arquivos não equivalem a uma comprovação da revisão de todo o software residente. Os binários efetivamente reenviados estão em `imagens/`. Os logs brutos foram mantidos, inclusive linhas de progresso intercaladas com mensagens de depuração.

Nenhum corte de alimentação, teste negativo de imagem ou teste de falha foi executado nesta sessão. A condição de integração relatada pelo autor permanece pertinente: o retorno remoto depende de uma aplicação que implemente o comando correspondente.
