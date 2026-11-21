# Animações dos habitantes (LIS 03)

Este documento explica como os habitantes são animados, quais animações existem, como elas se ligam às ações do LIS e o que ainda falta.

## Abordagem

A animação é esquelética e procedural, sem arquivos de animação. O corpo usa `UPoseableMeshComponent` com o esqueleto de 18 ossos do `SK_Villager`. Cada estado é uma função do tempo que devolve uma rotação por osso, nos eixos do personagem (frente, esquerda, cima). `AAethraLisNpc::ApplyPose` converte essas rotações para o espaço de cada osso, partindo da pose de referência. Os eixos são lidos do próprio esqueleto ao iniciar: a esquerda vem dos ombros e a frente do produto vetorial. Por isso o resultado não depende de como o FBX foi orientado.

Motivos para essa escolha:

- Não depende de AnimBlueprint nem de assets binários de animação.
- Cada pose pode usar a fase do passo, a distância percorrida e a variação de cada habitante.
- O custo medido é de cerca de 0,11 ms por quadro para os 20 corpos (teste `Aethra.LISVillage.10`).

## Estados

| Animação | Quando (ação do LIS e corpo) | Descrição |
|---|---|---|
| Parado | Ocioso; aguardando caminho; patrulha ou exploração parados | Respiração, peso alternando entre as pernas, olhar devagar ao redor (fora de sincronia entre habitantes) |
| Andando | navegação `andando` | Pernas ±26°, joelho dobrando na volta da perna, braços opostos, quique de 2 cm, giro de tronco; ciclo a cada 1,3 m percorrido |
| Correndo | navegação `andando` com caminho de mais de 40 células ou Segurança acima de 0,6 | Pernas ±44°, joelho até 75°, cotovelos dobrados, tronco inclinado; ciclo a cada 2,2 m |
| Machado | Cortar lenha, no ponto da árvore | Levanta acima da cabeça, golpeia e recupera, em 1,25 s; o tronco acompanha |
| Enxada | Plantar/colher, no canteiro | Golpe baixo em 1,6 s com o corpo curvado |
| Coletando | colhendo ou semeando um canteiro; construtor no depósito | Agacha e estende os braços, em 2,4 s |
| Martelo | Construir, na bancada | Golpes rápidos com o braço direito em 0,7 s, olhando a peça |
| Inspecionando | Construir sem madeira, com estrutura danificada na memória | Inclina-se, mão no queixo, olha em volta |
| Conversando | Conviver, Ajudar, Pedir ajuda | Gestos com as mãos, aceno de cabeça, de frente para o parceiro |
| Negociando | Atender no mercado, Vender, Comprar comida | Braços à frente apresentando, olhar alternado |
| Sentado | Ocioso no lugar de descanso | Sentado no chão, pernas à frente, mãos nos joelhos, de frente para a fogueira |
| Comendo | Comer | Sentado, levando a mão à boca |
| Dormindo | Dormir, já dentro de casa | Corpo deitado, respiração lenta |
| Pescando | Pescar, na margem | Braços à frente, puxão ocasional (sem vara) |

A escolha fica em `AAethraLisDirector::AnimFor(FLisNpc, Corpo)` e a ferramenta em `ToolFor`. As duas leem só o estado do LIS (ação atual) e o do corpo (andando, fase da atividade). O corpo nunca escolhe uma animação por conta própria. O teste `Aethra.LISVillage.03` confere 24.000 combinações de corpo e quadro, e em nenhuma a animação mostrada diverge do que o estado do LIS pede.

## Transições e giros

- **Troca de estado:** mistura de 0,3 s entre a pose anterior e a nova, com interpolação esférica por osso e curva suave. A pose anterior continua avançando no tempo durante a mistura, o que evita congelar no meio.
- **Virar:** o habitante gira até 540°/s em direção ao destino ou ao alvo do trabalho. Ao andar, se precisa virar mais de 70°, reduz a velocidade a 35% até se alinhar, em vez de deslizar de lado.
- **Girar parado:** quando gira sem andar, os pés fazem um pequeno passo no lugar.
- **Deitar e levantar:** dormir deita o corpo inteiro (rotação da malha); qualquer outra ação o levanta.
- **Ordem no quadro:** a animação é escolhida depois de tratada a chegada ao destino. Assim, no quadro em que o LIS troca "Viajar" pela ação, o corpo já mostra a ação. Esse atraso de um quadro foi encontrado e corrigido pelo teste 03.

## Testes que verificam

- **`Aethra.LISVillage.03`:** animação igual ao estado do LIS ao longo de 40 s de vida da vila. O corte de lenha levanta a mão direita mais de 40 cm de verdade (posição do osso medida). Dormir deita o corpo e outra ação o levanta. Quem anda usa andar ou correr.
- **`Aethra.LISVillage.07`:** construtor coletando no depósito e martelando na bancada, com o martelo na mão.
- **`Aethra.LISVillage.08`:** depois de reiniciar, o lenhador volta a usar o machado.

## Limitações

- **Pés:** não há IK. Em degraus os pés podem afundar ou flutuar alguns centímetros; o corpo sobe o degrau num pequeno salto.
- **Assento:** sentar é sempre no chão, porque um banco voxel tem 1 m de altura, alto demais para a escala do corpo.
- **Acessórios faltando:** pescar não tem vara; colher e semear não mostram o item na mão.
- **Correr:** não há variação por cansaço nem por carga.
- **Personalidade:** não há animações diferentes por personalidade, só a variação de fase e do olhar.
