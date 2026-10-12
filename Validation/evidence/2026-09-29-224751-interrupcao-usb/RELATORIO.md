# Desconexão USB do CANable e recuperação

A sessão começou em 29/09/2026 e o corte documentado ocorreu em 30/09/2026, conforme os horários dos comandos. O alvo foi a Blue Pill da direita, NAME `0x80000000000E30EA`, endereço `0x80`. O autor confirmou que retirar o USB do CANable mantém as três placas alimentadas por outras conexões.

## Tentativa preparatória sem corte durante a transferência

A primeira transferência usou uma imagem de 44032 bytes, CRC `0x0B6A11BB`, e pausas de 250 ms entre blocos. Ela terminou integralmente às 22:49:54 de 29/09, antes da confirmação da desconexão. Foi verificada com retorno zero. Esse registro foi preservado, mas não conta como ensaio de interrupção. Não houve pedido de execução dessa imagem.

## Corte documentado em 30/09

A repetição utilizou a mesma imagem, com pausas de um segundo após cada bloco e nenhuma retransmissão. Essas pausas deram tempo à intervenção manual e tornam o tempo da operação inadequado para medir desempenho. A instrumentação está preservada em `instrumentacao-tentativa2.py`.

O autor retirou o USB do CANable mantendo as placas alimentadas. O último progresso registrado foi 1800/44032 bytes. A ferramenta terminou com erro de escrita serial, incluindo `Device not configured` e `Could not write to serial device`, retorno 2. A enumeração de portas após o corte confirmou a ausência do adaptador.

Após a reconexão do mesmo CANable, a descoberta encontrou a Blue Pill alvo em modo bootloader e as outras duas placas em modo aplicação. O alvo manteve NAME e endereço; informou capabilities `0xF1028038`. Os metadados ainda indicavam o tamanho e o CRC anunciados para a imagem interrompida. O pedido de execução foi recusado com erro 11, imagem inválida, e retorno 2 da ferramenta.

O novo envio da aplicação original de 7776 bytes foi concluído e verificado com CRC `0x62443C8D`. A descoberta final encontrou os três participantes em modo aplicação. A consulta de atividade do alvo registrou 64 mensagens enfileiradas, 137 recepções válidas, zero observações de perda no FIFO e TEC/REC iguais a zero naquele instante.

## Interpretação

No caso ensaiado, a perda da conexão USB da interface durante a transferência deixou o destinatário acessível no bootloader após o restabelecimento do enlace. O dispositivo bloqueou explicitamente a execução da imagem incompleta e voltou a executar uma aplicação após novo envio integral, sem acesso SWD.

O ensaio representa remoção do adaptador USB do computador. Não é um teste separado de abertura apenas dos fios CAN_H/CAN_L, nem uma caracterização elétrica da rede. A alimentação das placas durante a desconexão foi confirmada pelo autor, sem registro oscilográfico. Houve uma ocorrência documentada desse corte em uma Blue Pill; não se conclui comportamento em todos os instantes de atualização.

Os oito comandos e seus registros brutos estão em `commands/`; imagens, instrumentações e enumeração de portas foram preservadas. A primeira tentativa concluída permanece distinguida da repetição em que o corte efetivamente ocorreu.
