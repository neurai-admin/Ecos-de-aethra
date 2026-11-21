# Aethra FloraGen

Código: `Public/VisualWorld/AethraFlora.h` e `Private/VisualWorld/AethraFlora.cpp`. Atende ao adendo de vegetação e às seções 4 a 7 do Prompt Mestre 08.

## Regra

**Geometria estilizada, materiais elaborados e diversidade natural.** Nenhuma planta comum é fotorrealista ou arredondada.

## Árvores (8 espécies, 3 variantes cada)

| Grupo | Forma | Espécies |
| --- | --- | --- |
| Comuns | `Blocky`: tronco em 3 segmentos de bloco, galhos angulares (caixas a 35–55°) com tufos de cubos, copa em camadas de cubos deslocados | Carvalho-de-Elden, Bétula-Clara, Arbusto-de-Blocos, Salgueiro-do-Rio (cortinas retangulares) |
| Coníferas | `Conical`: pirâmides quadradas sobrepostas, cada camada levemente girada; silhueta triangular | Pinheiro-Escalonado |
| Outras | `Angular` | Palmeira-Angular (tronco inclinado em segmentos, folhas em lâminas dobradas) e Árvore-Seca (tronco nu com galhos angulares bifurcados, em lugares secos) |
| Especiais | `Organic`, rara | Árvore Anciã: tronco alargado, raízes, copa irregular arredondada e manchas de Musgo Luminoso |

- **Variantes:** cada espécie tem 3. As variantes mudam a proporção (mais alta e fina, ou mais baixa e larga), o número de galhos e camadas e a posição dos cubos.
- **Prova (testes 13 e 17):**
  - a copa das comuns só tem faces alinhadas aos eixos;
  - o pinheiro termina em ponta;
  - as variantes diferem;
  - as três aparecem no mundo.

## Ecologia

`FFloraEnvironment` reúne o que decide onde cada planta nasce:
- bioma, temperatura, umidade e altitude;
- inclinação;
- **exposição ao sol** (normal do terreno em relação ao sol do meio-dia);
- **fertilidade** (rocha, umidade, inclinação e rios próximos);
- **disponibilidade de água** e densidade da floresta;
- estação do ano, reservada para quando houver esse sistema.

Com isso:
- **Espécies por bioma:** a mistura de árvores sai dos pesos por bioma, dos limites de temperatura e umidade (a árvore seca só onde é seco) e de **bosques regionais**. Cada espécie é comum numa região e rara em outra; a bétula vai de 12% a 42% das árvores de floresta conforme a região (teste 15).
- **Clareiras e áreas abertas:**
  - florestas ganham clareiras (ruído de 160 m);
  - campos de flores ficam abertos, com árvores só nas bordas;
  - a planície fica ampla, para se ver longe.
- **Flora rasteira:** grade de 1,7 m (capim, samambaia nas florestas, flores esparsas nos campos, flores alpinas nas montanhas), calculada com as amostras do próprio chunk, sem chamadas extras ao gerador.

## Campos de flores

`FFloraGen::FieldAt` define regiões grandes (ruído de 130 a 280 m, em duas oitavas, com bordas irregulares) onde a ecologia combina:

| Campo | Onde | Flores |
| --- | --- | --- |
| Girassóis | Planície e campo plano, ensolarado, fértil, de 12 a 32 °C | Girassol de 1,4–2,2 m, cabeça quadrada voltada para o sol, pétalas em lâminas, folhas em caixas |
| Lavanda | Ensolarado e seco, inclusive colinas baixas; preferência por calcário e arenito | Touceira com espigas roxas |
| Rosas silvestres | Campos, margens e bordas de floresta úmidas e amenas | Arbusto de cubos com rosas em losango |
| Flores mistas | Principalmente campos floridos | Margaridas, papoulas e tulipas, em manchas de uma cor |
| Alpinas | Pastagens frias de montanha | Almofada de musgo com flores-estrela azuis |

- **Densidade:** dentro de um campo forte, de 85% a 90% das plantas são da espécie do campo (66 de 73 girassóis numa área de 15 × 15 m, teste 16).
- **Área na seed 125**, numa janela de 6 × 6 km: 124 ha de girassóis, 74 de lavanda, 212 de rosas, 222 de flores mistas e 166 de alpinas. Os campos ocupam menos de 35% do terreno; o resto fica aberto.
- **Visíveis a qualquer distância:** o campo tinge o terreno em todos os níveis (L0, L1 e horizonte), e as flores 3D aparecem perto do jogador.
- **Movimento:** as flores balançam com o vento do material, pela altura relativa e pela força de vento da espécie.

Outras espécies (lírios, hortênsias, plantas tropicais, aquáticas e espécies próprias de Aethra) entram como novas linhas da tabela `FloraMakeGround()` e novos casos em `FieldAt`.

## Não é só decoração: contratos para o futuro

`EFloraUse` marca o que cada planta pode dar: colheita, sementes, corante (com a cor), perfume, alquimia, polinização, ornamental, alimento, fibra e madeira. Exemplos:
- girassol: sementes, alimento e corante amarelo;
- lavanda: perfume e alquimia;
- rosa: perfume e corante.

Agricultura, jardinagem, comércio, corantes, perfumes e alquimia **ainda não existem**; esses dados são a interface para eles.

## Identidade, corte e LOD

- **Identidade:** cada árvore tem um id estável (célula de 7 m mais a espécie).
- **Corte:** `FellTree` persiste o corte em `Pieces_<seed>.flora`. Escavar sob uma árvore também a derruba.
- **LOD:**
  - chunks distantes mostram 40% das árvores (as mesmas), só as espécies marcadas para longe, com a variante 0;
  - distâncias de desenho e de sombra variam por espécie e por perfil gráfico;
  - grama até 75 m e flores até 110–260 m, também escaladas pelo perfil.
