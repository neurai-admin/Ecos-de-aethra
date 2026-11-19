# Motor voxel

## Coordenadas e geração

Células inteiras usam X/Y no plano e Z na vertical. O mundo aceita X/Y de −16.384 inclusive a 16.384 exclusivo e Z de 0 a 63. A divisão dos chunks arredonda para baixo nas coordenadas negativas. Cada chunk carrega até 16.384 valores de bloco em um array denso.

Ruído inteiro interpolado com parâmetros fixos cria um vale sinuoso com colinas, estratos de pedra, terra e áreas de areia. Árvores e arbustos vêm de amostragem determinística regional. Um tronco ou copa junto à divisa é calculado com as mesmas regras nos dois chunks. A camada Z=0 é protegida.

## Malha e streaming

Faces contra blocos sólidos adjacentes são omitidas. Cada face exposta tem quatro vértices, dois triângulos, normal e cor. Na edição de borda, o chunk vizinho carregado reconstrói a malha. Chunks descarregados consultam o estado atualizado quando entram em alcance.

O mundo prepara o chunk central antes de posicionar o jogador. Depois carrega até um chunk por frame, em ordem aproximada de distância, e descarrega os que saem do raio ativo. Os presets F1, F2 e F3 têm raio de 2, 3 e 4 chunks, respectivamente. O máximo nominal por preset é 25, 49 e 81 chunks.

## Persistência

O salvamento armazena a diferença contra o terreno gerado. Voltar um bloco ao valor original remove a sobrescrita. Os arquivos `AethraWorld_<seed>_0.sav` e `AethraWorld_<seed>_1.sav` se alternam a cada revisão. No carregamento, vale a maior revisão válida. Se o único estado persistente estiver inválido, o protótipo bloqueia novas edições para preservar os arquivos.

O uso previsto é uma instância do jogo por seed, com uma sessão local. O conjunto esparso é serializado a cada edição. Medir o custo de reconstrução, colisão e I/O definirá os limites da abordagem e a prioridade de mesclagem de faces e trabalho assíncrono.
