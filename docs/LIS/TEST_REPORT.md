# LIS: relatório de testes (fase 1)

Data: 19/11/2026. Branch `claude/lis-core-01`, worktree `D:\ECOS_DE_AETHRA_LIS`, base `main` (`3d154aa`). Unreal Engine 5.8.4 (CL 58902052), confirmado em `Engine/Build/Build.version`. Máquina: Ryzen 5 5500, RTX 3060 12 GB, 16 GB.

## Como reproduzir

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' EcosDeAethraEditor Win64 Development '-Project=D:\ECOS_DE_AETHRA_LIS\EcosDeAethra.uproject' -WaitMutex
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\ECOS_DE_AETHRA_LIS\EcosDeAethra.uproject' -Unattended -NullRHI -NoSound "-ExecCmds=Automation RunTests Aethra" "-TestExit=Automation Test Queue Empty" -stdout
```

O teste 10 grava o relatório do experimento em `Saved/LIS/Elden_seed2026.md`. A cópia desta execução está em [ELDEN_REPORT.md](ELDEN_REPORT.md).

## Resultado

Os 15 testes passaram: 12 `Aethra.LIS.*` e os 3 `Aethra.Voxel.*` existentes em `main`. A compilação terminou sem erros e sem avisos do módulo (os únicos dois avisos vêm de `Attribute.h` do motor).

| Teste | O que verifica |
| --- | --- |
| 01 DeterministicGeneration | Mesma seed gera a mesma vila; seed diferente gera outra; não há duas personalidades iguais; 5 dias simulados se reproduzem exatamente |
| 02 PersonalityShapesChoices | Explorador corajoso vai ao Ermo arriscado e o cauteloso não; NPC empático alimenta o faminto que vê e o frio não |
| 03 UtilityEvaluation | Fome com comida leva a comer e cansaço a dormir; na escassez, cinco perfis escolhem comprar, pedir ajuda, plantar, explorar e pescar |
| 04 PriorityShift | A fome supera o trabalho; depois de comer, o NPC volta a trabalhar |
| 05 MemoryRecord | Testemunha lembra como visto; quem está longe não; boato tem fonte e menos confiança; o mesmo fato tem valências diferentes; o esquecimento preserva o importante |
| 06 KnowledgeLimitedByPerception | Frutas surgem no Ermo e só o explorador sabe; o aldeão sabe depois de ouvir dele |
| 07 LearningFromExperience | Habilidade cresce com a prática; risco aprendido reduz o corte de lenha; negociações ruins reduzem vender; guarda reavalia o perigo |
| 08 AsymmetricRelations | Ajuda gera confiança e afeto maiores de quem recebe; recusa gera ressentimento só em quem pediu; testemunha passa a respeitar o ajudante |
| 09 PersistenceAcrossRestart | Save em disco, carga com checksum idêntico, memórias da escassez preservadas, 48 horas após a carga idênticas ao original; versão desconhecida e arquivo truncado rejeitados sem alterar o estado |
| 10 EldenTwentyNpcs | 20 NPCs por 20 dias: todos decidem; ninguém sabe das frutas na hora em que surgem; só quem estava na floresta viu o corte; pelo menos 8 atividades na vila e 4 respostas diferentes à crise; necessidades e relações nos limites; menos de 1 ms por hora simulada |
| 11 VoxelAdapter | Sem classificador nada é emitido; 40 blocos viram um evento; mudança distante é ignorada; estoque cai 10 unidades; o lenhador percebe e se ressente do jogador; agricultor e aldeão não percebem; mudança fora do raio de percepção altera o mundo sem ser vista |
| 12 SchedulerTiersAndBudget | Classificação próximo, intermediário e distante; o nível intermediário age em passos dobrados; o distante não decide e é agregado; orçamento de 3 decisões por hora adia 7 e todos decidem em seguida |

Os testes 02, 03, 04, 07 e 08 são de regressão de comportamento: protegem decisões já validadas contra ajustes futuros de pesos.

## Desempenho medido (Elden, seed 2026, 20 NPCs, 480 horas)

Medido com `FPlatformTime` dentro de `Tick`, no editor em `-NullRHI`, numa única thread.

| Métrica | Valor |
| --- | --- |
| Tempo médio por hora simulada | 0,0061 ms |
| Pior hora simulada | 0,0334 ms |
| Decisões | 2297 (5,7 por NPC por dia) |
| Replanejamentos | 75 (3,3 % das decisões) |
| Decisões adiadas pelo orçamento | 0 (orçamento 64) |
| Passos agregados (nível distante) | 0 (todos os locais no nível próximo) |
| Eventos | 237 |
| Memórias atuais / formadas / esquecidas | 929 / 1266 / 337 (até 48 por NPC) |
| Memória da simulação | ~124 KB |
| Tamanho do save | 71.353 bytes |
| Mudanças de objetivo | 5 |

Se a simulação avançasse uma hora de jogo por segundo, 20 NPCs custariam cerca de 6 µs por segundo de jogo. Não foi medido com mais NPCs nem dentro do jogo em execução.

## Observações qualitativas do experimento

Dados em [ELDEN_REPORT.md](ELDEN_REPORT.md). Nenhum resultado foi forçado; as perturbações mudam o mundo, não as escolhas.

- **Escassez:** os sete agricultores plantaram, pescadores pescaram, lenhadores cortaram e venderam e guardas patrulharam. Dois NPCs deram comida a vizinhos, dois compraram e três foram ao Ermo (dois agricultores curiosos e um explorador). Os mais famintos comeram primeiro.
- **Frutas:** quatro NPCs as encontraram explorando e cinco souberam por conversa, com confiança entre 0,60 e 0,70.
- **Corte pelo jogador:**
  - os três lenhadores e mais dois NPCs viram; outros três souberam por conversa;
  - o lenhador agressivo julgou o corte com valência −0,72 e acumulou o ressentimento máximo pelo jogador;
  - o lenhador sem agressividade chegou só a 0,17.
- **Exploradores:** os dois têm pouca coragem e exploraram pouco depois que o perigo aumentou.

## Limitações desta fase

- Utilidade e pesos foram calibrados à mão com um experimento; a vila ainda não foi validada em outras seeds e durações maiores.
- O comportamento não foi integrado ao jogo nem testado com o mundo voxel real; o adaptador foi testado com eventos simulados.
- A avaliação de "decisões coerentes" é feita por testes de cenário e pelo relatório, não por métricas estatísticas.
