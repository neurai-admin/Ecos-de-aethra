# Motor voxel

## Coordenadas e geração

Células inteiras usam X/Y no plano e Z na vertical. O mundo aceita X/Y de −16.384 inclusive a 16.384 exclusivo e Z de 0 a 63. A divisão dos chunks arredonda para baixo nas coordenadas negativas. Cada chunk carrega até 16.384 valores de bloco em um array denso.

Ruído inteiro interpolado com parâmetros fixos cria um vale sinuoso com colinas, estratos de pedra, terra e áreas de areia. Árvores e arbustos vêm de amostragem determinística regional. Um tronco ou copa junto à divisa é calculado com as mesmas regras nos dois chunks. A camada Z=0 é protegida.

Uma segunda passagem de vegetação (Marco 02) só preenche células que a primeira deixou como ar: pinheiros em terreno mais alto, nos lotes sem árvore, e capim alto e flores sobre grama. Assim, seeds existentes e edições salvas mantêm o significado. Novos tipos de bloco são sempre acrescentados ao fim de `EAethraBlock`, porque o save grava o valor numérico.

## Malha e streaming

Faces contra blocos opacos adjacentes são omitidas. Cada face exposta tem quatro vértices, dois triângulos, normal e cor com oclusão ambiente por vértice; o quad é dividido pela diagonal mais clara. Plantas são dois quads em cruz na seção 1 da malha, sem colisão, e não escondem faces nem contam na oclusão. Na edição de borda, o chunk vizinho carregado reconstrói a malha. Chunks descarregados consultam o estado atualizado quando entram em alcance.

O mundo prepara o chunk central antes de posicionar o jogador. Depois carrega até um chunk por frame, em ordem aproximada de distância, e descarrega os que saem do raio ativo. Os presets F1, F2 e F3 têm raio de 2, 3 e 4 chunks, respectivamente. O máximo nominal por preset é 25, 49 e 81 chunks.

## Horizonte distante

`AAethraHorizon` cobre cerca de 1 km além dos chunks voxel com tiles em blocos. O passo do LOD cresce por anel de tiles: 2, 4, 8 e 16 células. Cada célula LOD usa o topo opaco da coluna gerada, então árvores aparecem como colunas. Um buraco igual ao quadrado de chunks carregados evita sobreposição. Saias descem até a rocha na borda voxel e oito blocos entre tiles para fechar frestas. O console `aethra.Horizon 0` desliga o horizonte.

## Persistência

O salvamento armazena a diferença contra o terreno gerado. Voltar um bloco ao valor original remove a sobrescrita. Os arquivos `AethraWorld_<seed>_0.sav` e `AethraWorld_<seed>_1.sav` se alternam a cada revisão. No carregamento, vale a maior revisão válida. Se o único estado persistente estiver inválido, o protótipo bloqueia novas edições para preservar os arquivos.

O uso previsto é uma instância do jogo por seed, com uma sessão local. O conjunto esparso é serializado a cada edição. Medir o custo de reconstrução, colisão e I/O definirá os limites da abordagem e a prioridade de mesclagem de faces e trabalho assíncrono.
