# Validação integrada A–H

> **Hardware validation was not performed.** Este documento é o procedimento a
> executar; não é um registro de aprovação. Preencha a tabela de resultados
> durante a bancada e não marque o hardware como validado apenas com base nos
> builds ou testes sem hardware.

Todos os testes abaixo formam **uma única sessão**, na ordem A–H. Essa ordem
estabelece um estado inicial conhecido, valida a atualização normal, provoca uma
interrupção depois do erase e termina recuperando o mesmo target.

## Material e premissas

- STM32F103C8Tx com HSE de 8 MHz e LED ativo-baixo em PC13;
- ST-LINK e STM32CubeProgrammer CLI;
- transceptor CAN entre PA11/PA12 e o barramento diferencial;
- interface USB–CAN exposta pelo Linux como `can0`;
- CAN_H, CAN_L e GND comum, com 120 Ω em cada extremidade física;
- GNU Arm Embedded Toolchain, GNU Make, Python 3.10+ e `can-utils`;
- repositório aberto em sua raiz.

Com toda a rede desligada, uma medição aproximada de 60 Ω entre CAN_H e CAN_L
é compatível com duas terminações de 120 Ω em paralelo. Essa medição não valida
polaridade, alimentação ou compatibilidade elétrica do transceptor.

Os comandos pressupõem node ID 1 e SocketCAN `can0`. Se o sistema atribuiu outro
nome à interface, substitua somente `can0` de forma consistente.

## Preparação de software

Crie um ambiente isolado e instale a CLI e suas dependências de teste:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e 'Tools/canloader[test]'
```

Execute um quality gate limpo e guarde a saída no registro da sessão:

```sh
make clean
make bootloader
make test-application
make test
```

Os arquivos usados na bancada devem existir em:

```text
build/bootloader/CanLoader.bin
TestApplication/build/canloader_test_app.bin
```

Não prossiga para o hardware se algum comando acima falhar. Mesmo quando eles
passam no ambiente de desenvolvimento, isso não constitui validação física.

## Configuração do SocketCAN

Com o adaptador conectado e reconhecido como `can0`:

```sh
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 up
ip -details -statistics link show can0
```

Confirme na saída `bitrate 500000` e estado `UP`. Em outro terminal, é opcional
manter um monitor durante toda a sessão:

```sh
candump can0
```

A ferramenta CanLoader não executa esses comandos nem eleva privilégios.

## Estado inicial por ST-LINK

O próximo erase é intencional e destrói bootloader, aplicação e metadata atuais
do target. Confirme que o ST-LINK está ligado ao STM32 correto e, somente para
esta sessão controlada, execute:

```sh
STM32_Programmer_CLI -c port=SWD -e all
STM32_Programmer_CLI -c port=SWD -w build/bootloader/CanLoader.bin 0x08000000 -v -rst
```

A gravação deve verificar o `.bin` em `0x08000000` e resetar o target. Não grave
a aplicação de teste por SWD nesta sessão: ela deve chegar pela CAN.

## TESTE A — bootloader

Com Flash inicialmente apagada, a metadata não é válida. Depois do reset:

- o dispositivo deve permanecer no bootloader, sem reset em loop;
- PC13 deve alternar a cada 500 ms;
- a aplicação de duas piscadas ainda não deve aparecer.

Falha esperada a investigar: LED apagado/aceso sem alternância ou padrão da
aplicação antes de qualquer update.

## TESTE B — CAN e INFO

Execute:

```sh
python -m canloader info --interface socketcan --channel can0 --node 1 --verbose
```

Com o erase total do estado inicial, espere:

```text
Protocol version: 1
Node: 1
Capabilities: 0xF1010100
Application base: 0x08004000
Application maximum size: 48128 bytes
Stored image size: 0 bytes
Stored image CRC: 0x00000000
```

O valor de capabilities esperado representa target `0xF1`, protocolo 1, nó 1,
sem update ativo e sem aplicação válida. Confirme também no `candump` requisições
em `0x601` e respostas em `0x581`.

## TESTE C — update completo

Envie a aplicação sem `--run`, para separar update, verificação e jump:

```sh
python -m canloader update TestApplication/build/canloader_test_app.bin --interface socketcan --channel can0 --node 1 --verbose
```

Durante erase/recepção/verificação, PC13 deve alternar a cada 125 ms. A CLI
deve progredir até o tamanho total e terminar com uma linha semelhante a:

```text
Verified <tamanho> bytes, CRC 0x<crc>.
```

Depois disso o bootloader fica em `READY`, com LED apagado, e não executa a
aplicação porque `--run` não foi usado.

## TESTE D — CRC

Calcule o CRC do arquivo enviado no host:

```sh
python -c "from pathlib import Path; import zlib; data=Path('TestApplication/build/canloader_test_app.bin').read_bytes(); print(f'0x{zlib.crc32(data) & 0xFFFFFFFF:08X}')"
```

Peça uma nova verificação ao bootloader:

```sh
python -m canloader verify --interface socketcan --channel can0 --node 1
```

O CRC impresso por `verify` deve coincidir exatamente com o valor do Python e
com o valor informado no fim do TESTE C.

## TESTE E — jump

Execute:

```sh
python -m canloader run --interface socketcan --channel can0 --node 1 --verbose
```

A CLI deve receber o ACK antes do handoff. Em seguida, a aplicação deve repetir:

1. LED aceso por 80 ms;
2. apagado por 100 ms;
3. aceso por 80 ms;
4. apagado por 740 ms.

LED continuamente aceso indica que a aplicação encontrou `PRIMASK != 0` no
handoff, e o teste deve ser marcado como falha.

## TESTE F — reboot com imagem válida

Pare de enviar comandos CAN, pressione RESET fisicamente e aguarde mais de
1500 ms. O bootloader deve manter PC13 aceso durante a boot window e depois
executar automaticamente a aplicação. O padrão de duas piscadas deve reaparecer
sem comando `run`.

## TESTE G — atualização interrompida

Crie uma imagem válida preenchida até o limite da partição. Os primeiros vetores
e o código continuam sendo os da aplicação; o preenchimento apenas torna a janela
de interrupção observável:

```sh
python -c "from pathlib import Path; src=Path('TestApplication/build/canloader_test_app.bin').read_bytes(); Path('/tmp/canloader-interrupt.bin').write_bytes(src.ljust(0xBC00, b'\xff'))"
```

Com a aplicação ainda executando, inicie o update com tentativas suficientes
para atravessar o próximo reset:

```sh
python -m canloader update /tmp/canloader-interrupt.bin --interface socketcan --channel can0 --node 1 --timeout 0.5 --retries 20 --verbose
```

Logo depois de iniciar o comando, pressione RESET para abrir a boot window. As
tentativas de DISCOVER devem reter o bootloader e iniciar a transferência. Espere
a CLI mostrar pelo menos 100 bytes, mas menos que 48128, e então interrompa o
target durante a transmissão: pressione RESET novamente ou remova e restaure sua
alimentação. Não use apenas `Ctrl+C`, porque o cliente tenta enviar `ABORT` de
forma cooperativa.

O update deve terminar com erro depois da reinicialização. No target:

- a aplicação parcial não pode executar;
- PC13 deve voltar ao padrão lento de `WAIT_HOST`;
- o dispositivo deve continuar respondendo a INFO.

Confirme:

```sh
python -m canloader info --interface socketcan --channel can0 --node 1
python -m canloader run --interface socketcan --channel can0 --node 1
```

INFO deve indicar capabilities sem o bit de aplicação válida. O segundo comando
deve sair com erro e NACK `INVALID_APPLICATION`; qualquer jump é falha crítica.

## TESTE H — recuperação

Sem apagar nem regravar o bootloader por SWD, reenvie a imagem original completa
e solicite o jump:

```sh
python -m canloader update TestApplication/build/canloader_test_app.bin --interface socketcan --channel can0 --node 1 --run --verbose
```

O update, FINISH e VERIFY devem concluir, o ACK de RUN deve ser recebido e o
padrão de duas piscadas deve reaparecer. Pressione RESET uma última vez, aguarde
1500 ms sem tráfego de controle e confirme que a aplicação volta a iniciar.

Ao encerrar a sessão:

```sh
sudo ip link set can0 down
```

## Registro de resultado

| Teste | Critério | Resultado/observação |
|---|---|---|
| A | bootloader permanece aguardando; LED lento | não executado |
| B | INFO com IDs, versão e mapa corretos | não executado |
| C | imagem transmitida e marcada válida | não executado |
| D | CRC host = FINISH = VERIFY | não executado |
| E | ACK de RUN e padrão de duas piscadas | não executado |
| F | reboot inicia imagem após 1500 ms | não executado |
| G | interrupção nunca executa imagem parcial | não executado |
| H | novo update recupera o dispositivo | não executado |

Somente substitua `não executado` por `aprovado` ou `reprovado` depois de
realizar e registrar a observação correspondente no hardware real.
