# Desempenho da Vila Viva (LIS 03)

Este documento traz o custo medido dos habitantes 3D, das animações e da vila, nas mesmas condições com e sem o LIS.

## Como foi medido

```powershell
powershell -ExecutionPolicy Bypass -File Tools\Bench\bench.ps1 -Name lis03-sem-lis -Elden -NoLIS
```
```powershell
powershell -ExecutionPolicy Bypass -File Tools\Bench\bench.ps1 -Name lis03-com-lis -Elden -Extra '-dpcvars=aethra.lis.Debug=0'
```
```powershell
powershell -ExecutionPolicy Bypass -File Tools\Bench\bench.ps1 -Name lis03-com-painel -Elden
```

- **Rota:** a mesma do Marco 02, com 3.601 quadros a passo fixo de 60 Hz (`-benchmark -fps=60`). O FPS é o número de quadros dividido pelo tempo real.
- **Configuração:** 1920×1080 no editor em `-game`, mundo de demonstração (`-Elden`, seed 673).
- **Hardware:** Ryzen 5 5500, RTX 3060 12 GB, 16 GB de RAM.
- **Repetições:** uma execução de cada configuração. Diferenças de cerca de 3% estão dentro do ruído de uma execução só.
- **Isolamento:** cada execução usa um `-userdir` próprio, então a vila é construída no começo de cada uma com LIS (cerca de 180 quadros). Sem LIS não há vila nem habitantes; as árvores do mundo de demonstração ficam no lugar.
- **Custo do diretor:** `AethraLIS/DirectorMs` e `AethraLIS/AnimMs` no CSV. Os quadros depois da construção foram calculados à parte, a partir do CSV.

## Resultados

| Execução | FPS médio | Quadro médio | p95 | GameThread | RenderThread | GPU |
|---|---|---|---|---|---|---|
| Sem LIS (sem vila e sem habitantes) | 135,5 | 7,38 ms | 8,29 ms | 2,66 ms | 6,05 ms | 6,91 ms |
| Vila e 20 habitantes, painel desligado | 131,2 | 7,62 ms | 8,77 ms | 3,50 ms | 6,23 ms | 7,06 ms |
| Vila e 20 habitantes, painel ligado | 135,3 | 7,39 ms | 8,29 ms | 4,12 ms | 6,01 ms | 6,94 ms |

Custo do diretor nas execuções com LIS, depois da construção:

| Medida | Painel desligado | Painel ligado |
|---|---|---|
| Diretor por quadro: LIS, percepção, navegação, movimento e animação | 0,214 ms (p95 0,289; máx 0,50) | 0,196 ms (p95 0,257; máx 0,92) |
| Só a animação dos 20 esqueletos | 0,194 ms (p95 0,263) | 0,178 ms (p95 0,233) |
| Construção da vila (só na primeira vez) | cerca de 180 quadros com `SaveEdits` de 6,7 ms em média (máx 28 ms) | igual |

Testes automatizados, sem renderização (`Aethra.LISVillage.10`, 3.600 quadros a 60 Hz): diretor com média de **0,29 ms**, animação **0,11 ms**, pior quadro 1,6 ms, 215 caminhos sem nenhuma falha.

## Leitura

- **FPS:** a cena continua limitada pela GPU (cerca de 7 ms). Os habitantes e a vila praticamente não mudam o FPS: −3% numa execução e 0% na outra, dentro do ruído.
- **GameThread:** sobe 0,84 ms com a vila e os habitantes. Desse total, o diretor responde por cerca de 0,21 ms, quase tudo de animação. O resto (cerca de 0,6 ms) é custo do motor com 20 malhas esqueléticas e 4 acessórios presos a ossos por habitante, mais a geometria extra da vila nos chunks. Essa parte não foi isolada: ficaria para um perfil com Unreal Insights.
- **Painel de depuração:** custa cerca de 0,6 ms na GameThread (texto em Canvas), como no LIS 02. `aethra.lis.Debug 0` remove esse custo por completo.
- **Comparado ao LIS 02:** o diretor era 0,21 ms com corpos de cubos e é cerca de 0,21 ms com esqueletos. A animação procedural custa cerca de 0,2 ms no jogo e 0,11 ms sem renderização.

## Otimizações em uso

- **Corpo:** uma seção de malha e um material por habitante (cores por parâmetro), com acessórios só quando a profissão ou a atividade pedem.
- **Distância:** a 50 m a pose é atualizada só 4 vezes por segundo; acima de 150 m o corpo é escondido e a animação não roda.
- **Caminhos:** no máximo 2 buscas A* por quadro, com invalidação só da coluna alterada. Só os caminhos que passam por ela são refeitos.
- **Percepção:** encontros, trabalho visto e chegadas são verificados 1 vez por segundo, com limites por par e por observador.
- **Pedidos de bloco:** com o limite de 1 por hora, o habitante espera em vez de pedir de novo a cada quadro.

## Pendências de desempenho

- Medir com 3 ou mais execuções por configuração e num build empacotado.
- Separar o custo das malhas esqueléticas do custo da geometria da vila, com a vila já construída nas duas execuções.
- A construção inicial causa engasgos de até 28 ms por bloco salvo. Agrupar os salvamentos exigiria uma API no motor voxel, que pertence à equipe GPT/Codex e precisa ser coordenada.
