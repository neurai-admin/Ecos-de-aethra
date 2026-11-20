# Aethra CaveGen

Código: `Public/VisualWorld/AethraCaveGen.h` e o `.cpp` correspondente; integração em `AAethraVisualWorld` (`UpdateCaves`, `BuildCaveBlock`, `BuildCaveFeatures`, `Dig`).

As cavernas pertencem ao mundo principal. Não há outra dimensão nem portal, e não há monstros: o interesse vem de exploração, geologia, minerais e mistério.

## Como funciona

1. **Planejamento (função pura da seed):**
   - uma caverna de demonstração perto do spawn (encantada);
   - até 23 outras pelo domínio.
   - **Tipo pelo lugar:** nas montanhas, obscura ou gigante; em parte das florestas, encantada; no resto, natural.
   - **Entrada:** numa encosta seca, longe da água e das vilas, com o túnel apontado para dentro do morro.
2. **Primitivas:**
   - túneis com raio variável;
   - câmaras elipsoidais;
   - colunas naturais de rocha nas cavernas gigantes;
   - poço vertical de 28 m nas obscuras.
3. **Densidade com sinal:**
   - positiva na rocha, negativa no ar;
   - é ar acima da superfície do WorldGen e dentro das primitivas;
   - as paredes têm ruído em duas oitavas (até cerca de 1 m), para a rocha nunca ficar lisa.
4. **Região volumétrica:** em volta de cada caverna (e de cada escavação), blocos de 16 m substituem o height field.
   - **Malha:** cada bloco é gerado com Surface Nets a 1 m, com sombreamento facetado e vértices levemente puxados para a grade de 0,5 m. O resultado é rocha geométrica e angulosa, não massinha.
   - **Height field:** o terreno de 2 m deixa de desenhar os quads cobertos (com recuo de 2 m), e o volume desenha o chão ali com a mesma superfície do WorldGen.
5. **Materiais (AME):** rocha da província (granito, basalto, calcário…), terra na camada de solo, cascalho no chão e Musgo Luminoso nas encantadas. A profundidade escurece levemente o albedo.
6. **Formações:**
   - estalactites de 4 faces;
   - estalagmites de 5 faces;
   - cristais hexagonais de **Aetherita**, mineral original de Aethra que brilha.
   - **Posição:** cada formação é lançada por raios até a rocha.
   - **Encantadas:** ganham luzes de ponto teal sem sombra (baratas).
   - **Encantadas e gigantes:** ganham um lago subterrâneo na câmara mais funda.
7. **Escavação:** `Dig(centro, raio, preencher)` é uma esfera somada à densidade.
   - Remalha os blocos tocados.
   - Abre o height field se a região é nova.
   - Derruba as árvores dentro da esfera (persistidas como cortadas).
   - Marca o horizonte como sujo.
   - Grava em `Pieces_<seed>.caves` (formato `CAV1`).
   - No jogo, o mesmo botão de remover ou colocar bloco escava ou preenche quando não há mundo voxel.

## O que existe agora

| Categoria | Estado |
| --- | --- |
| Natural | Gerada (túnel, 2 câmaras, galeria) |
| Encantada | Gerada (cristais de Aetherita, musgo luminoso, luz teal, lago) — caverna de demonstração |
| Obscura | Gerada (poço profundo, câmara sem luz, galeria lateral) |
| Gigante | Gerada (câmara de 48 × 40 × 26 m, colunas, lago) |
| Glacial, inundada, vulcânica, mineral, com vegetação, ligada a ruínas | Declaradas na arquitetura; não implementadas |

Mundo da seed 125: 24 sistemas de 4 tipos e 2.222 blocos volumétricos ativos. Os blocos só são malhados perto do jogador (raio do L0 + 48 m).

## Compatibilidade

- O motor voxel não foi alterado: o checksum das colunas da seed 42819 é o mesmo (teste 05).
- As cavernas são do Visual World. Levar o CaveGen ao mundo voxel exige uma proposta separada, porque mudaria a geração e o significado das seeds.
- O teste 20 prova que a escavação persiste ao reiniciar e que outra seed não é afetada.

## Limites

- **Formações após escavação:** formações cuja rocha foi escavada são recalculadas, mas a cada escavação todas são reconstruídas (simples, não otimizado).
- **Ruído grosso:** em encostas íngremes, o ruído de 2,6 m pode abrir pequenos furos na rocha fina sobre um túnel.
- **Escavar no horizonte:** escavações não mudam o horizonte distante (ver HORIZON.md).
