# Gravação e depuração pelo STM32CubeIDE

O repositório contém duas configurações de depuração:

- `CanLoader STM32F103C8Tx.launch`;
- `CanLoader NUCLEO-G0B1RE.launch`.

As configurações usam ST-LINK e SWD, verificam o download, conectam sob reset e
param no `main`. Cada uma grava somente as seções do ELF do bootloader. A
aplicação de referência continua sendo transferida pelo CAN.

## 1. Importar o projeto

No STM32CubeIDE:

1. selecione **File > Import**;
2. escolha **General > Existing Projects into Workspace**;
3. selecione a raiz do repositório;
4. mantenha **Copy projects into workspace** desmarcado;
5. conclua a importação e use **Project > Refresh** se as configurações não
   aparecerem imediatamente.

## 2. Gerar os ELF

As configurações `.launch` não usam o builder gerenciado antigo do CubeIDE.
Antes de depurar, abra **Terminal > New Terminal** dentro do IDE e execute:

```sh
make TARGET=stm32f103c8 bootloader
make TARGET=stm32g0b1re bootloader
```

Os arquivos esperados são:

```text
build/stm32f103c8/bootloader/CanLoader.elf
build/stm32g0b1re/bootloader/CanLoader.elf
```

O comando **Project > Build Project** usa a configuração gerenciada do projeto
original e compila somente o STM32F103C8Tx. Para o STM32G0B1RE, use o segundo
comando `make` acima. Os fontes e drivers do G0 estão excluídos das configurações
gerenciadas `Debug` e `Release` do F103 para impedir a mistura das duas
plataformas.

## 3. Selecionar o ST-LINK

Abra **Run > Debug Configurations > STM32 C/C++ Application** e selecione a
configuração da placa. Na aba **Debugger**:

1. mantenha **ST-LINK (ST-LINK GDB server)**;
2. escolha **SWD**;
3. mantenha **Connect under reset**;
4. se houver mais de um probe conectado, habilite a seleção por número de série
   e escolha o ST-LINK da placa correta;
5. confirme a interface com **Scan** quando a versão do CubeIDE oferecer esse
   botão.

Na NUCLEO-G0B1RE, use o ST-LINK integrado à placa. No STM32F103C8Tx, conecte o
probe externo a SWDIO, SWCLK, GND e 3V3/VTref; NRST é recomendado para a
estratégia **Connect under reset**.

## 4. Gravar e depurar

Selecione a configuração desejada e pressione **Debug**. O CubeIDE deverá:

1. iniciar o ST-LINK GDB server;
2. conectar ao microcontrolador;
3. apagar os setores necessários;
4. gravar e verificar o ELF;
5. interromper a execução em `main`.

Use **Resume** para iniciar o bootloader. Depois da primeira gravação, use a
ferramenta host para transmitir a aplicação pelo CAN.

## 5. Primeira instalação e troca de mapa

O download normal do CubeIDE apaga somente os setores necessários ao ELF. Ele
não garante que uma aplicação ou metadata antiga seja removida. Ao trocar de um
mapa antigo ou preparar um ensaio limpo, faça uma única vez um **Full chip
erase** pelo STM32CubeProgrammer e depois execute novamente a configuração de
depuração correta.

Não use o ELF do F1 na NUCLEO-G0B1RE nem o ELF do G0 no STM32F103. Confira o
caminho mostrado na aba **Main** antes de pressionar **Debug**.

## 6. Erros comuns

- **Program file does not exist**: execute o comando `make ... bootloader` do
  target correspondente.
- **Erros `HAL_FDCAN_*` durante um build do F103**: execute **Project > Clean**,
  atualize o projeto com **Refresh** e compile novamente. Esse erro indica que o
  diretório `Debug` ainda contém arquivos gerados antes da exclusão dos fontes do
  STM32G0.
- **No ST-LINK detected**: confira cabo USB, alimentação, driver e firmware do
  probe.
- **Target not found**: reduza a frequência SWD e mantenha **Connect under
  reset**; no F1, conecte também NRST.
- **Wrong device or core**: encerre a sessão, desconecte a outra placa e confira
  o ELF e o número de série selecionados.
- **A aplicação antiga iniciou**: faça um full chip erase na primeira instalação
  e grave novamente apenas o bootloader.
