# LIS 02: os NPCs no mundo voxel (integração experimental)

Data: 20/11/2026. Branch `claude/lis-world-integration-01`, worktree `D:\ECOS_DE_AETHRA_LISW`. Unreal Engine 5.8.4 (CL 58902052).

Esta é a primeira integração jogável do LIS. Os 20 habitantes de Elden têm corpos 3D provisórios, decidem pelo LIS, andam pelo terreno voxel, percebem alterações de blocos, guardam memórias e cortam madeira por uma fila validada. **Não é um sistema final:** os corpos são caixas, a navegação é uma grade própria e o PR só deve entrar depois dos PRs de que depende (ver o fim deste documento).

## Base da branch

| Commit | Conteúdo |
| --- | --- |
| `095df36` | `codex/marco-02-correcoes` (local, não publicada no GitHub): Marco 02 + correções do GPT/Codex (SecurityToken removido, script de benchmark protegido, sombreamento nas junções, cook limpo) |
| `af65501` | merge de `claude/lis-core-01` (PR #2, com as correções da revisão) |
| `c82e02c` | merge de `claude/voxel-contract-lis-01` (extensão do contrato voxel, revisão separada) |
| `2273e14`, `e03f1e2`, `b1b066d` e seguintes | trabalho do LIS 02 |

As branches originais não foram alteradas. A worktree do GPT/Codex (`Documents\Codex\...\work\marco-02`) foi apenas lida.

## Arquitetura

```
AAethraVoxelWorld ──OnBlockChanged(Cell, Before, After, Instigator)──► AAethraLisDirector::HandleBlockChanged
      ▲                                                                   │ só enfileira (adaptador) e invalida a coluna da navegação
      │ SetBlockBy(..., Npc(id))                                          ▼
      │                                                   AAethraLisDirector::Step (por quadro)
      │                                                     1. aplica pedidos de bloco dos NPCs (validados)
      └────────────── fila de pedidos ◄───────────────────  2. Flush do adaptador → eventos LIS (percepção por posição)
                                                            3. relógio LIS: Tick(1) a cada SecondsPerHour (30 s)
                                                            4. corpo segue a mente: SyncBody por NPC
                                                            5. caminhos A* com orçamento (2 por quadro)
                                                            6. movimento, animação e encontros
FLisSimulation (módulo AethraLIS) ◄── Arrive / TravelFailed / Interrupt / Meet / Perceives / OnDecision
```

| Arquivo | Papel |
| --- | --- |
| `Source/EcosDeAethra/Public/AethraLisDirector.h`, `Private/LIS/AethraLisDirector.cpp` | Ponte LIS ↔ mundo: relógio, âncoras, corpos, percepção, fila de blocos, save, overlay, capturas |
| `Public/AethraLisNav.h`, `Private/LIS/AethraLisNav.cpp` | Navegação voxel: cache de colunas e A* |
| `Public/AethraLisNpc.h`, `Private/LIS/AethraLisNpc.cpp` | Corpo provisório e movimento ao longo do caminho |
| `Source/AethraLIS/...` | Ganchos novos no núcleo (abaixo) |
| `Private/AethraGameMode.cpp` | 8 linhas: cria o diretor em `StartPlay`, exceto com `-NoLIS` |
| `EcosDeAethra.Build.cs` | Dependência pública de `AethraLIS` |

### Ganchos no núcleo do LIS

| Gancho | Uso |
| --- | --- |
| `Config.bExternalTravel` | A viagem não tem duração fixa; termina quando o mundo chama `Arrive(id)` ou `TravelFailed(id)` |
| `Arrive` | O NPC chega, registra a chegada, observa o local e inicia (ou replaneja) a intenção |
| `TravelFailed` | Sem caminho: o NPC fica onde está, aprende que o plano foi ruim (expectativa −0,5) e decide de novo |
| `Interrupt` | O mundo não conseguiu executar a ação (ex.: nenhuma árvore alcançável) |
| `Meet` | Encontro entre dois NPCs, lembrado só por eles |
| `Perceives` | Quem percebe uma mudança voxel, pela posição real do corpo |
| `OnDecision` | Todas as opções pontuadas da última decisão, para o overlay |

Eventos novos `Arrived` e `Met` têm relevância baixa, não são recontados e são esquecidos primeiro. O save do LIS passou à versão 3; as versões 1 e 2 continuam carregando.

## Decisões e comportamentos

A decisão continua sendo do Utility AI do LIS (fase 1). O mundo só executa e informa o resultado:

| Atividade (LIS) | No mundo |
| --- | --- |
| Viajar | Caminho A* até o lugar pessoal do NPC ao redor da âncora do destino; chegada chama `Arrive` |
| Cortar lenha (coletar) | Procura um tronco a até 12 células da Floresta, anda até o lado dele e pede a remoção de um bloco de madeira |
| Plantar, patrulhar, explorar (trabalhar/explorar) | Anda em volta do lugar (raio 4, 8 e 10 células) |
| Dormir (descansar) | Vai ao seu lugar na Vila e deita |
| Conviver, ajudar, pedir ajuda (socializar) | Anda até a pessoa escolhida pelo LIS e vira-se para ela; troca de informações e efeitos sociais ficam no LIS |
| Pescar, vender, comprar, comer | Fica no lugar do Rio, Mercado ou Vila |

O replanejamento acontece quando o caminho é bloqueado, quando o destino fica inacessível ou quando um perigo visto interrompe a ação. Não ocorre a cada quadro. A troca de objetivos continua diária e com histerese.

## Percepção e memória

- **Alterações de blocos:** o evento chega com o autor (`Instigator`): jogador → `LisPlayer`, NPC → id persistente, outro → desconhecido. O adaptador agrupa as mudanças de cada quadro, e só NPCs cujo corpo está a até 20 células na horizontal e 10 na vertical percebem. Não há teste de linha de visão.
- **Encontros:** a cada segundo, pares a menos de 2,6 m com ao menos um andando registram `Met`. O mesmo par no máximo duas vezes por dia; cada NPC, no máximo um encontro por hora.
- **Chegadas:** só o viajante registra `Arrived`.
- **Compartilhamento:** ao conviver, o LIS passa memórias recentes com confiança reduzida (fase 1).
- Cada memória guarda o que, onde (local e, no log de eventos, a célula exata), quando, como (visto ou ouvido), quem e a relevância.

Um lenhador que corta madeira no próprio local de trabalho não gera ressentimento nos colegas. Remoções feitas pelo jogador continuam gerando.

## Navegação

A UE tem NavMesh e StateTree, mas um NavMesh sobre malhas procedurais que mudam a cada bloco exigiria reconstruções frequentes. Para esta fase foi usada uma grade própria, mais simples e testável:

- **Coluna:** cada coluna do terreno tem uma altura onde se pode ficar em pé, com pés e cabeça livres. Água e troncos bloqueiam; folhas altas e plantas não.
- **Leitura:** `GetColumn` faz uma geração por coluna. As colunas são calculadas sob demanda numa área de 145 × 145 em torno da vila, e o início lê cerca de 19 mil colunas em 21–28 ms.
- **Passos:** sobe 1 bloco, desce até 2, e diagonais só sem cortar quinas.
- **Busca:** A* com limite de 6.000 expansões.
- **Atualização regional:** uma mudança de bloco invalida só a sua coluna, e só os NPCs cujo caminho restante passa por ela replanejam. Nada é reconstruído por inteiro.
- **Destino inacessível:** a busca falha, o painel mostra o erro e o LIS recebe `TravelFailed`.
- **Movimento:** cinemático, de centro a centro de célula, com a altura do terreno. Ele não depende da colisão dos chunks, que só existe perto do jogador. A cápsula do NPC bloqueia o jogador, mas não os traços de edição de blocos.

## Ações seguras sobre blocos

O LIS pede, e o mundo valida e executa:
1. O NPC pede `FAethraBlockRequest { NpcId, Cell, Expected, Result }` e o pedido entra numa fila.
2. No quadro seguinte, fora de qualquer callback, o diretor confere:
   - se o NPC existe;
   - se ele já alterou um bloco nesta hora de jogo (no máximo uma alteração por hora);
   - se o bloco ainda é o esperado;
   - se o bloco está a até 4,5 células dos olhos;
   - se a regra permite a alteração: só madeira, só virando ar, só para quem está cortando lenha ou é lenhador, e dentro da área da vila.
3. `SetBlockBy(..., Npc(id))` aplica a alteração pelo motor voxel, que mantém a validação e o save dele.
4. O `OnBlockChanged` resultante chega com o autor e vira percepção para os NPCs próximos.

O callback só enfileira, então não há edição recursiva.

## Persistência

- **LIS:** `Saved/LIS/World_<seed>.lis` é gravado a cada 6 horas de jogo e ao sair. Ao iniciar, um save existente é restaurado, incluindo as âncoras reais dos locais. Os corpos reaparecem no lugar do local atual, e quem estava viajando volta a andar.
- **Voxel:** o formato do save não mudou. As edições dos NPCs são edições voxel comuns, e o autor não é gravado.

## Desempenho medido

Rota de benchmark do Marco 02 (`bench.ps1`, 60 s, passo fixo, 1080p, editor em `-game`), uma execução por configuração, na mesma máquina:

| Configuração | FPS médio | Tempo de quadro p95 | GameThread média | GameThread p95 |
| --- | --- | --- | --- | --- |
| Sem NPCs (`-NoLIS`) | 121,1 | 9,29 ms | 2,91 ms | 4,49 ms |
| 20 NPCs, overlay desligado (`aethra.lis.Debug=0`) | 121,3 | 9,31 ms | 3,04 ms | 4,34 ms |
| 20 NPCs, overlay ligado (padrão da demo) | 120,9 | 9,31 ms | 3,72 ms | 5,60 ms |

- **Diretor do LIS** (estatística CSV `AethraLIS/DirectorMs`, overlay ligado): média de 0,21 ms, p95 de 0,51 ms e máximo de 2,6 ms. O máximo acontece quando vários caminhos são calculados no mesmo quadro.
- **Custo da integração:** com o overlay desligado, cerca de 0,13 ms de GameThread, sem mudança de FPS. A cena é limitada pela GPU e pelo RenderThread.
- **Overlay:** o desenho do texto de depuração custa cerca de 0,7 ms de GameThread. Ele é uma ferramenta, não parte do jogo.
- **Teste `Aethra.LISWorld.12`:** sem jogador, todos os NPCs animados e 1.800 quadros, o diretor custou 0,21–0,27 ms em média. O movimento e a animação respondem por 0,2 ms. As consultas de caminho custam 0,002 ms em média, com 71 buscas e nenhuma falha.
- **Distância:** acima de 50 m os membros não são animados; acima de 150 m o corpo é ocultado (o movimento continua).

Limites da medição: uma execução por configuração, num único computador, no editor e não na build empacotada. Não foi medido com mais de 20 NPCs.

## Depuração

O overlay é ligado por padrão na demo:
- `aethra.lis.Debug 0` desliga o overlay; `2` mostra só os nomes.
- O NPC inspecionado é o que está mais perto da mira, ou um id fixado com `aethra.lis.Select <id>`.

O painel do NPC inspecionado mostra:
- identidade, profissão, objetivo e ação;
- local e destino;
- necessidades e personalidade;
- inventário;
- o estado da navegação (passo, replanejamentos de caminho);
- as quatro melhores opções da última decisão, com as notas;
- as cinco memórias mais recentes, com fonte, confiança e relevância;
- o último erro;
- as últimas ações validadas ou recusadas.

Outros controles:
- `aethra.lis.SecondsPerHour` muda o ritmo do relógio.
- `-AethraLisShots` posiciona a câmera, tira 4 capturas no motor e encerra. As capturas desta entrega estão em [captures/](captures/).

## Testes

`Aethra.LISWorld.01–12` (`Source/EcosDeAethra/Private/Tests/AethraLisWorldTests.cpp`). Eles rodam num mundo com a seed 2026 e apagam ao final os saves voxel e do LIS dessa seed:

| Teste | Verifica |
| --- | --- |
| 01 Initialization | O LIS inicia no mundo; as 6 âncoras são caminháveis e alcançáveis a partir da vila |
| 02 NpcCreation | 20 corpos sobre chão caminhável |
| 03 IdentityLink | Cada identidade do LIS tem exatamente o seu corpo; nomes distintos |
| 04 AutonomousGoals | Em 3 horas todos decidem; quem viaja anda para o destino escolhido pelo LIS; ao menos 4 atividades diferentes (foram 8) |
| 05 Movement | Moradores chegam ao destino (`Arrived`) e ficam perto do local atual (9 chegadas) |
| 06 PathReplanning | Um muro no caminho faz só esse NPC replanejar e contornar; destino murado faz o LIS registrar a falha e replanejar; o erro aparece nos dados de depuração |
| 07 Perception | O jogador remove um bloco: o vizinho percebe, com o jogador como autor; quem está a 45 células não |
| 08 MemoryRecord | O que, quando, como, quem, relevância e a célula exata; chegadas lembradas pelo viajante |
| 09 Socialization | Encontro registrado pelos dois e relação criada; quem vai conviver anda até a pessoa escolhida |
| 10 PersistenceAfterRestart | Novo diretor restaura do disco o mesmo estado (checksum), memórias, âncoras e corpos |
| 11 VoxelSaveCompatibility | Pedido com bloco errado, escavação, limite por hora e fora de alcance são recusados; o corte válido é aplicado, chega ao LIS com o NPC como autor e é restaurado por um novo mundo voxel; o LIS usa arquivo próprio |
| 12 Performance20Npcs | 1.800 quadros com 20 NPCs: decisões continuam e o custo médio fica abaixo de 4 ms (limite folgado só contra regressões graves) |

Resultado: os 31 testes `Aethra.*` passam (12 `Aethra.LIS`, 12 `Aethra.LISWorld` e 7 `Aethra.Voxel`, incluindo `InstigatorCompatibility`).

## Como rodar a demo

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' EcosDeAethraEditor Win64 Development '-Project=D:\ECOS_DE_AETHRA_LISW\EcosDeAethra.uproject' -WaitMutex
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\ECOS_DE_AETHRA_LISW\EcosDeAethra.uproject' -game -windowed -ResX=1600 -ResY=900
```

A vila fica cerca de 20 blocos à frente do ponto de partida, na direção em que a câmera começa. Mire num habitante para ver o painel. Apagar `Saved/LIS/World_42819.lis` recomeça a vila.

## Limitações conhecidas

- Os corpos são provisórios (caixas com cores por profissão), sem esqueleto nem animação real.
- A navegação é um heightfield: um nível por coluna, sem cavernas, pontes, escadas ou natação. Os NPCs não pulam vãos, e o movimento cinemático ignora colisão com outros NPCs e com o jogador.
- Não há teste de linha de visão na percepção, nem audição.
- Os lenhadores consomem troncos próximos da vila sem replantar. Cada um tira no máximo um bloco por hora de jogo.
- A posição exata dos corpos não é salva; ao carregar, eles voltam ao lugar do local atual.
- Fora da área de 145 × 145 células não há navegação. Se o jogador se afastar, os NPCs continuam simulados, e os corpos a mais de 150 m ficam ocultos.
- A vila de seed 42819 fica num terreno acidentado de floresta, e a Floresta está a 6 células da Vila. Escolher as âncoras com mais critério (área plana, distância mínima) é trabalho futuro.
- O overlay custa cerca de 0,7 ms de GameThread quando ligado.

## Dependências entre PRs

| Item | Estado | Necessário para |
| --- | --- | --- |
| PR #1 `claude/marco-02-graficos` | Aberto, sem merge | Base do Marco 02 |
| `codex/marco-02-correcoes` (GPT/Codex) | Só local, sem PR | Correções de segurança e do Marco 02; esta branch foi criada sobre ela |
| PR #2 `claude/lis-core-01` | Aberto, sem merge | Núcleo do LIS |
| `claude/voxel-contract-lis-01` | Local; precisa de revisão da equipe GPT/Codex | `Instigator`, `SetBlockBy`, `GetColumn` |
| `claude/lis-world-integration-01` | Esta branch | Depende das quatro acima |

Ordem sugerida:
1. Revisar e aprovar o Marco 02 com as correções do Codex (publicar `codex/marco-02-correcoes`).
2. Aprovar o PR #2.
3. Revisar a extensão do contrato voxel.
4. Só então rebasear ou abrir o PR desta integração contra a `main` atualizada.
