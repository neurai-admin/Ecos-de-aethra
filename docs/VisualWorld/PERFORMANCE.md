# Desempenho

Meta do prompt: **1080p a 60 FPS numa RTX 3060.** A máquina de teste **é** uma RTX 3060, com Ryzen 5 5500.

## Prompt 08: perfis gráficos (20/11/2026, build final desta branch)

Mesmo método da seção seguinte: voo de 60 s em passo fixo, uma execução por linha, perfil escolhido por `-Quality`.

```powershell
powershell -ExecutionPolicy Bypass -File Tools\Bench\bench.ps1 -Name alto -Seconds 60 -VisualWorld -Quality 2
powershell -ExecutionPolicy Bypass -File Tools\Bench\bench.ps1 -Name baixo-720 -Seconds 60 -VisualWorld -Quality 0 -Height 720
```

| Configuração | FPS médio | 1% low | p95 / p99 | GameThread média / p95 | RenderThread média | GPU média | RAM | VRAM |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Voxel (Marco 02, referência) | 137,0 | 73,1 | 8,25 / 11,27 ms | 2,53 / 3,62 ms | 6,00 ms | 6,85 ms | 2.930 MB | 672 MB |
| Visual World — Baixo, 1080p | 121,4 | 63,0 | 9,13 / 10,28 ms | 2,45 / 3,05 ms | 6,90 ms | 7,78 ms | 2.667 MB | 570 MB |
| Visual World — Médio, 1080p | 116,6 | 68,4 | 9,54 / 10,41 ms | 2,55 / 3,25 ms | 7,21 ms | 8,11 ms | 2.701 MB | 591 MB |
| Visual World — Alto, 1080p | 108,6 | 80,1 | 10,17 / 10,86 ms | 2,56 / 4,58 ms | 7,81 ms | 8,76 ms | 2.759 MB | 633 MB |
| Visual World — Ultra, 1080p | 101,9 | 74,1 | 11,01 / 11,74 ms | 2,77 / 6,48 ms | 8,34 ms | 9,35 ms | 2.896 MB | 778 MB |
| Visual World — Baixo, 720p | 128,1 | 65,0 | 8,60 / 9,45 ms | 2,48 / 2,92 ms | 6,51 ms | 7,37 ms | 2.665 MB | 526 MB |

| Perfil | Terreno 2 m / 8 m | Horizonte | Vegetação rasteira | Sombras | Névoa volumétrica |
| --- | --- | --- | --- | --- | --- |
| Baixo | 256 m / 1,1 km | 8 km | 140 m, 45% | 80 m, 2 cascatas, 1024 | Não |
| Médio | 320 m / 1,4 km | 12 km | 180 m, 70% | 120 m, 3 cascatas, 2048 | Não |
| Alto | 384 m / 1,6 km | 20 km | 230 m, 100% | 200 m, 3 cascatas, 2048 | Sim |
| Ultra | 448 m / 2,2 km | 32 km | 280 m, 100% | 300 m, 4 cascatas, 4096 | Sim |

Todos os perfis têm o mesmo mundo: biomas, árvores, cavernas, ruínas e construções (teste 23). Muda só o custo de desenho.

Leitura:
- **RTX 3060:** os 4 perfis passam de 100 FPS de média em 1080p. O 1% low fica acima de 60 FPS em todos, e o Alto tem p99 de 10,9 ms.
- **720p quase não muda nada:** a cena é limitada pela RenderThread (componentes de instância por chunk e por espécie) e pelos picos de construção de chunks na GameThread, não pela GPU (7,4 a 9,4 ms).
- **Perfil Baixo:** o 1% low é menor que no Alto porque os quadros normais são mais rápidos e o mesmo pico de construção pesa mais no percentil. A próxima otimização é tirar a construção dos chunks da GameThread (ROADMAP.md).
- **Duas otimizações já feitas:**
  - chunks distantes usam só a variante 0 das árvores (um terço dos componentes);
  - a construção virou 1 chunk a cada 0,1 s, em vez de 2 a cada 0,2 s.
  - Juntas, levaram o 1% low do Alto de 56,6 para 80,1 FPS.
- **Requisitos mínimos:** não definidos. Só esta máquina foi medida; o perfil Baixo em 720p é a meta experimental para computadores mais fracos e precisa ser medido em hardware real.

---

## Protótipo 01 (histórico)

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
