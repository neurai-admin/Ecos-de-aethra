# Roadmap

## Feito no Protótipo 01

1. **AME:** 33 superfícies paramétricas, seleção determinística e um shader para todas.
2. **WorldGen:** relevo, drenagem real, rio principal sinuoso, lagos, mar, biomas por regras, streaming em dois níveis e horizonte de 20 km.
3. **HydroGen:** óptica por corpo d'água, 9 perfis, densidade e flutuabilidade, cachoeiras visuais.
4. **AHG:** 13 formas que podem ser colocadas, removidas e persistidas; compatíveis com voxels.
5. **CivGen-lite:** casa, torre redonda, praça e ponte planejadas pelo rio.
6. **Flora** (adendo): 7 espécies paramétricas com formas em bloco, cônicas, angulares e orgânicas raras; vento; LOD; corte persistente.
7. **Demo e medição:** seed 125, cenas A–D, dia, entardecer e noite, 14 testes novos e benchmark.

## Feito no Prompt Mestre 08 (e adendos)

1. **Direção de arte consolidada:**
   - fantasia geométrica estilizada, sem ultrarrealismo e sem aparência de massinha;
   - uma só dimensão;
   - sem mobs hostis;
   - Aethra Reachable Horizon Rule;
   - fontes próprias, depois redesenhadas como letras entalhadas: Aethra Entalhe, Entalhe Bold e Runa.
2. **Materiais:**
   - relevo procedural em todas as superfícies;
   - brilho só nas superfícies encantadas;
   - bloom e saturação contidos;
   - 10 superfícies novas (pétalas, samambaia, Aetherita, Musgo Luminoso).
3. **FloraGen:**
   - ecologia (sol, fertilidade, água, inclinação);
   - 8 espécies de árvore com 3 variantes;
   - bosques regionais e clareiras;
   - 9 espécies rasteiras;
   - contratos de uso (colheita, corante, perfume, alquimia…).
4. **Campos de flores:** girassóis, lavanda, rosas silvestres, flores mistas e alpinas, visíveis em todos os LODs.
5. **Perfis gráficos e horizonte:**
   - perfis Baixo, Médio, Alto e Ultra;
   - horizonte em blocos assíncronos de 4 km, de 8 a 32 km conforme o perfil, do mesmo WorldGen.
6. **CaveGen:**
   - cavernas naturais, encantadas, obscuras e gigantes;
   - malha volumétrica facetada;
   - formações e cristais;
   - escavação persistente no jogo.
7. **RuinGen:**
   - 8 tipos de ruína e 3 culturas antigas, na superfície e no subsolo;
   - decadência por conservação;
   - lore e ponto de recompensa.
8. **Validação:**
   - 30 testes;
   - benchmark dos 4 perfis e de 720p;
   - 12 capturas no motor.

## Próximos passos (por prioridade)

1. **Construção de chunk fora da GameThread:** amostrar o terreno, a flora e as cavernas numa task e só criar componentes na GameThread. Isso remove os picos de construção que ainda limitam o 1% low (PERFORMANCE.md).
2. **Menos componentes por chunk:** agrupar instâncias de espécies numa mesma malha de LOD distante, porque a RenderThread é o limite hoje.
3. **Fade na distância de corte:** transição suave das instâncias (árvores, grama e flores) em vez de aparecerem por clusters.
4. **Horizonte com as edições:** grandes escavações refletidas nos blocos do horizonte (amostrar a densidade do CaveGen no topo).
5. **Interação no Visual World:**
   - colocar e remover peças AHG pela mira;
   - cortar árvores e coletar madeira;
   - colher flores (usar `EFloraUse`).
6. **LIS no Visual World:** os NPCs pedem `FellTree`, `Dig` e `FAhgLayer::Add` e `Remove` pela fila validada (PR #5).
7. **Ruínas:** pontes antigas, castelos, cidades soterradas, bibliotecas e loot; textos de lore ligados às tags.
8. **Cavernas:** glaciais, inundadas, vulcânicas e minerais, e cavernas com vegetação própria.
9. **Cachoeiras com degrau real** e canal do riacho sempre visível.
10. **Biomas futuros:** deserto, savana, tundra, selva e vulcânico, com flora correspondente (palmeiras, cactos angulares).
11. **Fauna** (aves, peixes, insetos, animais de planície, floresta e montanha) com comportamento simplificado em relação ao LIS. Não começa antes de estabilizar os sistemas principais.
12. **Agricultura e jardinagem:** plantar e cultivar flores; jardins de NPCs e reinos.
13. **Clima e estações:** chuva nos parâmetros do AME, estação do ano no FloraGen.
14. **Fontes na interface completa:** Aethra Runa em títulos e nomes de lugares; tamanhos para 4K. As fontes próprias já existem (Aethra Entalhe, Entalhe Bold e Runa).
15. **Build empacotada e benchmark nela; outras GPUs; requisitos mínimos reais.**

## Decisões em aberto (do diretor criativo)

- O Visual World substitui o terreno voxel como mundo principal, ou os dois convivem (por exemplo, voxel em minas e cavernas, contínuo na superfície)? As entregas mantêm os dois separados e não alteram o contrato voxel. Uma mudança de contrato iria numa proposta separada.
