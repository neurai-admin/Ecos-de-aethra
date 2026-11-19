# Decisões

| Decisão | Motivo e consequência |
| --- | --- |
| Módulo Runtime único no protótipo | As classes separam geração, apresentação e armazenamento com menos configuração de build. O futuro LIS pode usar um módulo próprio. |
| Cena montada pelo modo de jogo | O projeto começa com código e configuração reproduzíveis. Um mapa autoral com direção de arte pode assumir a entrada em etapa posterior. |
| Malha procedural de faces visíveis | Um componente por chunk, reconstrução local e colisão simples de inspecionar. Greedy Meshing pode reduzir triângulos em superfícies planas, mas precisa preservar materiais, colisão e normal. |
| Construção síncrona inicial | A geometria e a colisão são aplicadas juntas. O custo de geração por frame e a viabilidade de cozinha assíncrona serão avaliados com perfil de CPU. |
| Duas revisões de salvamento | O slot anterior fica disponível durante a próxima gravação. O volume crescente de edições deve orientar futuras partições do save. |
| Renderização inicial sem Lumen e Nanite | A cena usa luz direta, céu e malhas mutáveis para a primeira medição no PC-alvo. |
| Materiais procedurais sem texturas (Marco 02) | Padrão de 16 × 16 texels, bisel e plantas são HLSL em materiais criados por script Python do editor. São reproduzíveis por qualquer agente e não exigem assets binários de textura. |
| Horizonte só pela seed (Marco 02) | Geração pura e assíncrona, sem acesso ao save. Edições distantes não aparecem no horizonte; ver `shortcut:` em `AethraHorizon.cpp`. |
| Iluminação dinâmica sem Lumen (Marco 02) | Malha procedural não gera distance fields. Céu atmosférico, skylight em tempo real, névoa volumétrica, nuvens volumétricas e pós-processo custam cerca de 4 ms de GPU a 1080p na RTX 3060. |
| Colisão assíncrona (Marco 02, revê decisão do Marco 01) | O perfil mostrou ~7 ms por chunk carregado e até ~60 ms por edição com cozimento síncrono. Só o chunk de spawn cozinha de forma síncrona; nas edições a colisão antiga vale por alguns frames. |
| Altura 128 e montanhas fora do vale (Marco 02) | O índice denso não depende da altura, então saves continuam válidos. Montanhas só depois de 256 células preservam a área explorada. Builds antigas tratam saves com Z ≥ 64 como inválidos e bloqueiam edição. |
| Folhas não opacas (Marco 02) | Folhas recortadas exigem desenhar faces entre folhas; troncos dentro da copa passam a ter faces visíveis. Custo medido dentro da meta. |
| Vegetação aditiva (Marco 02) | A nova passagem só ocupa células de ar. Mudar blocos já gerados alteraria mundos e edições existentes. |
| Blueprint pelo contrato do mundo | Permite prototipar interação com NPCs em um mapa, mantendo malha e formato de save como detalhe privado. |
