# Decisões

| Decisão | Motivo e consequência |
| --- | --- |
| Módulo Runtime único no protótipo | As classes separam geração, apresentação e armazenamento com menos configuração de build. O futuro LIS pode usar um módulo próprio. |
| Cena montada pelo modo de jogo | O projeto começa com código e configuração reproduzíveis. Um mapa autoral com direção de arte pode assumir a entrada em etapa posterior. |
| Malha procedural de faces visíveis | Um componente por chunk, reconstrução local e colisão simples de inspecionar. Greedy Meshing pode reduzir triângulos em superfícies planas, mas precisa preservar materiais, colisão e normal. |
| Construção síncrona inicial | A geometria e a colisão são aplicadas juntas. O custo de geração por frame e a viabilidade de cozinha assíncrona serão avaliados com perfil de CPU. |
| Duas revisões de salvamento | O slot anterior fica disponível durante a próxima gravação. O volume crescente de edições deve orientar futuras partições do save. |
| Renderização inicial sem Lumen e Nanite | A cena usa luz direta, céu e malhas mutáveis para a primeira medição no PC-alvo. |
| Blueprint pelo contrato do mundo | Permite prototipar interação com NPCs em um mapa, mantendo malha e formato de save como detalhe privado. |
