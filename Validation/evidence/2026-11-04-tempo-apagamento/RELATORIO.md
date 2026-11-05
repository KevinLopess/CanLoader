# Sessão 2026-11-04 — tempo de apagamento da Flash

## Objetivo

Medir na bancada a duração do apagamento da região de aplicação nos dois alvos,
para substituir as estimativas calculadas a partir dos datasheets
(STM32F103C8: 20–40 ms por página de 1 KiB; STM32G0B1RE: 22 ms típicos e
40 ms máximos por página de 2 KiB).

## Montagem

Mesma bancada das sessões de 29/09: duas Blue Pill (0x80 e 0x82, STM32F103C8)
e uma NUCLEO-G0B1RE (0x81) no mesmo barramento, CANable2/SLCAN em
`/dev/cu.usbmodem2074318558421`, CAN clássico a 500 kbit/s, placas alimentadas
pelo USB, temperatura ambiente sem controle. O firmware das placas não foi
alterado. Estado do repositório em `estado-git.txt`.

## Método

Cada atualização foi executada com `Validation/run_timestamped_canloader.py`,
que chama a CLI do `canloader` sem alterações e acrescenta o instante do host
(resolução de microssegundos) a cada linha do log `--verbose`. O script
`Validation/erase_timing.py` localiza o DM14 ERASE enviado pela ferramenta e
mede o intervalo entre o DM15 PROCEED e o DM15 OPERATION_COMPLETED recebidos do
alvo. Pelo `bootloader.c` (`service_erase`), o apagamento só começa depois que o
PROCEED sai da fila de transmissão, e o intervalo inclui a inicialização dos
metadados (apagamento e escrita de uma página) antes das páginas da aplicação.
Os instantes são do host e incluem a latência do adaptador USB/SLCAN; não são
medições no próprio microcontrolador.

Imagens (SHA-256 em `manifest.sha256` após finalização):

- `stm32f103c8-application.bin` (7776 bytes) e `stm32g0b1re-application.bin`
  (14204 bytes): as mesmas das sessões de 29/09;
- `stm32f103c8-interrupcao-44032.bin`: a mesma imagem preenchida com `0xFF`
  usada nos ensaios de interrupção, ocupando as 43 páginas da região;
- `stm32g0b1re-regiao-489472.bin`: a imagem do G0 preenchida com `0xFF` até
  489472 bytes (239 páginas de 2 KiB), gerada nesta sessão.

## Resultados

| Alvo | Páginas da aplicação | Execuções | PROCEED → conclusão (ms) | Por página, incluindo metadados (ms) |
|---|---:|---:|---|---:|
| STM32F103C8 | 8 | 11 (0x80: 5; 0x82: 6, incluindo `F103B-restaura`) | 201,0–202,1 (média 201,4) | 22,4 |
| STM32F103C8 | 43 | 3 (0x82) | 980,3–980,5 (média 980,4) | 22,3 |
| STM32G0B1RE | 7 | 6 (0x81, incluindo `G0B1-restaura`) | 177,8–178,3 (média 178,0) | 22,3 |
| STM32G0B1RE | 239 | 1 (0x81) | 5307,4 | 22,1 |

Valores individuais em `tempos-apagamento.json`. O registro
`F103-parcial-01` corresponde à placa 0x80 (nomeado antes da convenção
`F103A`/`F103B`).

## Observações

- O apagamento das 239 páginas do G0 levou 5,3 s, acima do tempo de espera
  padrão de 4 s da ferramenta; a execução usou `--erase-timeout 15`.
- Na execução `G0B1-regiao-01`, a transferência chegou a 100%, o FINISH foi
  aceito (a verificação do CRC sobre 478 KiB atrasou a resposta e a ferramenta
  repetiu o pedido uma vez) e o pedido de execução foi aceito. A aplicação
  iniciou e reivindicou primeiro o endereço 0x80, como já ocorria no início da
  sessão, perdendo-o para a Blue Pill de NAME menor e ficando em 0x81. A
  ferramenta, ainda aguardando a confirmação final, detectou o NAME travado em
  outro endereço e terminou com código 2. O tempo de apagamento não é afetado.
- Ao final, a NUCLEO e a Blue Pill 0x82 foram regravadas com as imagens normais
  (`G0B1-restaura`, `F103B-restaura`) e a descoberta confirmou as três placas em
  modo aplicação nos endereços 0x80, 0x81 e 0x82 (`estado-final-ls.txt`).

## Vazão útil

Os mesmos registros permitem medir a vazão útil definida no capítulo de método
(tamanho da imagem dividido pelo intervalo entre o início da atualização e a
resposta bem-sucedida da verificação final). `Validation/throughput_timing.py`
mede o intervalo entre o comando `START_UPDATE` enviado e o ACK de `VERIFY`
recebido; o intervalo inclui preparação da sessão, apagamento, transferência,
`FINALIZE` e `VERIFY`. Valores individuais em `vazao-util.json`.

| Alvo | Imagem (bytes) | Execuções | Intervalo (s) | Vazão útil (bytes/s) |
| --- | ---: | ---: | --- | --- |
| STM32F103C8 | 7776 | 11 | 1,209–1,280 | 6076–6434 |
| STM32F103C8 | 44032 | 3 | 5,930–5,983 | 7360–7426 |
| STM32G0B1RE | 14204 | 6 | 1,848–1,956 | 7260–7684 |
| STM32G0B1RE | 489472 | 1 | 62,577 | 7822 |
