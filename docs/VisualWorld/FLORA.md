# Flora — sistema procedural de espécies

Código: `Public/VisualWorld/AethraFlora.h` e `Private/VisualWorld/AethraFlora.cpp`. Atende ao **adendo obrigatório de identidade geométrica da vegetação**.

## Regra

**Geometria estilizada, materiais elaborados e diversidade natural.**

| Grupo | Forma | Espécies |
| --- | --- | --- |
| Árvores comuns | `Blocky`: só caixas (cubos e retângulos), copas em camadas deslocadas e galhos quadrados | Carvalho-de-Elden, Bétula-Clara, Arbusto-de-Blocos, Salgueiro-do-Rio (cortinas retangulares penduradas) |
| Coníferas | `Conical`: pirâmides quadradas sobrepostas que afinam até a ponta (silhueta triangular) | Pinheiro-Escalonado |
| Outras espécies | `Angular`: forma própria | Palmeira-Angular (tronco em segmentos inclinados, folhas em lâminas retangulares dobradas) |
| Raras, antigas ou especiais | `Organic`: curvas, irregular, mais complexa | Árvore Anciã (tronco alargado, raízes, copa de esferas irregulares) |

Minecraft é só referência de linguagem visual. Nenhum modelo ou textura foi copiado. As árvores comuns não são fotorrealistas nem arredondadas. O teste 13 confirma três pontos:
- todas as faces das espécies `Blocky` são alinhadas aos eixos;
- o pinheiro termina em ponta;
- só a espécie rara é orgânica.

## Parâmetros por espécie (`FFloraSpecies`)

- **Geometria:** forma, altura e largura do tronco, largura e altura da copa, número de camadas, folhas ou módulos.
- **Materiais:** casca e folhas são superfícies AME (padrões Bark e Leaves, com variação e tonalidade por instância).
- **Tamanho:** a escala de cada árvore vai de mínimo a máximo, fixada pelo hash da célula.
- **Vento:** de 0 a 1. O shader desloca os vértices pelo quadrado da altura relativa (UV0.y), com fase pela tonalidade da instância. O tronco fica firme e a copa balança.
- **Bioma e clima:** peso por bioma, mais temperatura mínima e máxima.
  - Palmeira: só em praias.
  - Salgueiro: margens e áreas alagadas.
  - Pinheiro: montanhas e frio.
  - Carvalho: só acima de 4 °C.
  - Árvore Anciã: cerca de 1 em 300 árvores de floresta.
- **LOD:** tem duas partes.
  - Distância de desenho e de sombra por espécie (o arbusto some aos 450 m; a anciã fica visível até 4 km).
  - Nos chunks distantes (L1), fica um subconjunto estável de 40% das árvores, só das espécies marcadas `bFar`.

## Posição, identidade e persistência

- **Grade:** uma árvore no máximo por célula de 7 m. `FFloraPlacer::TreeAt(Gerador, Célula)` é função pura da seed e da célula.
- **Exclusões:** água, distância à margem, platôs de assentamento, ponte e encostas íngremes.
- **Identidade:** cada árvore tem um id estável (célula de 28 + 28 bits com sinal, mais a espécie). É o mesmo id em L0, em L1 e em qualquer sessão.
- **Chunks:** cada célula pertence a exatamente um chunk em cada nível. A grade antiga deixava colunas de células sem árvore entre chunks; isso foi corrigido.
- **Corte:** `AAethraVisualWorld::FellTree(Id)` marca a árvore como cortada em `FFloraLayer`. O arquivo próprio `FLR1` fica ao lado das peças (`Pieces_<seed>.flora`), e o chunk que a contém é reconstruído na hora. Depois de reiniciar, a árvore continua cortada; outra seed não é afetada (teste 14).

Esse é o gancho para a mineração (cortar madeira) e para as ações dos NPCs: o LIS pode pedir `FellTree` pela mesma fila validada que já usa para blocos voxel. Ligar o LIS ao Visual World fica para a próxima fase; nesta branch o LIS não é dependência.

## Números (seed 125)

- **Densidade:** cerca de 3.990 árvores de 6 espécies numa área de 840 × 840 m (teste 13).
- **Carga inicial:** 11.262 árvores e 41.781 tufos de grama e flores ao redor do spawn (teste 12).
