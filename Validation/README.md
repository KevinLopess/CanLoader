# Evidências de validação do CanLoader

Este diretório organiza as evidências usadas no capítulo de testes e resultados.
Ele complementa o procedimento técnico de `Docs/VALIDATION.md`; não substitui
os critérios descritos naquele documento.

Uma sessão somente pode ser classificada como aprovada quando os arquivos
indicados na coluna de evidência existirem e puderem ser relacionados ao commit,
ao binário e ao alvo ensaiado. A ausência de erro no terminal, isoladamente, não
é evidência suficiente.

## Estrutura

- `run_software_baseline.sh`: compila os dois firmwares, executa as suítes C e
  Python e arquiva ambiente, logs, tamanhos e hashes;
- `run_logged_command.py`: executa um comando explícito e registra saída,
  instante, duração e código de retorno;
- `finalize_session.py`: gera o manifesto SHA-256 de uma sessão depois que
  todos os registros forem incluídos;
- `analyze_verbose_log.py`: conta quadros, CAN-IDs e PGNs de um log `--verbose`,
  sem atribuir timestamps que o log não contém;
- `session-template.md`: ficha a ser copiada para cada campanha física;
- `evidence/`: sessões efetivamente executadas. Cada pasta deve ser imutável
  depois de citada na monografia; correções geram uma nova sessão.

Os scripts deste diretório não abrem uma interface CAN nem programam o STM32
automaticamente. O primeiro trabalha apenas com build e testes locais. O segundo
executa exatamente o comando informado depois de `--`, que deve ser conferido
pelo operador.

## Baseline de software

A partir da raiz do repositório:

```sh
./Validation/run_software_baseline.sh \
  Validation/evidence/AAAA-MM-DD-software-baseline
```

O diretório de destino não pode existir. Por padrão, o script usa
`.venv/bin/python`; outro interpretador pode ser selecionado com a variável
`PYTHON`. O baseline contém:

- commit e estado dos diretórios de código;
- versões do sistema, Python, dependências e toolchain;
- saída integral do build;
- saída integral das seis suítes C e da suíte Python;
- cópias dos binários, com tamanho, SHA-256 e CRC-32;
- mapas de ligação preservados como arquivos `.map.txt`;
- seções dos arquivos ELF.

Esse baseline verifica artefatos e lógica de software. Ele não deve ser
apresentado como validação do barramento, da Flash física ou do handoff.

## Sessão física

Crie uma nova pasta e copie a ficha antes de energizar a bancada:

```sh
mkdir -p Validation/evidence/AAAA-MM-DD-hardware
cp Validation/session-template.md \
  Validation/evidence/AAAA-MM-DD-hardware/session.md
```

Preencha os materiais, identidades e hashes antes do primeiro comando mutável.
Use um nome curto e estável para cada registro. Exemplo de descoberta, que é uma
operação de comunicação sem gravação de firmware:

```sh
.venv/bin/python Validation/run_logged_command.py \
  --session Validation/evidence/AAAA-MM-DD-hardware \
  --test A-ls -- \
  .venv/bin/python -m canloader ls \
  --interface slcan --channel /dev/cu.usbmodemXXXX \
  --bitrate 500000 --verbose
```

O registrador cria um arquivo de texto em `commands/` e um arquivo JSON com o
mesmo prefixo. O JSON contém o comando como uma lista de argumentos, evitando
ambiguidades de escape. O código de retorno é preservado. No esquema 1, o campo
`spawn_error` é opcional para manter a leitura dos registros criados antes de
sua inclusão; quando presente, informa uma falha ocorrida antes de o processo
ser iniciado.

Ao encerrar a sessão, gere ou atualize o manifesto:

```sh
.venv/bin/python Validation/finalize_session.py \
  --session Validation/evidence/AAAA-MM-DD-hardware
```

O arquivo `manifest.sha256` cobre todos os arquivos regulares da sessão, exceto
ele próprio. Qualquer alteração posterior exige uma nova finalização e deve ser
registrada na ficha.

Um log host pode ser resumido sem apagar o original:

```sh
.venv/bin/python Validation/analyze_verbose_log.py \
  --log Validation/evidence/SESSAO/commands/ARQUIVO.log \
  --output Validation/evidence/SESSAO/analysis/ARQUIVO.json
```

A análise conta somente os quadros vistos pela CLI. Ela não mede ocupação do
barramento nem o intervalo `START_UPDATE`--`VERIFY`.

Os comandos `update`, `run`, `reboot` e `set-address` mudam o estado do alvo.
Apagamento e gravação por SWD também são operações destrutivas. Antes de
executá-los, confirme o número de série do ST-LINK, o NAME, o endereço CAN e o
hash do binário. Nunca reutilize evidência de outra placa ou revisão.

## Estados permitidos

- `PENDENTE`: ainda não executado;
- `PARCIAL`: parte do procedimento foi executada e possui evidência, mas o
  conjunto de critérios ainda não foi completado;
- `APROVADO`: critério atendido e evidência arquivada;
- `REPROVADO`: execução concluída com critério não atendido;
- `BLOQUEADO`: não executável por ausência de material ou condição externa,
  com o motivo registrado.

Resultados parciais devem permanecer associados ao teste correspondente. Um
teste parcial, bloqueado ou reprovado não pode ser convertido em aprovado
apenas porque uma suíte automatizada relacionada foi aprovada.
