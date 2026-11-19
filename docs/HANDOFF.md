# Passagem de trabalho entre agentes

Este arquivo registra o estado para que GPT/Codex e Claude Code alternem no mesmo projeto sem perder progresso. Atualize-o ao encerrar cada sessão.

## Onde está o projeto

| Item | Local |
| --- | --- |
| Projeto e repositório Git | `D:\ECOS_DE_AETHRA` |
| Código original do GPT/Codex (cópia de origem, não editar) | `%USERPROFILE%Documents\Codex\2026-11-19\new-chat-4\work\EcosDeAethra` |
| Build empacotada do GPT/Codex (Marco 01) | `D:\ECOS_DE_AETHRA\Windows\EcosDeAethra.exe` |
| Build empacotada do Marco 02 | `D:\ECOS_DE_AETHRA\Builds\Marco02\Windows\EcosDeAethra.exe` |
| GitHub oficial | `neurai-admin/Ecos-de-aethra`, ainda vazio; nada foi enviado |

## Branches

| Branch | Conteúdo |
| --- | --- |
| `main` | Commit-base `3d154aa`: cópia exata do código do GPT/Codex, sem alteração. |
| `claude/marco-02-graficos` | Trabalho do Marco 02 do Claude Code. Merge em `main` só com aprovação do responsável. |

Uma nova sessão deve criar a própria branch a partir da última branch aprovada, por exemplo `codex/marco-03-...`.

## Rotina de verificação

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' EcosDeAethraEditor Win64 Development '-Project=D:\ECOS_DE_AETHRA\EcosDeAethra.uproject' -WaitMutex
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\ECOS_DE_AETHRA\EcosDeAethra.uproject' -Unattended -NullRHI "-ExecCmds=Automation RunTests Aethra.Voxel" "-TestExit=Automation Test Queue Empty" -stdout
powershell -ExecutionPolicy Bypass -File D:\ECOS_DE_AETHRA\Tools\Bench\bench.ps1 -Name verificacao -Seconds 60
```

Após mudar materiais, rode `Tools/Editor/create_voxel_material.py` pelo editor (ver cabeçalho do script) e faça um benchmark curto de aquecimento antes de medir, porque a primeira execução compila shaders.

## Regras combinadas

- Novos valores de `EAethraBlock` vão sempre ao fim da enum.
- Mudanças na geração só podem ocupar células de ar, a menos que o formato do save seja versionado.
- O contrato do LIS (`GetBlock`, `SetBlock`, `OnBlockChanged`, `Seed`) não mudou no Marco 02.
- Materiais são gerados por script; não edite os `.uasset` à mão sem atualizar o script.
- Commits em branch própria, mensagem descritiva, sem merge sem aprovação.

## Pendências abertas

Consulte a seção do Marco 02 em [TEST_REPORT.md](TEST_REPORT.md).
