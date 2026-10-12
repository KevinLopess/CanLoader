# Corte de alimentação durante a atualização e recuperação

## Condição

Ensaio realizado em 29/09/2026 na Blue Pill da direita, NAME `0x80000000000E30EA`, endereço `0x80`. A associação física foi conferida por desligamento prévio: somente esse NAME deixou de responder; a NUCLEO `0x81` e a outra Blue Pill `0x82` permaneceram presentes. Depois de religada para o início do ensaio, a placa informou a aplicação válida de 7776 bytes e CRC `0x62443C8D`.

Utilizou-se CANable2/SLCAN com CAN clássico a 500 kbit/s. Para ampliar o intervalo disponível ao corte manual, a aplicação foi preenchida com `0xFF` até 44032 bytes (capacidade informada do alvo), resultando no CRC `0x0B6A11BB`. Foram inseridas pausas de 250 ms após cada bloco concluído, pela instrumentação preservada em `instrumentacao-executada.py`. O procedimento foi configurado sem retransmissões. A execução instrumentada não serve como medição de desempenho.

## Intervenção e observações

Após a ferramenta indicar 1000 bytes transferidos, o autor recebeu a instrução de desligar somente a alimentação da Blue Pill da direita e confirmou a ação. O último progresso registrado foi 5200/44032 bytes. A tentativa do bloco no endereço `0x08006450` terminou por falta de resposta DM15; a ferramenta retornou 2. Não houve indicação de verificação da imagem completa.

Com a placa ainda desligada, a descoberta encontrou somente os dois outros participantes, ambos em modo aplicação. O autor então religou a alimentação do alvo.

| Verificação após religar | Resultado |
|---|---|
| Identidade | Mesmo NAME `0x80000000000E30EA` e endereço `0x80`. |
| Modo | Bootloader. |
| Capabilities | `0xF1028038`, sem os indicadores de aplicação válida/em execução observados antes do ensaio. |
| Metadados informados | 44032 bytes e CRC anunciado `0x0B6A11BB`; não equivalem a imagem validada. |
| Pedido de execução | RUN_APPLICATION recusado: imagem inválida, código 11; retorno 2 da ferramenta. |
| Novo envio | Aplicação original de 7776 bytes verificada com CRC `0x62443C8D`, seguida do pedido de execução. |
| Descoberta final | As três placas em modo aplicação, com as mesmas identidades. |

A consulta de atividade posterior à recuperação registrou recepções periódicas válidas do alvo; o log preserva os valores completos.

## Interpretação para o TCC

O corte manual da alimentação durante a transferência não levou à execução da imagem incompleta após o restabelecimento. O dispositivo permaneceu acessível no bootloader e recusou um pedido explícito de execução. O envio integral de uma imagem válida permitiu restabelecer a aplicação pela rede, sem intervenção SWD. A observação sustenta recuperação por novo envio no ponto de falha ensaiado; não representa rollback da aplicação anterior.

Foi executado um corte em um único alvo e durante a fase de transferência. O desligamento e a religação foram realizados e confirmados pelo autor; não houve medição da tensão com osciloscópio nem sincronização elétrica do instante do corte. O último progresso da ferramenta não define exatamente o último byte fisicamente gravado. Não foram ensaiados todos os instantes de apagamento, gravação de metadados e finalização, nem caracterizadas quedas parciais de tensão.

## Evidências

Os dez registros em `commands/` incluem a identificação física preliminar, a confirmação após religar, a transferência interrompida, consultas durante e após o desligamento, a tentativa negativa de execução, a recuperação e o estado final. Os retornos 2 da atualização interrompida e da execução recusada são esperados no ensaio. `sessao.json` documenta as condições e a confirmação do autor, cujo horário de registro não é uma medição do instante elétrico. As imagens e a instrumentação estão preservadas nesta pasta.
