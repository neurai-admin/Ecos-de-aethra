# LIS: memória

## Fato e lembrança

`FLisEventRecord` é o fato objetivo, guardado num log da simulação (até 4096 eventos) para depuração e relatórios. Nenhum NPC lê esse log.

`FLisMemory` é a versão de um NPC:

| Campo | Significado |
| --- | --- |
| `EventId` | identificador do acontecimento |
| `Type`, `Place`, `Hour` | tipo, local e hora em que ocorreu |
| `Actor`, `Target` | personagens envolvidos (0 = ninguém ou desconhecido, -1 = jogador) |
| `Amount` | quantidade conhecida, como o estoque, o preço pago ou a comida dada |
| `Source`, `ToldBy` | visto diretamente, ou ouvido de alguém |
| `Confidence` | 1 para o que foi visto; menor para o que foi ouvido |
| `Relevance` | importância para este NPC |
| `Valence` | como este NPC julgou o fato, de -1 a 1 |
| `LearnedHour` | quando soube |

As três memórias pedidas estão no mesmo registro. A episódica são os eventos. O conhecimento do ambiente fica em `FLisPlaceBelief` (estoque, risco, confiança, hora, fonte) e no preço visto. A memória social são os eventos com ator e alvo, que também ajustam `Relations`.

## Percepção

- `Emit`: lembram o evento os NPCs presentes no local, sem estar em viagem, além do ator e do alvo.
- `EmitAtCell` (eventos voxel): só os NPCs presentes cujo local está a até `PerceptionRadiusCells` da célula alterada.
- `Observe`: ao chegar ou terminar uma ação, o NPC atualiza a crença sobre o local. As frutas do Ermo são exceção: só são descobertas explorando.

Não há conhecimento global instantâneo. No experimento, ninguém sabia das frutas na hora em que apareceram; nove NPCs souberam depois, quatro por terem visto e cinco por ouvirem de alguém, com confiança entre 0,60 e 0,70.

## Transmissão

Ao conviver, cada NPC conta ao outro até duas memórias recentes (até 96 horas), as mais relevantes que o ouvinte ainda não tem. O ouvinte recebe a versão de quem contou, com confiança igual à confiança de quem contou × (0,3 + 0,6 × confiança no falante). Boatos têm metade do efeito de algo visto e ainda são escalados pela confiança. Isso vale para risco, estoques e relações com terceiros.

## Interpretação individual

`Valence` depende da personalidade e do envolvimento. Um perigo é pior para quem é cauteloso. Ver alguém ajudar agrada mais a quem é empático. Árvores cortadas pesam para o lenhador e quase nada para os outros. O teste 05 confere que duas testemunhas do mesmo perigo o julgam de forma diferente.

A relevância parte de um valor por tipo e aumenta se o NPC esteve envolvido, se o fato aconteceu em seu local de trabalho ou se envolve alguém por quem sente afeto ou ressentimento forte.

## Esquecimento

Cada NPC guarda até `MemoryCapacity` memórias (48). Ao exceder, esquece a de menor `relevância × confiança / (1 + idade/72h)`. Em 20 dias de Elden foram formadas 1266 memórias, esquecidas 337 e mantidas 929.
