# Desempenho

Meta do prompt: **1080p a 60 FPS numa RTX 3060.** A máquina de teste **é** uma RTX 3060, com Ryzen 5 5500.

## Método

- **Ferramenta:** `Tools/Bench/bench.ps1`, o mesmo script do Marco 02, com a nova opção `-VisualWorld`.
- **Configuração:** 1920 × 1080, VSync desligado, sem limite de FPS, passo fixo (`-benchmark -fps=60`, 3.601 quadros = 60 s de jogo), editor em modo `-game`, captura CSV.
- **Mundo voxel:** a rota do Marco 02.
- **Visual World:** voo circular de 420 m de raio em volta do assentamento, a 30 m do chão e a 15 m/s. Passa sobre o rio, a floresta e a planície, e o streaming carrega chunks durante todo o voo.
- **Aquecimento:** uma execução de 15 s antes, para compilar shaders.

```powershell
powershell -ExecutionPolicy Bypass -File Tools\Bench\bench.ps1 -Name voxel-baseline -Seconds 60
powershell -ExecutionPolicy Bypass -File Tools\Bench\bench.ps1 -Name visualworld-01 -Seconds 60 -VisualWorld
```

## Resultados (uma execução cada, 20/11/2026)

| Medida | Mundo voxel (Marco 02) | Visual World 01 |
| --- | --- | --- |
| FPS médio | 131,8 | **108,0** |
| Tempo de quadro médio / p95 / p99 | 7,59 / 8,83 / 11,94 ms | 9,26 / **10,41** / 12,98 ms |
| 1% low | 68,2 FPS | **63,7 FPS** |
| Máximo | 28,2 ms | 39,3 ms |
| GameThread média / p95 | 2,79 / 4,12 ms | 2,65 / 3,34 ms |
| RenderThread média / p95 | 6,25 / 8,22 ms | 7,85 / 9,92 ms |
| GPU média / p95 | 7,02 / 7,77 ms | 8,71 / 9,51 ms |
| Memória física | 2.913 MB | 2.666 MB |
| Memória de vídeo | 672 MB | 628 MB |

Estatísticas do Visual World (CSV `AethraVW/*`):
- **Quadro do ator:** 0,29 ms em média.
- **Construção de chunk:** 106 construções no voo; 7,96 ms em média, p95 de 13,3 ms e máximo de 15,5 ms.
- **Chunks carregados:** cerca de 139.

Testes (`Aethra.VisualWorld.12`):
- **Gerador:** 211–233 ms.
- **Amostra:** 4,5–4,7 µs.
- **Início do mundo:** cerca de 700 ms, com 138 chunks, 11.262 árvores, 41.781 tufos e cerca de 500 mil triângulos de terreno.

## Leitura

- **Meta atingida neste voo:** a média passa de 100 FPS e o 1% low fica acima de 60 FPS.
- **Custo extra em relação ao mundo voxel:** cerca de 1,7 ms de GPU e 1,6 ms de RenderThread por quadro, por causa de água translúcida com profundidade, instâncias de árvores com WPO de vento, horizonte de 20 km e névoa volumétrica.
- **Picos:** o máximo de 39 ms e o p99 de 13 ms vêm de quadros em que um chunk L0 é construído na GameThread (até cerca de 15 ms). O próximo passo óbvio é gerar as amostras do chunk em uma thread de trabalho; ver ROADMAP.md.

## Limites da medição

- Uma execução por configuração, num único computador, no editor e não na build empacotada.
- Os dois mundos têm conteúdo diferente: a comparação mostra o custo da proposta, não um A/B do mesmo cenário.
- Os limites do teste 12 são folgados: pegam regressões graves, não pequenas.
