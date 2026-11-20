# WorldGen — arquitetura

Código: `Public/VisualWorld/AethraWorldGen.h` e `Private/VisualWorld/AethraWorldGen.cpp`, mais o ator `AAethraVisualWorld`.

## Separado do mundo voxel

`FAethraWorldGen` é um gerador novo, ao lado de `AethraTerrain` (voxel), que **não foi alterado**. Seeds e saves voxel existentes continuam com o mesmo significado. O teste 05 compara um checksum de 4.096 colunas voxel da seed 42819 (`0xdbc636bf`) e verifica que iniciar o Visual World não cria, altera nem apaga nenhum `.sav`.

O Visual World é iniciado com `-AethraVisualWorld[=seed]`; sem a opção, o jogo é o mundo voxel de sempre. A seed padrão da demo é 125, escolhida por varredura: tem rio junto ao spawn, lagos, mar a cerca de 2 km e cachoeiras.

## Etapas (puras: só dependem da seed)

1. **Relevo**
   - Ruído gradiente, fbm e ridged.
   - Planície ampla, colinas suaves e soerguimento montanhoso de 1,4 a 3,8 km da origem.
   - Mar onde o campo continental é baixo.
2. **Drenagem numa grade de 513 × 513 nós de 16 m (±4,1 km)**
   - **Preenchimento:** priority flood (ε = 0,002), que enche as depressões.
   - **Escoamento:** D8 pela maior descida e acumulação de fluxo.
   - **Limiares de acumulação:** riacho acima de 400 e rio principal acima de 2.600.
   - **Largura:** `1,2 + 0,19·√A`, de 3 a 48 m.
   - **Profundidade:** `0,3 + 0,022·√A`, de 0,4 a 4,5 m.
   - **Lagos:** depressões preenchidas com mais de 0,6 m e ao menos 10 nós (até 16 corpos d'água no total).
   - **Distância ao rio:** transformada de distância chanfrada em duas passadas.
   - **Cachoeiras:** quedas de 4 m ou mais entre nós vizinhos.
   - **Rio principal:** da foz para montante, sempre pelo maior afluente.
   - **Tempo:** cerca de 210 ms.
3. **Amostra(P)**
   - **Platôs:** de assentamento.
   - **Rios:** domain warp (16 m de amplitude, 85 m de comprimento de onda) para meandros, e escavação com leito parabólico, margem curta e íngreme, depois subida suave.
   - **Água parada:** lagos e mar.
   - **Clima:** gradiente estilizado de 4,5 °C por 100 m, umidade e províncias de rocha.
   - **Bioma:** classificado por regras.
4. **Biomas por regras, não por mosaico.** Em ordem de prioridade:
   - **Praia:** mar raso.
   - **Pico nevado:** abaixo de −1 °C.
   - **Montanha:** acima de 160 m.
   - **Margem:** rio a menos de 7 m.
   - **Área alagada:** umidade acima de 0,84, abaixo de 60 m e perto da água.
   - **Floresta:** pelo campo de floresta.
   - **Campo florido ou planície:** o resto, pela umidade.

   Medido (teste 02): 7 biomas numa janela de 4 × 4 km, e só 5,8% dos passos de 8 m mudam de bioma.

## Chunks e níveis de detalhe

| Nível | Tamanho | Passo dos vértices | Raio | Uso |
| --- | --- | --- | --- | --- |
| L0 | 128 m | 2 m | 384 m | Perto: colisão (< 260 m), grama e flores (< 230 m) |
| L1 | 256 m | 8 m | 1,6 km | Médio |
| Horizonte | 20 × 20 km | 64 m | — | Malha única, rebaixada 30 m dentro de 1.450 m |

- **Bordas:** as saias escondem as frestas entre níveis. Vizinhos do mesmo nível compartilham os vértices de borda exatamente (teste 03: 1.560 vértices comparados, nenhuma diferença).
- **Streaming:** 2 chunks a cada 0,2 s, os mais próximos primeiro. Os chunks que deixam de ser necessários só são removidos depois que todos os necessários existem, então não há buracos.
- **Regeneração:** ao voltar a uma região, o chunk regenera idêntico (teste 11).
- **Água:** cada chunk tem uma seção de água com profundidade, velocidade e espuma na cor de vértice.

## Integração e compatibilidade

- **Mundo voxel:** o modo de jogo só cria o ator `AAethraVisualWorld` no lugar do mundo voxel. Mineração, construção e saves voxel continuam intactos no modo normal.
- **Contrato compartilhado:** nenhum contrato compartilhado foi alterado (`AethraTerrain`, `AAethraVoxelWorld`, formato do save voxel).
- **Mineração e construção no Visual World:** são feitas por peças AHG e pelo corte de árvores (ver HYBRID_GEOMETRY.md e FLORA.md). Escavar o terreno contínuo fica para a próxima fase (ver ROADMAP.md).
