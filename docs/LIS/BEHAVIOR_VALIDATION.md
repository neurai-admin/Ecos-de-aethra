# Validação de comportamento (LIS 03)

Este documento mostra o que mudou no núcleo LIS, como os habitantes percebem e reagem, como a navegação e as ações sobre o mundo foram validadas e quais testes cobrem tudo isso.

## O que mudou no LIS

O núcleo não foi recriado. Personalidade, memória, necessidades, objetivos, IA utilitária, aprendizado, relações, agenda e percepção continuam os mesmos. Foi acrescentado o seguinte (save versão 4, que ainda lê as versões 1 a 3):

| Mudança | Detalhe |
|---|---|
| Profissão Construtor | Usa o roster `ELisRoster::LivingVillage`: 6 agricultores, 4 lenhadores, 3 construtores, 3 comerciantes, 2 pescadores e 2 guardas. O roster do experimento LIS 01 continua igual |
| Ação Construir | Só para construtores. Exige lenha no depósito (pela crença do NPC, conferida ao começar) ou um dano recente lembrado |
| Ação Atender no mercado | Só para comerciantes |
| Eventos novos | Caminho bloqueado, Viu alguém trabalhando, Estrutura danificada |
| Lenha real | `Config.bLivingVillage`: o corte rende só a lenha que o mundo removeu de fato (`ReportYield`). Lenha vendida vai para o depósito (`World.WoodStore`) |
| Caminho bloqueado | `TravelFailed` registra a memória "Caminho bloqueado" com o lugar. `BlockedPenalty` reduz a nota das ações nesse lugar por até 12 horas, até o NPC chegar lá de novo. O peso vem da personalidade: cautela aumenta; coragem e ambição diminuem |
| Dano a estruturas | Diminui a confiança em quem causou e aumenta o ressentimento proporcional à lealdade. Construtores que lembram do dano recebem bônus para ir inspecionar |

As escolhas continuam vindo da IA utilitária. Nenhum roteiro fixo move os habitantes, e a variação aleatória continua limitada a ±0,02 para desempate.

## Exemplo pedido: fazenda inacessível

No teste `Aethra.LISVillage.05`, o jogador cerca e cobre o ponto da fazenda enquanto dois agricultores estão a caminho:

1. Os dois param de andar para lá: o caminho falha e o LIS replaneja.
2. Os dois guardam a memória "Caminho bloqueado em Campos" e o erro aparece no painel.
3. A reação depende da personalidade. A penalidade para voltar aos Campos ficou em **0,479** para Daron (cauteloso) e **0,118** para Iliric (ousado). Daron passa a preferir outras atividades por mais tempo; Iliric tenta de novo antes.
4. Quando um deles chega aos Campos de novo, a penalidade some. A memória também pode ser contada a outros na convivência, e quem ouve recebe a penalidade com a confiança reduzida.

## Percepção local

Os habitantes só percebem o que acontece perto deles. Não existe conhecimento global.

| Acontecimento | Quem percebe | Origem, relevância e confiança |
|---|---|---|
| Bloco quebrado ou colocado pelo jogador ou por outro NPC | Acordados a até 20 blocos (10 de altura) | Visto, autor identificado (jogador ou id do NPC) |
| Mudança em caminho | Quem estava andando por aquela coluna replaneja; se não houver alternativa, "Caminho bloqueado" | Visto, relevância 0,45 |
| Outro NPC trabalhando | Acordados a até 8 blocos; o mesmo par no máximo 2 vezes por dia, cada observador no máximo 1 vez a cada 3 horas | Visto, relevância 0,08; aumenta o respeito |
| Recurso removido | Igual a bloco quebrado; colegas de profissão não se ressentem | Visto |
| Chegada a um lugar comum | Quem chega, e quem já está a até 10 blocos (no máximo 1 vez a cada 2 horas por observador) | Visto, relevância 0,05 |
| Estrutura da vila danificada | Acordados a até 20 blocos | Visto, autor identificado |

**Boatos.** A convivência repassa memórias com a confiança reduzida pela confiança no falante. Rotinas como encontros, chegadas e trabalho visto não são repassadas.

**Limites.** A capacidade de 48 memórias por NPC continua a mesma. O teste `Aethra.LISVillage.06` confirma que tudo é salvo e restaurado de forma idêntica.

## Navegação

- **Níveis por coluna:** até 4 níveis por coluna, mais uma máscara de células livres. O A* anda entre células em pé: sobe 1, desce até 2, exige 2 blocos de altura livre (3 ao subir) e não corta quinas na diagonal. Assim os NPCs entram nas casas pela porta e não andam sobre o telhado (madeira e folhas não são chão).
- **Atualização local:** uma edição invalida só aquela coluna. Só quem passaria por ali replaneja (testes `Aethra.LISWorld.06` e `Aethra.LISVillage.04`). A malha nunca é reconstruída inteira.
- **Destino inacessível:** com caminho, o NPC desiste da viagem e o LIS replaneja; sem caminho até o ponto de trabalho, a ação é interrompida.
- **Quedas:** os NPCs nunca descem mais de 2 blocos de uma vez.
- **Folhagem:** as folhas contam como espaço livre só para a cabeça. Sem isso, NPCs ficavam presos em buracos sob as copas, um problema que o LIS 02 não tinha.

## Ações sobre o mundo

O pedido continua passando pela fila do LIS 02 e é validado fora de qualquer callback, nesta ordem:

1. NPC existe.
2. Limite de 1 alteração por hora de jogo (o NPC espera trabalhando, sem pedir de novo).
3. O bloco ainda é o esperado.
4. Alcance de até 4,5 blocos.
5. Permissão: madeira de árvore (nunca da vila) para quem corta lenha; canteiro da fazenda para quem está plantando.
6. `SetBlockBy` com o id persistente do NPC.

O teste `Aethra.LISVillage.09` confirma que um lenhador não derruba um poste da vila, que um agricultor não tira pedra da vila mas pode colher um canteiro, e que um dano do jogador é lembrado pelos vizinhos e não é desfeito.

## Testes automatizados

41 de 41 passam, executados com:

```powershell
& 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\ECOS_DE_AETHRA_LIS3\EcosDeAethra.uproject' -Unattended -NullRHI -NoSound "-ExecCmds=Automation RunTests Aethra" "-TestExit=Automation Test Queue Empty" -stdout
```

| Grupo | Testes | Resultado |
|---|---|---|
| `Aethra.LIS.01–12` (núcleo) | 12 | 12 passam |
| `Aethra.LISWorld.01–12` (LIS 02) | 12 | 12 passam |
| `Aethra.Voxel.*` | 7 | 7 passam |
| `Aethra.LISVillage.01–10` (novos) | 10 | 10 passam |

Testes novos:

1. **Criação e remoção:** 20 corpos com esqueleto; destruir o diretor remove todos.
2. **Corpo e identidade LIS:** id persistente N no corpo N; chapéu da profissão; mesma aparência depois de reiniciar; 4 profissões presentes.
3. **Animação e atividade:** 24.000 combinações de corpo e quadro sem divergência; 11 estados usados; o machado levanta a mão mais de 40 cm; dormir deita o corpo; andar mostra andar ou correr.
4. **Replanejamento:** da praça para dentro de uma casa passando pela porta; uma parede no meio do caminho faz replanejar só aquela rota e o NPC chega mesmo assim.
5. **Recurso indisponível:** a fazenda cercada, com reações diferentes por personalidade.
6. **Persistência depois de reiniciar:** checksum do LIS idêntico, vila não reconstruída, mesmos blocos, ações retomadas, corpos no mesmo lugar.
7. **Interação com estruturas:** todos os prédios e camas alcançáveis a partir da praça; construtor coletando no depósito e martelando na bancada, consumindo lenha real; quem vai dormir deita sob o telhado de casa.
8. **Salvamento de atividades:** o lenhador volta cortando a mesma árvore.
9. **Proteção de blocos:** descrita na seção acima.
10. **Desempenho com 20 NPCs:** diretor com média de 0,29 ms, animação 0,11 ms, pior quadro 1,6 ms, sem renderização (veja [PERFORMANCE.md](PERFORMANCE.md)).

Durante o desenvolvimento, os testes encontraram e levaram à correção de:

- animação atrasada 1 quadro;
- âncora da fazenda calculada antes da obra;
- escada da porta curta demais;
- chão acima do piso diante da porta;
- NPC preso em buraco sob copa;
- posição dos corpos alterando o checksum.

## Testes manuais no jogo

Feitos com a demonstração (`-AethraElden -AethraLisShots`), com capturas em `docs/LIS/captures/`:

- A vila foi construída: 11 locais, 1.509 alterações (681 blocos de árvores geradas removidos), 15 canteiros, nenhuma célula do jogador tocada.
- Cinco agricultores trabalharam em canteiros diferentes, colhendo e semeando 1 por hora, sem disputa.
- Os lenhadores cortaram troncos 1 bloco por hora. Quando um colega levou o bloco antes, o outro procurou o próximo tronco em vez de repetir o pedido.
- Os construtores buscaram material no depósito e martelaram na bancada, cada um no próprio lugar.
- Os comerciantes ficaram atrás e na frente da barraca, negociando.
- **Reação a dano:** o script de captura quebrou um bloco de parede de uma casa em nome do jogador (`FAethraInstigator::Player`). O comerciante mais próximo registrou "Estrutura danificada em Vila por jogador" (visto, relevância 0,60) e passou a ter ressentimento de 0,22 com o jogador (`lis03_05`, `lis03_06`).
- **Reabertura:** o jogo foi fechado e reaberto no mesmo mundo. O log registrou "Vila restaurada: 12 locais, 779 blocos", sem nova construção. Os habitantes voltaram ao trabalho às 10h-13h, e o comerciante manteve o ressentimento (`lis03_07`, `lis03_08`).

Ainda não verificado no jogo:

- quebrar a parede com o mouse, à mão (o script usa a mesma chamada `SetBlockBy` que o personagem usa).
