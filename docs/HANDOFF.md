# Passagem de trabalho entre agentes

## Correções do Marco 02

A branch `codex/marco-02-correcoes` parte de `b0e4cc7` e integra primeiro em `claude/marco-02-graficos` após aprovação. Faça edições e builds na worktree própria. Os pacotes do Marco 01 e 02 continuam nas pastas originais em `D:\ECOS_DE_AETHRA`.

- AFS está desabilitado no descritor e no INI compartilhado. Uma primeira tentativa só pelo INI fez a Unreal gerar outra credencial. Se uma futura distribuição Android exigir AFS, habilite-o no checkout privado de distribuição e mantenha as credenciais no override local ignorado `Config/UserEngine.ini`. As credenciais históricas continuam expostas. A retirada ou substituição de eventuais builds Android de desenvolvimento e uma limpeza coordenada do histórico dependem de aprovação.
- `Tools/Bench/bench.ps1` cria uma pasta exclusiva em `Saved/Bench` na própria worktree. Aqueça o pacote com uma execução curta e rode a medição em outra captura. O modo padrão mede 60 segundos simulados. Use `-RealTime` para a captura em tempo real e confira `duration` no resumo.
- Para repetir a validação das junções, execute `Automation RunTests Aethra.Voxel`. O caso `ChunkSeams` verifica faces e cores de vértices nas bordas, em cantos positivos e negativos, e após descarregar e recarregar a região.
- Empacote em saída vazia dentro da worktree, com cook completo. Se uma execução de cook não encerrar, preserve o log e o PID, registre se houve saída válida e mantenha o teste de cook limpo pendente. O reaproveitamento via `-skipcook` é um contorno histórico e exige verificar a origem dos assets.
- Consulte [TEST_REPORT.md](TEST_REPORT.md) para o resultado dos testes, as medições e a validação do pacote.

Este arquivo registra o estado para que GPT/Codex e Claude Code alternem no mesmo projeto sem perder progresso. Atualize-o ao encerrar cada sessão.

## Estado em 19/11/2026 (fim do Marco 02, Claude Code)

| Item | Local |
| --- | --- |
| Projeto e repositório Git | `D:\ECOS_DE_AETHRA` |
| Código original do GPT/Codex (cópia de origem, não editar) | `%USERPROFILE%\Documents\Codex\2026-11-19\new-chat-4\work\EcosDeAethra` |
| Build empacotada do Marco 01 (GPT/Codex) | `D:\ECOS_DE_AETHRA\Windows\EcosDeAethra.exe` |
| Build empacotada do Marco 02 | `D:\ECOS_DE_AETHRA\Builds\Marco02\Windows\EcosDeAethra.exe` |
| GitHub | `neurai-admin/Ecos-de-aethra`: `main` e `claude/marco-02-graficos`, PR para revisão |

| Branch | Conteúdo |
| --- | --- |
| `main` | Commit-base `3d154aa`: cópia exata do código do GPT/Codex. |
| `claude/marco-02-graficos` | Marco 02. Merge em `main` só com aprovação do diretor. |

Uma nova sessão deve criar a própria branch a partir da última branch aprovada, por exemplo `codex/marco-03-...`.

## Mapa rápido do código

| Arquivo | Papel |
| --- | --- |
| `Private/Voxel/AethraTerrain.*` | Geração pura por seed: relevo, montanhas, vegetação, água; regras de bloco (`IsOpaque`, `IsLeaves`, `IsSurface`...) |
| `Public/AethraVoxelWorld.h`, `Private/Voxel/AethraVoxelWorld.cpp` | Streaming, edição, save, contrato do LIS, cvar `aethra.Horizon` |
| `Private/Voxel/AethraVoxelChunk.*` | Malha por chunk em 4 seções (opaco, plantas, folhas, água), AO por vértice |
| `Private/Voxel/AethraHorizon.*` | Horizonte LOD no thread pool |
| `Private/Voxel/AethraBlockColor.h` | Cor por bloco; alfa = id do bloco para os materiais |
| `Private/AethraGameMode.cpp` | Sol, céu, névoa, nuvens, pós-processo |
| `Private/AethraCharacter.cpp` | Controles e rota de benchmark `-AethraBench` |
| `Tools/Editor/create_voxel_material.py` | Gera os 4 materiais (`M_AethraVoxel`, `M_AethraLeaves`, `M_AethraPlant`, `M_AethraWater`) |
| `Tools/Bench/` | `bench.ps1`, `csvstats.py`, `spikes.py` |

## Rotina de verificação

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' EcosDeAethraEditor Win64 Development '-Project=D:\ECOS_DE_AETHRA\EcosDeAethra.uproject' -WaitMutex
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\ECOS_DE_AETHRA\EcosDeAethra.uproject' -Unattended -NullRHI "-ExecCmds=Automation RunTests Aethra.Voxel" "-TestExit=Automation Test Queue Empty" -stdout
powershell -ExecutionPolicy Bypass -File D:\ECOS_DE_AETHRA\Tools\Bench\bench.ps1 -Name verificacao -Seconds 60
```

Para medir a build empacotada, passe `-Exe D:\ECOS_DE_AETHRA\Builds\Marco02\Windows\EcosDeAethra.exe`. Após mudar materiais, rode `Tools/Editor/create_voxel_material.py` pelo editor (ver cabeçalho do script) e faça um benchmark curto de aquecimento antes de medir, porque a primeira execução compila shaders.

O empacotamento original em `D:\ECOS_DE_AETHRA\Builds\Marco02` usou `-skipcook` após o cook chegar a `LogCook: Display: Done!` e travar no encerramento. Preserve esse pacote e use uma pasta nova para as validações.

## Regras combinadas

- Novos valores de `EAethraBlock` vão sempre ao fim da enum.
- Mudanças na geração só podem ocupar células de ar ou ficar fora do raio de 256 células, a menos que o formato do save seja versionado.
- O contrato do LIS (`GetBlock`, `SetBlock`, `OnBlockChanged`, `Seed`) não mudou no Marco 02.
- Materiais são gerados por script; não edite os `.uasset` à mão sem atualizar o script. `ComponentMask` novo nasce com R e G ligados: use o helper `mask()`.
- Commits em branch própria, mensagem descritiva, sem merge sem aprovação, sem force push.

## Próximas tarefas sugeridas

Consulte as pendências do Marco 02 em [TEST_REPORT.md](TEST_REPORT.md). A mais importante para o p99 é gerar a malha dos chunks no thread pool.
