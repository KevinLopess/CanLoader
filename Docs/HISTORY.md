# Histórico de desenvolvimento

## Linha do tempo

| Data | Commit | Etapa |
|---|---|---|
| 2026-08-10 | `785c92f` | Projeto STM32CubeIDE para o STM32F103C8 (Blue Pill) com CAN |
| 2026-08-10 | `2ab539e` | Primeiro protótipo (v1): IDs de 11 bits, nó fixo, salto para a aplicação, gravação da Flash, metadados e CRC |
| 2026-08-10 | `47e2b8c` | Ferramenta Python de atualização com retentativas |
| 2026-08-10 | `c4df14e` | Aplicação de teste relocada e testes nativos |
| 2026-08-10 | `c1818b7` | Correções de recuperação e de correlação de respostas |
| 2026-08-10 | `b6c05f4` | Documentação de arquitetura, protocolo, memória e validação |
| 2026-08-23 | `1b135d2` | Configuração persistente e comandos de gestão de rede |
| 2026-08-23 | `6e253e4` | Migração para mecanismos J1939; aplicação passa de `0x08004000` para `0x08005000` |
| 2026-09-07 | `f470c74` | Port do STM32G0B1RE (FDCAN em modo clássico) |
| 2026-09-07 | `ab75693` | Ferramentas de sessão registrada e registros de 2026-08-25 |
| 2026-09-07 | `8c542cb` | Organização por alvo e configurações do CubeIDE |
| 2026-10-12 | `127418e` | Mensagens periódicas da aplicação de teste |
| 2026-10-12 | `dd1e55f` | Comando `activity` na ferramenta |
| 2026-10-12 | `1117f39`..`5ccc225` | Registros dos ensaios de 29 e 30 de setembro |
| 2026-10-12 | `9fe46d5` | Organização do repositório e deste histórico |
| 2026-11-05 | `40ca23f` | Medição do tempo de apagamento e da vazão útil (sessão de 4 de novembro) |
| 2026-11-09 | `bc88391` | `APP_VARIANT` na aplicação de teste; a variante padrão mantém o binário ensaiado |
| 2026-11-09 | `b272acc` | Configurações Debug e Release por alvo no CubeIDE |
| 2026-11-09 | commit seguinte | Documentação atualizada com os resultados de bancada |

As alterações dos commits de 2026-10-12 foram feitas e ensaiadas entre 7 e 30
de setembro; o código commitado é idêntico ao registrado em
`Validation/evidence/2026-09-30-071952-software-para-monografia/`.

## Tag `v1.0.0`

A tag `v1.0.0` marca a versão citada na monografia. Ela foi criada em `b272acc`
e, em 9 de novembro de 2026, movida para o commit de documentação logo depois
dele, que só altera arquivos Markdown. O firmware, a aplicação de teste na
variante padrão e a ferramenta são os mesmos avaliados nos ensaios de bancada.

## Reescrita do histórico (2026-10-12)

O histórico foi reescrito para unificar a autoria no endereço pessoal do
autor, remover arquivos que não pertencem ao projeto (saídas de compilação em
`Debug/`, um ambiente Python incompleto, arquivos `.DS_Store` e um resumo com
caminhos locais) e padronizar as mensagens no formato Conventional Commits.
Datas e conteúdo do código foram preservados. O commit original `9e1eec3` foi
dividido em dois: o port do STM32G0 e as ferramentas e registros de validação.

Os registros de bancada anteriores à reescrita citam os identificadores
antigos. A correspondência é:

| Antigo | Novo |
|---|---|
| `4d6208b` | `785c92f` |
| `f6caab4` | `2ab539e` |
| `6d61b9b` | `47e2b8c` |
| `b729654` | `c4df14e` |
| `6c58a00` | `c1818b7` |
| `34f933d` | `b6c05f4` |
| `1ef7dd6` | `1b135d2` |
| `39492b1` | `6e253e4` |
| `9e1eec3` | `f470c74` + `ab75693` |
| `2ff3de6` | `8c542cb` |
