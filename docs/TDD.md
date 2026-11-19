# Projeto técnico

| Área | Implementação do protótipo |
| --- | --- |
| Engine | Unreal Engine 5.8, C++ com interfaces expostas para Blueprints |
| Cena | Mapa Entry da engine e montagem em execução no modo de jogo |
| Renderização | Malha procedural, cor por vértice, sol, luz ambiente e céu atmosférico |
| Mundo | Geração determinística de colunas, chunks 16 × 16 × 64, bloco de 1 m |
| Colisão | Malha complexa por chunk, atualização síncrona após edição |
| Entrada | Teclado e mouse pelo sistema nativo de ações e eixos |
| Salvamento | Sobrescritas esparsas, dois slots alternados por seed, formato versionado |
| NPCs | Consulta e evento de mudança de célula para integração futura |
| Alvo inicial | Windows x64, 1080p, 60 FPS como meta a medir |

O projeto tem alvos separados de editor e jogo. O plugin Procedural Mesh Component acompanha a engine. Um único módulo Runtime contém fronteiras entre geração, mundo, malha e interação, o que mantém a configuração de build simples para esta fase.

Projetos de Blueprint podem estender o modo de jogo e observar mudanças do mundo. Um mapa próprio pode conter uma instância de `AAethraVoxelWorld` com seed configurada antes de iniciar a geração. A demo posiciona o jogador na coluna central do mundo.
