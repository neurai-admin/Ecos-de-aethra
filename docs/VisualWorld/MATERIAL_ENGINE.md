# AME — Aethra Material Engine

Código: `Source/EcosDeAethra/Public/VisualWorld/AethraMaterialEngine.h` e o `.cpp` correspondente. Shader: `M_AethraSurface`, criado por `Tools/Editor/create_visualworld_materials.py`.

## Princípio

Uma superfície é **dados**, não textura:
- cor base e cor de acento (linear);
- variação de grande escala;
- rugosidade e metalicidade;
- um padrão procedural e a escala dele;
- desgaste.

Um único material lê essas grandezas de uma paleta de 64 × 4 texels float, criada em runtime, e gera o pixel. Não há arquivos de textura nem geração de textura por IA, nem por chunk nem em nenhum outro lugar.

## Biblioteca (33 superfícies no Protótipo 01; 43 após o Prompt 08)

| Categoria | Superfícies |
| --- | --- |
| Terreno | Grass, Meadow, Dirt, Mud, Sand, Gravel, Snow |
| Rocha | Granite, Basalt, Limestone, Sandstone, Marble, Slate |
| Vegetação | Moss, BarkOak, BarkBirch, BarkPine, LeavesBroad, LeavesNeedle, LeavesBirch, Reed |
| Construção | WoodPlank, CutStone, Brick, Mortar, RoofTile, Ceramic, Plaster, Iron, Bronze |
| Recurso | OreCopper, OreIron, Crystal |

Os 19 padrões do shader são:
- Noise, Grass, Meadow (com flores pontuais), Sand e Gravel;
- Strata (faixas onduladas), Speckle, Columns (basalto), Snow e Moss;
- Bark (estrias verticais) e Leaves;
- Planks, CutStone e Brick (com juntas), Tiles (escamas);
- Plaster, Metal e Ore (veios).

Os ids são gravados em dados de vértice e em saves. Novas superfícies entram sempre no fim da lista.

## Seleção

`FAmeSelector::Terrain(Ambiente, Seed, Célula)` é determinística e não guarda estado. A seed e a célula só posicionam manchas de cerca de 8 células; quem decide a superfície é o ambiente:
1. **Inclinação:** acima de 0,38 (ou 0,28 em montanha), mistura com a rocha da província local.
2. **Frio:** a neve aparece conforme a temperatura e só em inclinações suaves.
3. **Solo do bioma:** campo, grama e musgo, cascalho de montanha, lama de área alagada, areia de praia.
4. **Beira d'água:** a menos de 3 m da água, o solo é areia, cascalho ou lama, seja qual for o bioma.
5. **Clima e desgaste:** chuva e desgaste não trocam a superfície; só escurecem ou clareiam no shader.

`FAmeSelector::Building(Cultura, Parte, Rocha, Riqueza)` escolhe a superfície de cada parte de uma construção. Há três culturas:
- **EldenStone:** pedra talhada, reboco, telha e cerâmica.
- **RiverTimber:** tábua, reboco e junco.
- **HighlandSlate:** rocha local, ardósia, ferro e bronze.

## No shader

- **Terreno:** o albedo e a rugosidade são resolvidos em C++ por vértice (cor de vértice e flag em UV2). O padrão só modula a luminância. Interpolar ids entre vértices gerava faixas coloridas; dessa forma, uma transição muda o detalhe, nunca a cor.
- **Peças e árvores:** o id vem do UV1 ou dos dados por instância (`CustomData 1` = id + 1). `CustomData 0` é a tonalidade por instância e `CustomData 2` o vento das árvores.
- **Correção desta entrega:** malhas estáticas com só 2 canais UV liam o UV1 como flag de terreno e ficavam brancas. Agora toda malha AHG ou de flora tem 3 canais.

## Prompt 08: nada de massinha

- **Relevo procedural em toda superfície:** o shader calcula uma altura a partir do próprio padrão (juntas e cavidades afundam, o acento sobe, mais um grão fino) e gera a normal por derivadas de tela (bump por gradiente de superfície).
  - Não há normal maps nem texturas novas.
  - A profundidade vai na paleta (linha 3, verde), por padrão: alvenaria e telhas 2,2 cm; rocha 1,8; casca e tábuas 1,4; cascalho, musgo e folhas 1,1; grama e solo 0,7; neve, reboco e metal 0,3. Nenhuma superfície é perfeitamente lisa.
- **Brilho só onde é mágico:** canal `Glow` (linha 3, azul) para emissão. Só a Aetherita e o Musgo Luminoso brilham.
- **Pós-processo contido no Visual World:** bloom 0,3 e saturação 0,95.
- **Grama:** as lâminas são iluminadas com a normal do chão, então não aparecem pretas contra a luz e combinam com o terreno onde terminam.
- **Manchas de solo:** as manchas de solo alternativo usam ruído suave. O hash por bloco de 8 × 8 desenhava quadrados, que o relevo novo deixava evidentes.
- **Teste 22:**
  - superfícies naturais (terreno, rocha, vegetação) têm rugosidade de pelo menos 0,34 e nunca são metálicas;
  - toda superfície tem relevo e variação de cor;
  - só as encantadas brilham;
  - a flora usa só superfícies AME.

### Superfícies novas (fim da lista, ids preservados): 43 no total

PetalSunflower, SunflowerDisk, PetalRose, PetalLavender, PetalDaisy, PetalPoppy, PetalAlpine, Fern, Aetherite (mineral original de Aethra, brilho 2,5), GlowMoss (brilho 0,8).

Pétalas são foscas (rugosidade de 0,62 a 0,75), com veios do padrão Leaves.

## Limites

- **Relevo sem parallax:** o relevo é de normal, sem deslocamento real nem oclusão de parallax.
- **Clima:** os parâmetros de chuva existem, mas ainda não há sistema de clima ligado a eles.
- **Biomas futuros:** Deserto, Savana, Tundra, Selva, Vulcânico e Caverna já estão declarados, com regras de solo, mas o gerador ainda não os produz.
