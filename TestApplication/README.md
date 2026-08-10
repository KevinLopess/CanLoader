# Aplicacao de teste do CanLoader

Aplicacao minima para o STM32F103C8, sem HAL e sem alocacao dinamica. Ela e
linkada a partir de `0x08004000`; sua regiao de Flash tem `0xBC00` bytes e
termina em `0x0800FBFF`, imediatamente antes dos metadados em `0x0800FC00`.
A RAM disponivel e de 20 KiB.

O `SystemInit()` posiciona o VTOR em `0x08004000` e seleciona o HSI de 8 MHz.
Depois da inicializacao, uma interrupcao de SysTick a cada 1 ms alimenta uma
maquina de estados nao bloqueante. O LED ativo-baixo em PC13 repete duas
piscadas de 80 ms, separadas por 100 ms, seguidas por uma pausa de 740 ms.

A aplicacao nao corrige IRQs mascaradas pelo bootloader. Se `PRIMASK` chegar
diferente de zero, o LED permanece continuamente aceso como indicacao de erro;
portanto, o padrao de duas piscadas tambem valida o handoff das interrupcoes.

## Build

Com o toolchain `arm-none-eabi` no `PATH`:

```sh
cd TestApplication
make
```

O build reutiliza por referencia o startup e os headers CMSIS existentes no
projeto. Os artefatos `ELF`, `MAP`, `BIN` e `HEX` sao gravados em `build/`.
