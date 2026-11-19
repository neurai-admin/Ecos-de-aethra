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
