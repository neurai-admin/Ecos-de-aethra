# ECOS DE AETHRA

Fundação C++ do sandbox voxel para Windows, desenvolvida para Unreal Engine 5.8. O repositório oficial é [neurai-admin/Ecos-de-aethra](https://github.com/neurai-admin/Ecos-de-aethra).

O pacote Win64 Development gerado no PC abre por `D:\ECOS_DE_AETHRA\Windows\EcosDeAethra.exe`. Mantenha a pasta `Windows` completa ao mover a build.

## Rodar no Windows

1. Use a Unreal Engine 5.8 e uma instalação compatível do Visual Studio ou Build Tools com MSVC x64 e Windows SDK. Consulte a [matriz de suporte da Epic](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine).
2. Coloque o conteúdo do projeto em `D:\ECOS_DE_AETHRA\`. Caso a pasta já seja o clone oficial, extraia os arquivos dentro dela e preserve o diretório `.git`.
3. Compile o alvo `EcosDeAethraEditor` para Win64 Development com o script abaixo. Ajuste o caminho da engine conforme a instalação.
4. Abra `EcosDeAethra.uproject` e inicie **Play**. O modo de jogo monta a cena proceduralmente.

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' EcosDeAethraEditor Win64 Development '-Project=D:\ECOS_DE_AETHRA\EcosDeAethra.uproject' -WaitMutex
```

| Controle | Ação |
| --- | --- |
| WASD, mouse, espaço | Andar, olhar e saltar |
| Clique esquerdo | Remover o bloco mirado |
| Clique direito | Colocar um bloco na face que está sob a mira |
| 1, 2, 3, 4 | Selecionar grama, terra, pedra ou areia |
| F1, F2, F3 | Selecionar distância e qualidade gráfica |
| Console `aethra.Horizon 0/1` | Desligar ou ligar o horizonte distante |

Benchmark repetível a 1080p (rota fixa, captura CSV e screenshots em `Saved/Bench/<nome>`):

```powershell
powershell -ExecutionPolicy Bypass -File Tools\Benchench.ps1 -Name minha-medicao -Seconds 60
```

Os materiais `M_AethraVoxel` e `M_AethraPlant` são gerados por `Tools/Editor/create_voxel_material.py`. Consulte a [passagem de trabalho](docs/HANDOFF.md) antes de continuar o desenvolvimento.

O código inclui vale e colinas, árvores, pinheiros, arbustos, capim e flores, horizonte distante com LOD, iluminação atmosférica, câmera em primeira pessoa, blocos editáveis e salvamento das alterações por seed. O alvo de editor compilou na Unreal Engine 5.8.4 e os testes de automação passaram. Consulte o [relatório de testes](docs/TEST_REPORT.md).

- [Visão do jogo](docs/GDD.md) e [roteiro](docs/DEVELOPMENT_ROADMAP.md).
- [Arquitetura](docs/ARCHITECTURE.md), [projeto técnico](docs/TDD.md) e [motor voxel](docs/VOXEL_SYSTEM.md).
- [Contrato do LIS](docs/LIS_INTEGRATION.md), [convenções](docs/CODING_STANDARDS.md) e [decisões](docs/DECISIONS.md).
