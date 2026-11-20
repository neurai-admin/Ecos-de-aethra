# LIS: aprendizado

O aprendizado é limitado ao que o jogo implementa e roda localmente, sem rede neural e sem serviço externo. São três mecanismos com limites fixos, todos persistidos.

| Mecanismo | Atualização | Efeito na decisão |
| --- | --- | --- |
| Habilidade por profissão (`Skill`) | Ao praticar: `+0,03 × (1 − s)` em caso de sucesso, `+0,008 × (1 − s)` em caso de falha | Produção maior, sucesso maior na pesca, negociação e patrulha, e mais peso na utilidade da ação |
| Risco por local (`Beliefs[].Risk`) | Perigo visto: `+0,45 × (1 − r)`. Ouvido: `+0,25 × confiança × (1 − r)`. Visita segura: `× 0,97`. Perigo repelido por guarda: `× 0,85` | Penaliza ir ao local conforme cautela e coragem, e atrai patrulhas |
| Expectativa por ação (`Expectation`) | Média móvel do resultado: `e += 0,25 × (resultado − e)`, com resultado entre -1 e 1 | `+0,25 × e` na utilidade. Expectativa ruim de venda também reduz o objetivo de prosperar |

## Exemplos pedidos, verificados no teste 07

- **Caminho perigoso:** dois ataques na floresta elevam o risco aprendido, e a utilidade de cortar lenha cai mais de 0,2.
- **Prática:** dez colheitas elevam a habilidade de plantio em mais de 0,1.
- **Comerciante:** três negociações fracassadas deixam a expectativa de venda abaixo de -0,3, e vender passa a valer menos.
- **Guarda:** enfrentar uma ameaça eleva a avaliação de perigo daquele local, e patrulhá-lo passa a valer mais para um guarda corajoso.

## Mudança de objetivos por experiência

A reavaliação diária usa o que foi aprendido e lembrado. Descobertas lembradas favorecem descobrir; risco aprendido no Ermo reduz esse objetivo em quem é cauteloso; negociações ruins reduzem prosperar; escassez percebida eleva sobreviver ou apoiar a comunidade, conforme a empatia. Em Elden, com a mesma seed, houve 5 trocas de objetivo em 20 dias.

## Fora do escopo desta fase

- Aprender ações novas ou receitas.
- Imitar outros NPCs.
- Ensinar habilidades.
- Esquecer habilidades sem uso.
