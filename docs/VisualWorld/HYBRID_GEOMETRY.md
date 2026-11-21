# AHG — Aethra Hybrid Geometry

Código: `Public/VisualWorld/AethraHybridGeometry.h` e o `.cpp` correspondente.

## Representação

Uma peça tem: forma, superfície AME, posição (centro da base), yaw e tamanho em metros (x, y, z).

- **Formas:** cada uma é uma malha paramétrica construída em C++ em tamanho unitário (x e y em ±0,5 m, z de 0 a 1 m) e escalada por instância. Assim, uma malha serve para qualquer tamanho.
- **Desenho:** um `UInstancedStaticMeshComponent` por forma, com a superfície em dados por instância. Todas as formas usam o mesmo material.

## Formas (13)

| Forma | Triângulos (unitária) | Uso |
| --- | --- | --- |
| Block, HalfBlock | 12 | Muros, bases, degraus |
| Wedge, Ramp | 8 / 8 | Contrafortes, rampas de ponte |
| Stairs | 4 degraus | Escadas |
| Cylinder, Column | 24 lados | Torre, colunas |
| Sphere, Dome | 8 anéis | Remates, cúpula |
| Arch | Perfil em arco | Portas, vãos de ponte |
| CurvedWall | Quarto de anel | Muro curvo da praça |
| ConeRoof, GableRoof | — | Coruchéu, telhado de duas águas |

As novas formas entram no fim da lista, porque o id é gravado.

## Colocar, remover, persistir

`FAhgLayer`:
- **`Add(Peça, MundoVoxel)`:** com um mundo voxel, cada célula de 1 m que a peça cobre precisa estar livre (ar ou planta) e não ter outra peça; as células passam a ser exclusivas, como blocos. Sem mundo voxel (no Visual World), as peças podem se sobrepor para compor um edifício.
- **`Remove(Id)`:** libera as células da peça.
- **Persistência:** arquivo próprio `AHG1`, versão 1, gravado de forma atômica (`.tmp` seguido de troca). O formato do save voxel não muda. Os ids não são reaproveitados depois de recarregar.
- **Arquivo danificado:** a carga é recusada e a camada fica como estava (teste 04).

Compatibilidade com voxels (teste 10):
- as peças nunca escrevem blocos;
- uma peça num bloco sólido é recusada;
- depois de minerar o bloco, a peça cabe ali;
- a camada informa quais células estão ocupadas, para o jogo recusar um bloco colocado ali.

## CivGen-lite: assentamento da demo (43 peças)

Ele é planejado na margem do rio principal, onde o gerador cria um platô (raio de 20 m) e um segundo platô do outro lado da ponte. As superfícies vêm de `FAmeSelector::Building` com a cultura EldenStone:

- **Casa:** embasamento, paredes, porta em arco, cornija, telhado de duas águas, chaminé e escada.
- **Torre redonda:** cilindro, frisos, cúpula de cerâmica, coruchéu, esfera de remate, contrafortes em cunha e janelas em arco.
- **Praça:** colunas, remates, muros curvos e banco.
- **Ponte:** arcos, tabuleiro, parapeitos e rampas nas duas margens.

O assentamento é gravado na primeira execução em `Saved/VisualWorld/Pieces_<seed>.ahg` e, nas seguintes, é carregado, não reconstruído. As peças do jogador entram no mesmo arquivo.

## Limites

- **Colisão:** é a caixa simples de cada forma (`UseSimpleAsComplex`). Escadas e arcos colidem como caixas.
- **Interface:** ainda não há interface de jogo para colocar e remover peças no Visual World. A API e os testes existem, e a interface fica para a próxima fase.
