# Interrupção abrupta da ferramenta e recuperação

## Condição

Ensaio físico realizado em 29/09/2026 na Blue Pill identificada pelo NAME `0x80000000000E30EA`, endereço `0x80`. A NUCLEO `0x81` e a outra Blue Pill `0x82` permaneceram conectadas. Interface CANable2/SLCAN, CAN clássico a 500 kbit/s. A aplicação de referência possui 7776 bytes e CRC `0x62443C8D`.

O processo Python da ferramenta foi encerrado com SIGKILL depois que sua saída indicou 4000 de 7776 bytes transferidos. A saída de progresso é emitida após `write_block` retornar, conforme inspeção da ferramenta. Como pode haver quadros já enfileirados, 4000 bytes é o progresso observado no momento da injeção, não uma medição independente do último byte gravado.

SIGKILL foi escolhido para impedir a execução do tratador de cancelamento da ferramenta, que tenta enviar ABORT quando intercepta uma exceção. Este ensaio caracteriza encerramento abrupto do processo. Não representa o comportamento específico de Ctrl+C, nem corte físico de alimentação ou remoção do adaptador.

## Sequência e resultado

| Etapa | Resultado observado |
|---|---|
| Consulta inicial | Aplicação válida em execução, capabilities `0xF102803D`, tamanho 7776 e CRC `0x62443C8D`. |
| Interrupção | SIGKILL aplicado quando o progresso indicava 4000/7776 bytes; processo encerrado com retorno -9. |
| Descoberta posterior | Alvo `0x80` em modo bootloader, mantendo o NAME. Os dois outros participantes em modo aplicação. |
| Consulta posterior | Capabilities `0xF1028038`; metadados ainda informavam tamanho e CRC anunciados. |
| Pedido de execução | RUN_APPLICATION recusado: imagem inválida, código 11; ferramenta retornou 2. |
| Novo envio | Imagem completa recebida e verificada, CRC `0x62443C8D`; execução solicitada. |
| Descoberta final | Os três participantes em modo aplicação, com os mesmos NAMEs e endereços. |
| Atividade do alvo recuperado | 64 mensagens enfileiradas e 136 recepções periódicas válidas; TEC=0, REC=0 e zero observações de perda no FIFO na consulta. |

O tamanho e o CRC declarados nos metadados da imagem interrompida não constituem verificação da integridade de toda a imagem: a tentativa de execução foi explicitamente recusada. O novo envio foi necessário para recuperar a aplicação.

## Interpretação para o TCC

Após o encerramento abrupto da ferramenta durante a transferência, o dispositivo permaneceu acessível no modo de atualização e recusou a execução da imagem incompleta. Uma transferência completa posterior, realizada pelo mesmo barramento e sem reprogramação por SWD, restabeleceu a execução da aplicação e sua comunicação periódica. O resultado demonstra recuperação por novo envio no cenário ensaiado, sem restauração automática da aplicação anterior.

Foi executada uma tentativa de interrupção em uma Blue Pill. Não foram medidos o comportamento sob todas as fases possíveis de falha nem a continuidade temporal dos outros participantes por captura independente.

## Relato do autor e próximos casos

O autor relatou já ter testado encerramento da ferramenta, desconexão CAN/USB e interrupção da alimentação, observando permanência no bootloader. Neste conjunto, apenas o encerramento abrupto da ferramenta foi repetido e documentado. Os cortes físicos terão registros próprios.

## Evidências

`commands/` contém os oito registros e respectivos metadados. O retorno -9 do processo interrompido e o retorno 2 da tentativa de execução inválida são os resultados esperados das duas ações negativas. Os outros seis comandos terminaram com zero. `imagens/` preserva os binários copiados da rodada anterior; o arquivo STM32F103 foi o efetivamente utilizado. A revisão exata do bootloader residente permanece não confirmada por leitura independente.
