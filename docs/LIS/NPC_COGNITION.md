# LIS: cognição e decisão

## Opções consideradas

`FLisSimulation::Evaluate` lista as ações que o NPC acredita serem possíveis, com local e alvo:

| Ação | Disponível quando (pela crença do NPC) | Local |
| --- | --- | --- |
| Comer | tem comida, ou acredita que o celeiro tem | onde está, ou Vila |
| Dormir | sempre | Vila |
| Plantar/colher | espera estoque nos campos (último valor visto mais a rebrota) | Campos |
| Pescar | sempre | Rio |
| Cortar lenha | espera estoque na floresta | Floresta |
| Vender | tem lenha ou sobra de comida | Mercado |
| Comprar comida | tem moedas pelo último preço visto e acredita que o Mercado tem comida | Mercado |
| Pedir ajuda | sem comida e conhece alguém com laço acima do neutro | Vila |
| Ajudar | tem comida e vê alguém faminto no mesmo local | onde está |
| Conviver | sempre | Vila |
| Explorar | sempre | Ermo |
| Patrulhar | guarda ou coragem acima de 0,65 | local que acredita ser mais arriscado |

Um NPC não pode ajudar alguém que não vê, nem buscar frutas no Ermo sem antes descobri-las ou ouvir falar delas.

## Utilidade

`Utility` soma termos legíveis, todos testáveis:

1. **Necessidades:** para cada necessidade, `déficit^1,5 × satisfação da ação`. A curva convexa faz uma necessidade crítica dominar. Comer e dormir têm peso maior.
2. **Ofício:** ação da própria profissão vale ×1,3 + 0,12; de outra, ×0,7; mais 0,15 × habilidade.
3. **Personalidade:** multiplicadores por ação. Ajudar: 0,2 + 1,5 × empatia. Conviver: 0,4 + 0,9 × sociabilidade. Explorar: 0,3 + 0,9 × curiosidade + 0,3 × coragem. Pedir ajuda cresce com sociabilidade e cai com ambição. Comprar cresce com cautela.
4. **Situação da comunidade:** a preocupação vem do celeiro que o NPC acredita existir e soma a plantar, pescar e ajudar conforme lealdade e empatia.
5. **Relações:** pedir ajuda pesa pela confiança e afeto no alvo; ajudar pesa pela fome visível do outro.
6. **Risco aprendido:** `−2 × risco do destino × max(0, 0,3 + cautela − 0,5 × coragem)`. Para patrulhar, o risco atrai, sobretudo guardas.
7. **Objetivo:** bônus de 0,1 a 0,35 nas ações alinhadas ao objetivo atual.
8. **Experiência:** `+0,25 × expectativa aprendida da ação`.
9. **Custo de deslocamento:** `−0,025 × horas de caminhada`.
10. **Hora do dia:** à noite, dormir sobe e trabalhar desce.
11. **Variação controlada:** ±0,02 por seed, só para desempate.

`Decide` escolhe a maior pontuação, planeja o deslocamento e inicia a ação.

## Planejamento e replanejamento

O plano tem até duas etapas: viajar e executar. Na chegada, o NPC observa o local e confere as pré-condições contra o mundo real (estoque, celeiro, presença do alvo, preço). Se a crença estava errada, conta um replanejamento e decide de novo. Um perigo visto no próprio local interrompe a ação em curso. Em Elden, 3,3 % das decisões foram replanejadas.

## Objetivos

Cinco objetivos de longo prazo: sobreviver, dominar o ofício, prosperar, apoiar a comunidade e descobrir. São reavaliados diariamente a partir de personalidade, habilidade, moedas, escassez percebida, descobertas lembradas, risco aprendido e expectativa de negociação. Um novo objetivo precisa superar o atual em 0,15. Cada troca gera o evento `GoalChanged`.

## Exemplo da escassez

O teste 03 coloca cinco aldeões famintos, sem comida e com o celeiro vazio, diante das mesmas opções. Cada um escolhe por seus meios e traços:

| Aldeão | Traços | Escolha |
| --- | --- | --- |
| Comerciante | cauteloso e ambicioso | Comprar comida |
| Guarda | muito sociável, pouco ambicioso, com um amigo próximo | Pedir ajuda |
| Agricultor | leal e empático | Plantar/colher |
| Explorador | curioso e corajoso, sabe das frutas | Explorar |
| Pescador | traços médios | Pescar |

O resultado não é imposto: o teste falharia se a utilidade não produzisse essas escolhas.
