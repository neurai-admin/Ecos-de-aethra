# Horizonte real e alcançável

Código: `AAethraVisualWorld::UpdateHorizon`, `LaunchHorizonTile` e `ApplyHorizonTile` (`Private/VisualWorld/AethraVisualWorld.cpp`).

## Aethra Reachable Horizon Rule (obrigatória)

Tudo o que aparece no horizonte como terreno, montanha, floresta, rio, lago, ruína ou cidade existe de verdade no mundo, e o jogador pode chegar lá. Só céu, nuvens, sol, lua e efeitos atmosféricos são exceção.

Como o sistema cumpre a regra:
- **Mesmo gerador:** o horizonte é amostrado do mesmo `FAethraWorldGen`, com a mesma seed, que gera os chunks jogáveis. Não há imagem de fundo nem malha decorativa.
- **Prova automática:** o teste 18 compara vértices do horizonte com a altura real nos mesmos pontos (diferença máxima medida: 0,000 m, descontado o rebaixamento fixo de 10 m). Também confere que os chunks e as ruínas usam a mesma superfície.
- **Montanhas além do domínio de drenagem (±4 km):** o relevo continua (`BaseHeight`), então as cordilheiras vistas a 20 km existem e o streaming as gera ao chegar perto.
- **Ruínas e construções:** ficam sobre a superfície real e são desenhadas a qualquer distância. O que se vê de longe é o que se encontra.
- **Edições do jogador:** `MarkHorizonDirty` reconstrói os blocos afetados. Escavações pequenas não aparecem a quilômetros; uma mudança grande o bastante para ser vista de longe ainda não altera o horizonte, porque o horizonte amostra a altura do WorldGen e não as edições (ver ROADMAP.md).

## Níveis de detalhe

| Faixa | Representação | Passo | Distância (perfil Alto) |
| --- | --- | --- | --- |
| Perto | Chunks L0: terreno com colisão, vegetação rasteira, cavernas em volume | 2 m | até 384 m |
| Médio | Chunks L1: terreno simplificado, árvores com LOD (40% das árvores, variante 0), sombras só nos perfis altos | 8 m | até 1,6 km |
| Longe | Blocos de horizonte de 4,096 km, gerados em threads de trabalho | 64 m até 12 km, 128 m depois | até 20 km |

Distância visual realmente alcançada (blocos carregados, medido nas capturas): de 24 a 29 km do observador no perfil Alto, porque o último anel de blocos de 4 km passa do raio de 20 km. No Ultra o raio é 32 km. A névoa atmosférica atenua o que está além de cerca de 15 km.

## Transições e popping

- **Sem z-fighting:** o horizonte fica 10 m abaixo da superfície real (invisível a quilômetros) e os chunks L1 ficam 0,4 m abaixo dos L0. Onde dois níveis se sobrepõem por um instante, o mais detalhado vence.
- **Troca de chunks:** cada chunk L1 sai assim que os seus quatro filhos L0 existem, e cada L0 sai assim que o pai L1 existe. Isso evita buracos e evita que terreno grosso fique sob o fino.
- **Saias:** escondem frestas entre chunks e entre blocos de passo diferente.
- **Cores contínuas:** o terreno tem a mesma cor em todos os níveis. Os campos de flores tingem o terreno em L0, L1 e no horizonte, então são vistos de longe do mesmo jeito que de dentro.
- **Construção espalhada:** um chunk a cada 0,1 s, para reduzir os picos de quadro.
- **Pendente:** ainda não há fade de instâncias na distância de corte; as árvores e a grama aparecem por clusters. Ver ROADMAP.md.

## Ferramentas

- `aethra.vw.Quality 0..3` escolhe o perfil (raio do horizonte: 8, 12, 20 ou 32 km).
- `aethra.vw.ShowHorizon 0` oculta o horizonte (depuração).
- `aethra.vw.Hide <máscara>` oculta camadas (depuração): 1 L0, 2 L1, 4 horizonte, 8 piscinas, 16 cavernas, 32 cachoeiras, 64 vegetação.
