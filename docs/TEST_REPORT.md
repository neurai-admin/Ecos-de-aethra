# Relatório de testes

## Correções do Marco 02 — 19/11/2026

Worktree independente baseada em `b0e4cc7`, Unreal Engine 5.8.4 (CL 58902052), Ryzen 5 5500, RTX 3060 e 16 GB de RAM. Os resultados históricos dos Marcos 01 e 02 estão nas seções abaixo.

| Verificação | Resultado |
| --- | --- |
| Configuração AFS | O plug-in e a conexão de rede estão desabilitados. A primeira tentativa de desligar apenas no INI regenerou uma credencial ao abrir o editor. Com o plug-in desabilitado no descritor, o token não reapareceu após abrir, compilar e empacotar. O valor anterior está em 14 commits do histórico compartilhado. A validade residual depende de algum pacote Android de desenvolvimento que ainda o utilize. |
| Saída do benchmark | PowerShell 5.1 e 7 passaram em 14 nomes inválidos, rejeição de arquivos no lugar de diretórios e preservação de resultados ao repetir o nome. A criação das duas junções NTFS foi negada pelo sandbox, de modo que esses dois casos foram pulados. |
| Compilação e automação | Alvo de editor compilado. Seis testes `Aethra.Voxel` passaram, incluindo `ChunkSeams`, que testa faces nas divisas, mudança de oclusão em cantos positivos e negativos e reposição após descarregar e carregar chunks. |
| Build em Windows | Win64 Development arquivada em `Builds/Marco02Correcoes/Windows`. As capturas foram produzidas pelo executável empacotado fora do editor. A rota exercitou movimento, salto e edição com saves de teste próprios. |
| Visual | As capturas equivalentes dos dois pacotes mostram água, vegetação, céu, sombras e horizonte. A correção preserva os materiais e a direção artística. |

### Cook e procedência do pacote

A primeira tentativa terminou antes do cook com MSVC C3859/C1076 e erro 1455 de memória virtual no executor XGE. A execução seguinte usou o executor local, com configuração de memória da worktree, e compilou o alvo de jogo. O UAT iniciou um cook completo com Zen em uma worktree sem cook prévio. Processou 507 pacotes, pulou zero incrementalmente e 7 por plataforma. O `UnrealEditor-Cmd.exe` encerrou com código 0, cerca de três segundos após `LogCook: Display: Done!`. Stage e archive terminaram, e o UAT saiu com código 0. A falha de encerramento original não se repetiu nessa execução e sua causa permanece indeterminada.

Os quatro materiais versionados coincidiram com os hashes dos ponteiros LFS. Os quatro constam no Asset Registry do cook e no manifesto UFS do pacote. SHA-256 do executável interno `EcosDeAethra/Binaries/Win64/EcosDeAethra.exe` `aad5746c616834fbeb3f49ea5466551e9f049d6eede0bf9b37889d37991ddd6a`.

### Benchmark empacotado a 1920 × 1080

Uma sessão curta de 10 segundos simulados aqueceu cada pacote. A medição de cada pacote usou a rota de passo fixo a 60 Hz, com 5 segundos de aquecimento interno, VSync desligado, limite de FPS desligado e saves isolados. Cada CSV contém 3.601 frames para os 60 segundos de simulação. A resolução também foi conferida nas imagens produzidas pelo jogo.

| Métrica | Marco 02 original | Marco 02 corrigido |
| --- | ---: | ---: |
| FPS médio | 112,0 | 112,2 |
| 1% low | 54,2 FPS | 54,3 FPS |
| Frame p95 | 10,07 ms | 10,11 ms |
| Frame p99 | 15,83 ms | 15,17 ms |
| Pior frame | 33,31 ms | 37,57 ms |
| Game Thread média | 2,42 ms | 2,46 ms |
| Render Thread média | 7,44 ms | 7,43 ms |
| GPU média | 7,93 ms | 7,91 ms |
| RAM média (PhysicalUsedMB) | 1.468,73 MB | 1.496,72 MB |
| VRAM local média | 664,54 MB | 663,84 MB |
| Geração de chunks por frame ativo | — | 0,15 ms |
| Malha e aplicação por frame ativo | 3,09 ms | 3,33 ms |
| Aplicação da malha por frame ativo | 1,34 ms | 1,44 ms |

Na execução corrigida, o 1% de frames mais lentos teve 14,91 ms de Game Thread em média. Nesses frames, geração, construção e aplicação de malhas representaram em média 0,13 ms e 2,49 ms, respectivamente, com a aplicação incluída na medição de malha. A Render Thread esperou 8,38 ms em média nesses frames. O tempo exclusivo de montagem da malha chegou a 10,87 ms no pior frame medido para essa métrica, com média de 1,89 ms nos frames em que rodou. A amostra associa os picos ao trabalho da Game Thread e ao streaming, mas não isola um único responsável. Preserve a arquitetura atual e capture uma trilha Unreal Insights para atribuir o tempo restante antes de mover tarefas para workers.

| CSV em `Saved/Bench` | SHA-256 |
| --- | --- |
| Corrigido `Profile(20261119_175023).csv` | `ac74ec79cbfbf6abd12fdac5090f22a8e97237442d6ff11e0dedeec5f1177879` |
| Original `Profile(20261119_175125).csv` | `de63db40988e5c80ca8444f53ed8265abf429a8165b4549c899638ec26a3c5b2` |

Na worktree de correções, execute a partir da raiz.

```powershell
& .\Tools\Bench\test-bench-output.ps1
$exe = (Resolve-Path .\Builds\Marco02Correcoes\Windows\EcosDeAethra.exe).Path
& .\Tools\Bench\bench.ps1 -Name warmup -Seconds 10 -Exe $exe
& .\Tools\Bench\bench.ps1 -Name medicao -Seconds 60 -Exe $exe
```

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
