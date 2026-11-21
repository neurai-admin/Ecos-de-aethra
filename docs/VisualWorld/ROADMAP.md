# Roadmap

## Feito no Protótipo 01

1. **AME:** 33 superfícies paramétricas, seleção determinística e um shader para todas.
2. **WorldGen:** relevo, drenagem real, rio principal sinuoso, lagos, mar, biomas por regras, streaming em dois níveis e horizonte de 20 km.
3. **HydroGen:** óptica por corpo d'água, 9 perfis, densidade e flutuabilidade, cachoeiras visuais.
4. **AHG:** 13 formas que podem ser colocadas, removidas e persistidas; compatíveis com voxels.
5. **CivGen-lite:** casa, torre redonda, praça e ponte planejadas pelo rio.
6. **Flora** (adendo): 7 espécies paramétricas com formas em bloco, cônicas, angulares e orgânicas raras; vento; LOD; corte persistente.
7. **Demo e medição:** seed 125, cenas A–D, dia, entardecer e noite, 14 testes novos e benchmark.

## Próximos passos (por prioridade)

1. **Construção de chunk fora da GameThread:** amostrar o terreno e a flora numa task e só criar componentes na GameThread. Isso remove os picos de cerca de 15 ms (PERFORMANCE.md).
2. **Interação no Visual World:**
   - colocar e remover peças AHG pela mira, como os blocos;
   - cortar árvores (`FellTree`) e coletar madeira;
   - escavar o terreno contínuo (camada de edições sobre o campo de altura, com o mesmo modelo de arquivo próprio).
3. **LIS no Visual World:** os NPCs pedem `FellTree` e `FAhgLayer::Add` e `Remove` pela fila validada que já existe no mundo voxel (PR #5).
4. **Cachoeiras com degrau:** o gerador escava um degrau real na queda, e o canal do riacho fica sempre visível.
5. **Culturas por região e gramática de quarteirões:** muralha, ruas e cidade fluvial em escala maior, usando as mesmas peças.
6. **Biomas futuros:** deserto, savana, tundra, selva e vulcânico, já declarados no AME, com as espécies de flora correspondentes (palmeiras no deserto e cactos angulares, por exemplo).
7. **Clima:** a chuva liga os parâmetros de umidade do AME e a agitação da água.
8. **Flutuabilidade em uso:** objetos e barcos usam `FloatFraction`.
9. **Build empacotada e benchmark nela; mais GPUs.**

## Decisões em aberto (do diretor criativo)

- O Visual World substitui o terreno voxel como mundo principal, ou os dois convivem (por exemplo, voxel em minas e cavernas, contínuo na superfície)? Esta entrega mantém os dois separados e não altera o contrato voxel. Uma mudança de contrato iria numa proposta separada.
