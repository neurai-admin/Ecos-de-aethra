# Integração do Living Intelligence System

O LIS poderá manter identidade, necessidades, objetivos, memórias, relações, reputação e facções. A evolução de sociedades, diplomacia e antagonistas emergentes depende da simulação de decisões e interações. Esta fase fornece uma entrada para percepção de mudanças de terreno.

| Contrato | Uso |
| --- | --- |
| `EAethraBlock AAethraVoxelWorld::GetBlock(FIntVector Cell) const` | Consulta por coordenada global, inclusive em chunks descarregados. Fora dos limites, retorna `Air`. |
| `bool AAethraVoxelWorld::SetBlock(FIntVector Cell, EAethraBlock Block)` | Solicita edição no mundo. Retorna sucesso após confirmar a persistência e atualizar a geometria carregada. |
| `OnBlockChanged` | Evento tipado com a célula e os valores anterior e novo. Ocorre uma vez por alteração aceita. |
| `Seed` | Identificador de geração do mundo de demonstração. Definido antes da inicialização. |

A consulta ao estado é autoritativa para simulação local. O evento ocorre na thread de jogo e representa a alteração de uma célula. Um assinante pode acumular observações relevantes por proximidade, como a remoção de folhagem, e processá-las posteriormente. O evento não fornece percepção individual, identidade do autor, histórico ou transmissão de rede. Um listener que precise editar o mundo deve agendar a edição para após o callback.

A equipe GPT/Codex mantém o módulo Runtime e os contratos do terreno. A futura equipe do LIS pode trabalhar em módulo próprio, evitando editar os mesmos arquivos no mesmo ciclo. Mudanças de interface podem seguir em PR separado e ser integradas antes dos consumidores.
