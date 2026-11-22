# Mundo em blocos de Aethra

O jogo agora é literalmente feito de blocos de 1 m. Os geradores do Visual World alimentam o motor voxel:
rios, lagos, mar e biomas do WorldGen, solos e rochas do AME, árvores e flores do FloraGen, cavernas do
CaveGen e ruínas do RuinGen. Cavar remove um cubo; colocar põe um cubo. Antes, o Visual World era um
terreno contínuo, e cavar deixava crateras lisas.

## Rodar

Sem nenhum parâmetro, o jogo abre o mundo em blocos (seed 125 do WorldGen):

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\ECOS_DE_AETHRA_MAIN\EcosDeAethra.uproject' -game -windowed -ResX=1920 -ResY=1080
```

| Parâmetro | Mundo |
| --- | --- |
| nenhum | O último mundo jogado (na primeira vez, o mundo em blocos de Aethra, seed 125) |
| `-AethraClassic` | Mundo voxel original (seed 42819) e seus saves |
| `-AethraSeed=<n>` | Qualquer seed; com o bit 30 ligado, é um mundo em blocos |
| `-AethraElden` | Demo da vila de Elden (sem mudança) |
| `-AethraVisualWorld` | Protótipo contínuo do Visual World, mantido para comparação |

## Como funciona

- `FAethraBlockGen` (`Source/EcosDeAethra/Public/VisualWorld/AethraBlockGen.h`) transforma o WorldGen em colunas de 128 blocos.
- `AethraTerrain::SurfaceAt` e `GenerateColumn` desviam para ele quando a seed tem o bit 30 (`0x40000000`).
  - Chunks, horizonte, LIS e saves continuam iguais, pois todos já pediam colunas por seed.
  - Seeds antigas nunca têm esse bit. Mundos e saves existentes continuam idênticos.
  - O mundo em blocos salva na sua própria seed (`AethraWorld_<seed>_<slot>`).
- Alturas: o mar fica no bloco 12.
  - Até 40 m acima do mar, 1 bloco = 1 m.
  - Morros são comprimidos a ×0,28 e montanhas a ×0,045, para o relevo inteiro caber em 128 blocos.
  - Abaixo do chão, a profundidade é 1:1 (cavernas).
- Superfície: o AME escolhe o solo (grama, terra, lama, areia, cascalho, neve) e a rocha (granito, basalto, calcário, arenito, ardósia, mármore).
- Água: rios, lagos e mar viram blocos de água.
  - Leito de cascalho nos rios, lama nos lagos e areia no mar.
  - Nenhuma planta cresce dentro da água.
- Árvores: as espécies do FloraGen viram formas de bloco: carvalho, bétula, pinheiro, arbusto, salgueiro, palmeira, árvore morta e árvore ancestral.
- Plantas: girassol, lavanda, rosas, flores brancas, azuis e amarelas, samambaia e capim, distribuídos pelos campos de flores do FloraGen.
- Cavernas: a densidade do CaveGen é amostrada por bloco. Há lagos de caverna, estalactites de calcário e cristais de Aetherite.
- Ruínas: as peças do RuinGen são rasterizadas em blocos de tijolo de pedra, mármore, tábuas, telhas e cerâmica.
- Spawn: uma coluna seca e plana perto da origem, à vista do rio, sem árvore em cima e virada para a água.

## Rio corrigido

O nível de água da drenagem (`RiverLevel`) podia ficar metros acima do terreno local. No spawn da seed
125, a água estava a 38,7 m e as margens a 33 m. O resultado era uma "parede" de água sobre as margens no
mundo em blocos e um rio que não parecia rio no protótipo contínuo.

A correção fica em `FAethraWorldGen::Sample`: a superfície do rio nunca passa de 0,8 m abaixo do chão
local antes do entalhe. Agora o rio corre dentro do próprio leito. Os testes de rio do Visual World
(continuidade, água doce e salgada, profundidade) continuam passando.

## Rio contínuo e blocos sem borda

- No mundo em blocos, a água não é desenhada em cubos. Cada chunk recebe uma superfície contínua no nível real da água (`FAethraBlockGen::BuildWaterSurface`).
  - A margem é suave: a opacidade cai com a distância até a água.
  - O fluxo vai na cor do vértice: RG é a direção e B a velocidade. As ondas e correntezas andam rio abaixo.
  - Os blocos de água continuam nos dados (cavar, colocar, LIS); só não são desenhados.
- As margens são de grama e sobem um bloco a cada 1,5 m. Até 3 m da água, o chão fica logo abaixo da superfície, então a água encontra a margem sem degrau.
- `FWorldSample::ShoreLevel` dá o nível da água próxima também em terra seca. É um campo novo; nada que já existia mudou de valor.
- Os blocos não têm mais chanfro nem variação de cor por bloco. A granulação do material segue as coordenadas do mundo e atravessa as bordas, então chão vizinho lê como uma superfície só. Tijolos, tábuas e troncos mantêm o desenho por bloco.

## Base do jogo

- **Inventário:** cinto de 8 espaços, com pilhas de até 99.
  - Cavar coleta o bloco; plantas e água não dão nada.
  - Colocar gasta um bloco do espaço escolhido; com o espaço vazio, nada é colocado.
  - 1–8 ou a roda do mouse escolhem o espaço.
  - Cada mundo salva o seu inventário (`Saved/SaveGames/AethraInventory_<seed>.inv`).
- **Nado:** com o corpo na água, o personagem nada.
  - Espaço sobe; parado, afunda devagar; olhar para baixo e andar mergulha.
  - Segurar espaço ao chegar na margem dá o impulso para sair.
- **Menu de mundos:** aparece ao abrir o jogo e com Esc.
  - Opções: Continuar, mundos salvos (os 6 mais recentes), Novo mundo de Aethra (semente aleatória), Mundo clássico e Sair.
  - O jogo lembra o último mundo jogado (`Saved/AethraLastWorld.txt`).
  - `-AethraNoMenu` pula o menu. O benchmark e as capturas automáticas também pulam.
- **Interface:** fontes entalhadas próprias, cinto com cantos cortados e moldura dourada, menu em painéis entalhados. Nada da interface vem do Minecraft.

## Arte própria (nada do Minecraft)

- **Materiais pintados:** as texturas não têm mais grade de 16×16 pontos. Todo padrão é ruído contínuo nas coordenadas do mundo: pinceladas largas mais grão fino. Tijolos, tábuas e telhas têm juntas suaves.
- **Lateral da grama:** no lugar da franja em pixel, um lábio de musgo suave e ondulado sobre o solo.
- **Plantas:** nada de planos em X.
  - A grama e a samambaia são tufos de lâminas afiladas, inclinadas em direções aleatórias.
  - As flores têm haste, folhas e uma corola de pétalas inclinada.
  - O girassol tem uma corola grande; a lavanda, hastes com espigas.
- **Folhagem:** os blocos de folha continuam no mundo, mas só como colisão invisível; cavar e subir funcionam como antes.
  - O que se vê são maços de folhagem: três cartões lobados por bloco exposto, em ângulos aleatórios e maiores que o bloco, então a copa fica redonda.
  - As copas do mundo em blocos são domos e discos, não caixas.
  - No horizonte, as copas só tingem o chão de verde-escuro, sem caixas flutuando.
- O mundo clássico usa os mesmos materiais, plantas e folhagem, mas o horizonte e as árvores dele não mudaram (os saves continuam idênticos).

## Blocos novos

Os ids são anexados depois de `BirchLeaves`, porque saves guardam os valores numéricos. Eles param em 31,
porque o material lê o id em alfa / 31:

`Gravel, Mud, Basalt, Limestone, Sandstone, Slate, Marble, StoneBricks, Planks, RoofTiles, Aetherite, Sunflower, Lavender, FlowerWhite, FlowerBlue, Fern, Ceramic`

Padrões e cores ficam em `Tools/Editor/create_voxel_material.py` e `AethraBlockColor.h`. Rode o script
de novo após mudar materiais.

## Medições

- Benchmark: RTX 3060, 1080p, rota fixa de 60 s, `bench.ps1 -Name blockworld-60s`.
- Mundo em blocos com LIS: média de 142 FPS e 1% low de 71 FPS. A referência do mundo voxel original era 137 FPS e 73 FPS.
- Geração de chunk: média de 2,4 ms e p95 de 5,9 ms.
- Construir o gerador leva cerca de 1 s na primeira coluna (WorldGen, cavernas, ruínas).

## Limites conhecidos

- O Aetherite ainda não emite luz; ele é só um bloco claro e liso.
- A lavanda e o girassol usam o desenho de flor genérico (cruz), com a cor própria.
- O cache de colunas é plano e limpa a si mesmo ao passar de 300 mil colunas.
- O horizonte distante mostra copas como blocos soltos, porque é a camada de topo de cada amostra.
