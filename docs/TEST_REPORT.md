# Relatório de testes

## Ambiente observado

| Item | Resultado |
| --- | --- |
| Unreal Engine | 5.8.4 encontrada em `D:\Epic Games\UE_5.8` |
| Git e Git LFS | Git 2.56.0 para Windows e Git LFS 3.8.1 |
| MSVC e SDK Windows | MSVC 14.52.37029 em `D:\VisualStudio\Community` e SDK 10.0.28000.0 selecionado pela UnrealBuildTool. SDK 10.0.26100.0 também disponível. |
| Repositório oficial | Público e vazio. A conta NeurAI tem escrita. A branch padrão configurada é `main`, ainda sem commit. |
| Clone por HTTPS | O Git local reportou falha de DNS para `github.com`. |

## Validação

O alvo `EcosDeAethraEditor Win64 Development` compilou na Unreal 5.8.4 após atualizar os alvos para `BuildSettingsVersion.V7` e a ordem de includes para `Unreal5_8`. O editor abriu o projeto e executou a cena em primeira pessoa.

O empacotamento Win64 Development compilou o alvo de jogo após trocar o acesso à luz direcional por uma API disponível em runtime. O Unreal Automation Tool concluiu com código 0 e gerou `D:\ECOS_DE_AETHRA\Windows\EcosDeAethra.exe`. A abertura do pacote interativo ainda precisa ser confirmada.

A verificação estática confirmou o descritor, o mapa, o material e o plugin nas dependências instaladas, os cabeçalhos de origem, as associações de teclado e mouse e a orientação das seis faces de cada bloco.

Os testes `Aethra.Voxel.Coordinates`, `Aethra.Voxel.Generation` e `Aethra.Voxel.SaveRoundTrip` passaram. O teste manual em Play removeu e recolocou um bloco, com atualização da geometria e gravação dos dois slots locais. Selecione os testes `Aethra.Voxel` no painel Session Frontend da Unreal ou use o editor em linha de comando após a compilação.

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\ECOS_DE_AETHRA\EcosDeAethra.uproject' -Unattended -NullRHI "-ExecCmds=Automation RunTests Aethra.Voxel" "-TestExit=Automation Test Queue Empty" -stdout -FullStdOutLogOutput
```

## Casos manuais pendentes

1. Andar e pular para conferir a colisão em diferentes inclinações.
2. Remover e recolocar um bloco em uma divisa de chunk e conferir ambos os lados.
3. Colocar areia em uma face lateral e conferir que a colocação no volume do personagem é bloqueada.
4. Afastar-se até descarregar um chunk alterado, voltar ao local e verificar a edição.
5. Fechar e reabrir a sessão com a mesma seed. Verificar a restauração das alterações e repetir com outra seed.
6. Testar F1 a F3 e registrar se o raio de carregamento e as opções gráficas mudam.

## Benchmark pendente

Executar em Standalone a 1920 × 1080 no PC-alvo, com o preset F2, a mesma seed e uma rota repetível. Aquecer a cena por 20 s e gravar 60 s de percurso e edições com `csvprofile start` e `csvprofile stop`. Registrar FPS médio, frame time p95/p99, tempos de Game e GPU pelo `stat unit`, RAM, preset e resolução. A meta inicial de 60 FPS estáveis segue sem medição.

## Marco 02 — Claude Code (19/11/2026)

Branch `claude/marco-02-graficos`. Hardware: Ryzen 5 5500, RTX 3060 12 GB (driver 621.24), 16 GB, Windows 11, UE 5.8.4.

### Build empacotada

`Builds\Marco02\Windows\EcosDeAethra.exe` (Win64 Development). A build do Marco 01 em `Windows\` foi preservada. No UE 5.8.4 o processo de cook terminou com `LogCook: Display: Done!` e travou ao encerrar; o processo foi finalizado e o stage/pak/archive foi executado com `-skipcook` (UAT código 0).

### Testes

| Teste | Resultado |
| --- | --- |
| `Aethra.Voxel` (5 testes: Coordinates, Generation, Horizon, PersistAcrossRestart, SaveRoundTrip) | Aprovados no editor |
| Persistência na build empacotada | Sessão 1 fez 30 edições pela rota; após fechar, a sessão 2 registrou `Loaded 30 voxel edits (revision 30) for seed 42819` e continuou gravando (save 9,2 → 12,5 KB) |
| Saves do Marco 01 | Carregam sem aviso de save inválido e não são regravados ao abrir |
| Movimento, salto, remoção de blocos, streaming e horizonte | Exercitados pela rota `-AethraBench` (passo fixo de 60 Hz) |
| Colocação de bloco pelo mouse | Não exercitada na build; `SetBlock` de colocação coberto por `PersistAcrossRestart` |

### Benchmark

`Tools\Bench\bench.ps1`: 1920 × 1080 janela, VSync desligado, `-benchmark -fps=60`, rota fixa de 60 s após 5 s de aquecimento, uma remoção de bloco a cada 2 s. pXX = percentil XX do tempo de frame; 1% low = FPS médio do 1% de frames mais lentos.

| Medição | FPS médio | p95 | p99 | 1% low | GPU médio | RAM |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Base GPT (editor `-game`) | 283 | 4,4 ms | 7,2 ms | — | 2,2 ms | 2,3 GB* |
| Marco 02 inicial (build empacotada) | 127 | 9,5 ms | 20,0 ms | — | 6,8 ms | 1,5 GB |
| Marco 02 final (build empacotada) | 129 | 8,6 ms | 14,8 ms | 59 | 6,9 ms | 1,5 GB |

\* O editor em `-game` inclui a memória do editor. VRAM local ~0,67 GB nas três medições.

### Gargalo investigado

Colunas próprias `Aethra/*` no csvprofile e `Tools\Bench\spikes.py` (compara o 1% de frames mais lentos com os frames típicos) mostraram que os picos vinham do cozimento síncrono de colisão: ~7 ms por chunk carregado e até ~60 ms por edição. O envio de malha do horizonte custava ~1,3 ms em média e não era a causa. Com colisão assíncrona, `ChunkUpload` caiu para 1,3 ms em média (máximo 5,8 ms). O pico restante (~10–14 ms na game thread) é a geração e a malha síncronas do chunk.

### Pendências

1. Gerar e montar a malha de chunks no thread pool, como já é feito no horizonte.
2. Água sem fluxo: remover um bloco na margem não reabastece; sem reflexo de tela (SSR) em translúcidos.
3. Horizonte ignora edições do jogador e não tem transição suave (popping) entre passos de LOD.
4. Cook do UE 5.8.4 trava ao sair após `Done!`; investigar ou usar o watchdog descrito no HANDOFF.
5. Teste manual de colocação de bloco com o mouse na build empacotada.
