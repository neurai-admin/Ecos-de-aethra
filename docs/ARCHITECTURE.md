# Arquitetura

| Responsabilidade | Componente |
| --- | --- |
| Geração determinística e conversão de coordenadas | `AethraTerrain` |
| Ciclo de vida dos chunks, edição e salvamento | `AAethraVoxelWorld` |
| Dados densos, geometria e colisão do chunk | `AAethraVoxelChunk` |
| Serialização das revisões | `UAethraWorldSave` |
| Inicialização e ponto de entrada da cena | `AAethraGameMode` |
| Controles, câmera e interação | `AAethraCharacter` |
| Feedback imediato | `AAethraHUD` |

O gerador calcula blocos a partir de seed e coordenadas globais. O mundo aplica as edições persistentes ao gerar cada chunk. Para construir uma malha, o chunk consulta uma coluna de borda por linha em cada vizinho e usa as mesmas regras do terreno local. As consultas também funcionam para células de chunks descarregados.

Uma edição valida a coordenada, grava a revisão, atualiza a malha e comunica o evento. A edição é revertida em memória se a gravação falhar. Cada chunk tem um componente de malha e colisão.

O protótipo executa essas operações na thread de jogo. O futuro módulo LIS pode consumir o contrato público sem depender das classes privadas de malha ou do formato de salvamento de terreno. A divisão de trabalho e o contrato estão em [LIS_INTEGRATION.md](LIS_INTEGRATION.md).
