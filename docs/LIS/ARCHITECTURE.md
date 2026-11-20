# LIS: arquitetura (fase 1)

O Living Intelligence System (LIS) é a fundação da simulação de NPCs de ECOS DE AETHRA. Esta fase implementa e valida o núcleo; não é uma inteligência completa. Os NPCs escolhem ações por utilidade a partir do que sabem, lembram o que viram ou ouviram, aprendem com resultados e mantêm relações próprias. Não há roteiro por personagem nem rede neural.

## Módulo

`Source/AethraLIS` é um módulo Runtime separado, que depende só de `Core`. Ele não inclui nenhum arquivo do módulo `EcosDeAethra` e roda sem mundo, atores ou renderização, o que permite testá-lo isoladamente e de forma determinística.

| Arquivo | Papel |
| --- | --- |
| `Public/LisTypes.h` | Tipos: identidade, personalidade, necessidades, memória, crenças, relações, estado do mundo abstrato |
| `Public/LisSimulation.h`, `Private/LisSimulation.cpp` | Escalonador, cognição (utilidade), planejamento, execução, percepção, memória, aprendizado, relações e objetivos |
| `Private/LisSerialization.cpp` | Formato de save versionado, checksum |
| `Public/LisVoxelAdapter.h`, `Private/LisVoxelAdapter.cpp` | Entrada de eventos do mundo voxel |
| `Public/LisElden.h`, `Private/LisElden.cpp` | Experimento Vila de Elden (20 NPCs) e relatório |
| `Private/AethraLISModule.cpp` | Registro do módulo, log `LogAethraLIS`, nomes em português |
| `Private/Tests/LisTests.cpp` | 12 testes `Aethra.LIS.*` |

Arquivos compartilhados alterados: `EcosDeAethra.uproject` (declara o módulo) e os dois `*.Target.cs` (`ExtraModuleNames.Add("AethraLIS")`). Nenhum arquivo do motor voxel foi alterado.

## Componentes pedidos e onde estão

| Componente | Implementação |
| --- | --- |
| NPC Identity | `FLisIdentity`: id, nome, idade, profissão, origem. Gerada pela seed e pelo índice. |
| NPC Personality | `FLisPersonality`: coragem, ambição, empatia, curiosidade, agressividade, lealdade, sociabilidade e cautela, de 0 a 1. Configurável por código e salva. |
| NPC Needs | Segurança, alimentação, descanso, trabalho, convivência e propósito, como déficit de 0 a 1. |
| NPC Memory | `FLisMemory` por NPC, com o fato objetivo em `FLisEventRecord`. Ver [MEMORY_SYSTEM.md](MEMORY_SYSTEM.md). |
| NPC Cognition | `Evaluate`, `Utility`, `EvaluateGoal`. Ver [NPC_COGNITION.md](NPC_COGNITION.md). |
| NPC Planning | `Decide` e `StartIntent`: deslocamento até o local, validação das pré-condições na chegada, replanejamento. |
| NPC Learning | Habilidade, risco por local, expectativa por ação. Ver [LEARNING_SYSTEM.md](LEARNING_SYSTEM.md). |
| NPC Social | Relações dirigidas com cinco valores. Ver [SOCIAL_SYSTEM.md](SOCIAL_SYSTEM.md). |
| Simulation Scheduler | `Tick` por hora de jogo, três níveis de detalhe e orçamento de decisões. |

## Mundo abstrato

Seis locais com posição num mapa de horas de caminhada: Vila (com o celeiro comunitário), Campos, Floresta, Rio, Mercado e Ermo. Cada local tem estoque, capacidade, rebrota, perigo e uma célula voxel de ancoragem. Preço da comida sobe quando o celeiro baixa. O estoque do Mercado é a comida nas bancas: cresce com importações (0,4 por hora, até 30) e com a comida que os aldeões vendem, e só ele abastece quem compra. O mundo é a verdade; os NPCs só conhecem suas crenças sobre ele.

## Ciclo de uma hora (`TickHour`)

1. O mundo rebrota, o perigo volta ao valor base e o preço é recalculado.
2. As necessidades de todos os NPCs mudam.
3. Para cada NPC, em ordem rotativa: define o nível pelo local; o nível distante recebe um passo agregado a cada 12 horas; uma vez por dia, às 6h, reavalia o objetivo; conclui a ação que terminou e aplica o resultado; se estiver livre, decide, dentro do orçamento.

## Níveis de simulação

| Nível | Distância do foco | Comportamento |
| --- | --- | --- |
| Próximo | até `NearRadius` | Decisão completa por utilidade e troca de conhecimento em conversas |
| Intermediário | até `MidRadius` | Mesma decisão, mas cada ação dura o dobro e rende o dobro, e conversas não transmitem memórias |
| Distante | além | Sem decisão individual: a cada `FarStepHours` consome comida e produz pela profissão |

`MaxDecisionsPerTick` limita as decisões por hora; quem excede decide na hora seguinte (`Stats.Deferred`). Não há paralelismo nesta fase, como pedido.

## Determinismo

Toda variação vem de `Rand(ator, sal)`, um hash de seed, hora, ator e sal, sem estado oculto. A mesma seed produz a mesma simulação, e um save restaurado continua exatamente como o original (teste 09). Os tempos medidos não entram no save nem no checksum.

## Persistência

`SaveToFile` grava um binário com assinatura `LIS1` e versão 2: configuração, mundo, estatísticas, log de eventos e, por NPC, identidade, personalidade, necessidades, objetivo, inventário, habilidades, expectativas, crenças, memórias, relações, plano em andamento e contadores. A escrita usa arquivo temporário e renomeação. A versão 2 acrescenta, por NPC, os eventos esquecidos recentemente (`Forgotten`); saves da versão 1 continuam carregando. Versão desconhecida, arquivo truncado, enum fora da faixa, configuração inválida (passo distante, capacidades ou orçamento zerados ou negativos) e id de NPC fora da posição são rejeitados sem alterar o estado. O save voxel não foi alterado; o LIS deve gravar em arquivo próprio, por exemplo `Saved/LIS/<seed>.lis`.

## Limites desta fase

- O mundo é abstrato; não há movimento nem modelos 3D.
- Planejamento de uma etapa (deslocar e agir); GOAP e StateTree ficam para depois.
- Sem famílias, organizações, economia ou guerras: só a estrutura que os comporta (relações, reputação derivável, objetivos).
- O evento voxel não informa quem editou; ver [WORLD_INTEGRATION.md](WORLD_INTEGRATION.md).
