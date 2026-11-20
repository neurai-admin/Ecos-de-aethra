# Direção de arte — Aethra Visual World

Protótipo 01 (`claude/aethra-visual-worldgen-01`). Este documento registra a avaliação das referências, a identidade adotada e o que fica fora dela.

## Avaliação das referências (sete imagens enviadas)

As imagens são de terceiros. Elas **não foram copiadas para o repositório** e nada do jogo foi feito a partir delas: nenhuma construção, mapa, textura ou asset. Elas servem só como referência de clima, composição e escala.

| Referência | O que aproveitamos (princípio) | O que não aproveitamos |
| --- | --- | --- |
| Cidade fortificada em planície fluvial (referência principal de arquitetura) | Cidade que nasce do rio: ponte, torre redonda, muralha clara e telhados contrastantes. A silhueta da torre e da cúpula é legível de longe. | A planta da cidade, a muralha específica, as cúpulas e as cores exatas |
| Planícies com rio largo | Rio largo e sinuoso como eixo da paisagem. Vegetação densa nas margens e campo aberto entre florestas. | Um rio específico |
| Montanhas no horizonte | Cordilheira nevada como fundo permanente, com névoa de distância (perspectiva aérea) | Cadeias reconhecíveis |
| Florestas e campos floridos | Manchas de floresta com borda definida e campo florido em regiões, não em ruído | Espécies e texturas fotográficas |
| Cachoeiras | Água branca em degrau com névoa na base | Simulação de fluido (fora do escopo; ver HYDROGEN.md) |
| Vila | Escala humana: casas pequenas junto à água | Modelos de casa |

Nenhuma imagem de referência é apresentada como resultado do motor. As capturas em [captures/](captures/) são todas do jogo rodando na UE 5.8.

## Identidade

**Geometria estilizada, materiais elaborados, diversidade natural.** É uma mistura própria de Minecraft, LEGO e Roblox:

- **Geometria híbrida:** o cubo continua sendo a unidade, ao lado de cunhas, arcos, cilindros, cúpulas e telhados (AHG). O mundo voxel continua existindo e não muda.
- **Materiais detalhados:** cada superfície tem padrão procedural, variação de grande escala, desgaste e rugosidade (AME). Não usamos texturas fotográficas nem texturas geradas por IA.
- **Iluminação cinematográfica:** sol de tarde quente por padrão (16 h), névoa de altura, nuvens volumétricas, entardecer e noite reais (céu escuro, exposição ajustada).
- **Paisagem grandiosa:** planície com rio largo, montanhas a 1,4–3,8 km e horizonte de 20 km.

### Vegetação (adendo obrigatório)

Detalhes em [FLORA.md](FLORA.md).

- **Árvores comuns:** carvalho, bétula, arbusto e salgueiro, só com caixas (cubos e retângulos). O teste 13 garante que toda face delas é alinhada aos eixos.
- **Pinheiros:** pirâmides quadradas escalonadas, com silhueta triangular.
- **Palmeiras:** tronco em segmentos inclinados e folhas retangulares dobradas.
- **Formas orgânicas:** só a árvore anciã, que é rara (cerca de 1 em 300 árvores de floresta).
- **O que não fazemos:** árvores fotorrealistas e copas arredondadas em espécies comuns.

### Paleta

- **Terreno:** grama verde-média (sRGB 72,120,38) e campo um pouco mais quente.
- **Rocha:** neutra, conforme a província geológica (granito, basalto, calcário, arenito, ardósia e mármore).
- **Construções de Elden:** pedra talhada creme, reboco, telha terracota e cerâmica azul para os ricos.
- **Água:** a cor não é escolhida à mão. Ela vem da óptica do corpo d'água (ver HYDROGEN.md).

## Decisões oficiais do Prompt Mestre 08 (20/11/2026)

| Tema | Decisão | Onde está |
| --- | --- | --- |
| Identidade gráfica | Fantasia geométrica estilizada de alta qualidade | Este documento |
| Inspirações | Minecraft (mundo, mineração, árvores geométricas), LEGO (construção modular) e Roblox (criação), sem cópia direta | — |
| Realismo | Não ultrarrealista: a beleza vem de direção de arte, composição, geometria, cor, luz e distância de visão | — |
| Materiais | Naturais e detalhados, **nada de aparência de massinha**: rugosidade, variação de cor e relevo procedural em toda superfície; bloom e saturação contidos | [MATERIAL_ENGINE.md](MATERIAL_ENGINE.md), teste 22 |
| Árvores comuns | Predominantemente quadradas: tronco segmentado, galhos angulares, copa de cubos variados, 3 variantes por espécie | [FLORA.md](FLORA.md), testes 13 e 17 |
| Pinheiros | Triangulares ou cônicos estilizados | [FLORA.md](FLORA.md) |
| Árvores especiais | Livres para formas orgânicas (a Árvore Anciã, rara, com musgo luminoso) | [FLORA.md](FLORA.md) |
| Vegetação | FloraGen com ecologia (bioma, clima, umidade, solo, fertilidade, água, sol e inclinação); campos de girassóis, rosas silvestres, lavanda, flores mistas e alpinas | [FLORA.md](FLORA.md), testes 15 e 16 |
| Desempenho | Escalável, com 4 perfis (Baixo, Médio, Alto, Ultra) que mantêm o mesmo mundo | [PERFORMANCE.md](PERFORMANCE.md), teste 23 |
| RTX 3060 | Alvo para gráficos elevados e horizonte longo | [PERFORMANCE.md](PERFORMANCE.md) |
| Horizonte | Visão extrema baseada em LOD, com distâncias decididas por benchmark | [HORIZON.md](HORIZON.md) |
| **Aethra Reachable Horizon Rule** | Tudo o que o horizonte mostra existe no mundo e pode ser alcançado: o mesmo WorldGen, a mesma seed, nada inventado | [HORIZON.md](HORIZON.md), teste 18 |
| Mundo | Uma única dimensão principal nesta fase: sem Nether, The End, dimensões paralelas ou portais | — |
| Cavernas | Naturais, encantadas, obscuras e gigantes, no mesmo mundo, escaváveis | [CAVEGEN.md](CAVEGEN.md), testes 19 e 20 |
| Ruínas | Civilizações antigas na superfície e no subsolo, com lore e pontos de recompensa | [RUINGEN.md](RUINGEN.md), teste 21 |
| Mobs | Sem zumbis ou monstros hostis tradicionais nesta etapa: o interesse das cavernas vem de geologia, mistério e recursos | — |
| Fauna | Animais naturais e espécies próprias ficam para o futuro; nenhum animal nesta entrega | [ROADMAP.md](ROADMAP.md) |

## O que não é

- Não é Minecraft: os modelos, texturas, blocos e mapas são próprios, e nada é cópia.
- Não usa o BSL nem outro shader pack de terceiros.
- Não é um cenário estático para screenshot: tudo é gerado da seed, o mundo é transmitido em streaming e as peças podem ser colocadas, removidas e persistidas.
