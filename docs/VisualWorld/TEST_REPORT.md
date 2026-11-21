# Relatório de testes

Arquivo: `Source/EcosDeAethra/Private/Tests/AethraVisualWorldTests.cpp`. Os testes rodam numa pasta temporária (`Saved/VisualWorldTests`), apagada ao final.

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\ECOS_DE_AETHRA_VW\EcosDeAethra.uproject' -Unattended -NullRHI -NoSound '-ExecCmds=Automation RunTests Aethra' '-TestExit=Automation Test Queue Empty'
```

## Resultado (20/11/2026): 21 de 21 passam

| Teste | Verifica | Medido |
| --- | --- | --- |
| VisualWorld.01 MaterialSelectionDeterministic | 4.000 ambientes aleatórios, avaliados duas vezes em ordem inversa, dão a mesma escolha. Regras: inclinação dá rocha, frio dá neve, beira d'água dá areia, cascalho ou lama. Clima e desgaste não trocam a superfície. Construção por cultura e riqueza | 0 divergências |
| 02 BiomeRules | Regras de cada bioma; nenhum bioma futuro é gerado; regiões e não mosaico | 7 biomas em 4 × 4 km; 5,8% de trocas a cada 8 m (limite: 12%) |
| 03 ChunkConsistency | Dois geradores com a mesma seed concordam em 3.000 pontos. Vértices de borda entre chunks vizinhos são idênticos | 24 bordas, 1.560 vértices, 0 diferenças |
| 04 GeometryPersistence | As 13 formas são gravadas e recarregadas idênticas. Remoção persiste; ids não são reaproveitados; arquivo truncado é recusado sem perda. O assentamento é gravado e recarregado com a peça do jogador | 43 peças + 1 |
| 05 ExistingSavesPreserved | Checksum de 4.096 colunas voxel da seed 42819 igual ao da base; iniciar o Visual World não toca nenhum `.sav` | `0xdbc636bf` |
| 06 RiverContinuity | Rio principal: cada nó drena para o seguinte, nunca sobe, tem água ao longo do traçado desenhado e não salta de nível nas bordas de chunk | 334 nós (5,3 km); 320 verificados, 0 secos; 30 travessias de borda |
| 07 FreshVersusSaltWater | Mar salgado; rios, riachos, lagos e cachoeiras doces e menos densos. Flutuabilidade: 1.010 kg/m³ afunda na água doce e flutua no mar. A salinidade não muda a cor; o sedimento muda | — |
| 08 DepthVariation | Transmitância cai com a profundidade; raso mostra o fundo, fundo mostra o in-scatter. No rio, a profundidade varia e cresce para jusante | 1,58–9,40 m; média 4,85 m a montante e 5,17 m a jusante |
| 09 RegistryIntegrity | AME (ids, nomes, faixas, paleta 64 × 4), corpos d'água (ids, limite 16), 13 formas AHG e registro de flora | — |
| 10 VoxelCompatibility | Peça no ar é aceita; mesma célula duas vezes, recusada; peça num bloco sólido, recusada; os blocos não mudam. Remoção libera a célula; minerar o bloco libera o lugar | Motivos de recusa registrados no log |
| 11 RegionStreaming | Os dois níveis presentes; a 3 km a origem descarrega e a região nova carrega; contagens limitadas; ao voltar, o chunk regenera idêntico | 16 chunks L0 + 116 L1; 406 gerados e 274 removidos |
| 12 Performance | Gerador abaixo de 2 s, amostra abaixo de 25 µs, início abaixo de 5 s, chunk médio abaixo de 60 ms | 233 ms; 4,7 µs; 721 ms; 8,4 ms (máx. 37,9) |
| 13 FloraSpecies (adendo) | Espécies comuns só com caixas; pinheiro termina em ponta; só a rara é orgânica. Palmeira só na praia, sem carvalho em montanha fria, anciã abaixo de 1%. Posição determinística; ids decodificam a célula; nunca na água | 3.990 árvores de 6 espécies em 840 × 840 m |
| 14 FloraPersistence (adendo) | Cortar grava o arquivo e reconstrói o chunk sem a árvore; depois de reiniciar continua cortada; outra seed não é afetada | — |
| Voxel.ChunkSeams, Coordinates, Generation, Horizon, InstigatorCompatibility, PersistAcrossRestart, SaveRoundTrip | Testes antigos do mundo voxel, **sem alteração** | Passam |

Nenhum teste antigo foi modificado.

## Também verificado no jogo

- **Capturas:** 8 cenas tiradas pelo motor com `-AethraVisualWorld -AethraVWShots`: dia, entardecer, noite, rio e ponte, mar, lago, cachoeira e arquitetura (ver [captures/](captures/)).
- **Benchmark:** ver PERFORMANCE.md.

## Não verificado

- Build empacotada (Shipping) com o Visual World.
- Outras GPUs.
- Interface de jogo para colocar ou remover peças e cortar árvores no Visual World: só a API e os testes existem.
