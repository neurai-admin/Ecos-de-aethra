# LIS: relações sociais

## Estrutura

Cada NPC tem `TMap<int32, FLisRelation> Relations`: a visão dele sobre outro ator. A→B é independente de B→A. A chave -1 representa o jogador. A relação é criada no primeiro contato que a altera.

| Valor | Início | Faixa |
| --- | --- | --- |
| Confiança | 0,5 | 0–1 |
| Respeito | 0,5 | 0–1 |
| Afeto | 0,5 | 0–1 |
| Medo | 0 | 0–1 |
| Ressentimento | 0 | 0–1 |

A reputação de alguém pode ser derivada como a média do respeito que os outros têm por ele. Ainda não é armazenada.

## Eventos que alteram relações

| Evento | Quem recebeu ou sofreu | Quem agiu | Testemunha ou quem ouviu (efeito × peso) |
| --- | --- | --- | --- |
| Ajuda dada | confiança +0,15, afeto +0,12, respeito +0,05 pelo ajudante | afeto +0,04 por quem recebeu | respeito +0,06 × (0,5 + empatia), confiança +0,03 pelo ajudante |
| Ajuda recusada | ressentimento +0,15, confiança −0,10, afeto −0,05 por quem recusou | nada | respeito −0,03 × empatia por quem recusou |
| Negociação falhou | confiança −0,12 e ressentimento +0,06 × (0,5 + agressividade) pelo parceiro, do ponto de vista de quem negociou | — | — |
| Negociação bem-sucedida | confiança +0,03 pelo parceiro, do ponto de vista de quem negociou | — | — |
| Conflito | ressentimento +0,2, medo +0,1 × (1 − coragem), afeto −0,1 pelo agressor | ressentimento +0,05 | medo +0,06 × cautela, respeito −0,03 pelo agressor |
| Perigo repelido | — | — | respeito +0,1, confiança +0,04 pelo guarda |
| Recurso removido no local de trabalho | ressentimento e desconfiança pelo autor, se conhecido | — | — |
| Conviver | afeto +0,03 × (0,5 + sociabilidade) e confiança +0,01 por quem convidou; afeto +0,02 × (0,5 + sociabilidade) de volta | | |

O peso é 1 para o que foi visto e 0,5 × confiança para o que foi ouvido. A assimetria é testada no teste 08: quem é ajudado passa a confiar mais no ajudante do que o contrário, e quem tem um pedido recusado guarda ressentimento sem que o outro guarde.

## Ajuda e conflito emergentes

- **A quem pedir:** só a alguém por quem o NPC tem confiança + afeto − ressentimento acima de 1, o valor de uma relação neutra.
- **Ajuda pedida:** quem é solicitado decide pela empatia, pelo afeto e pela confiança no solicitante, pela lealdade, pela ambição (que reduz) e pelo ressentimento (que reduz). Só ajuda se tiver comida sobrando.
- **Ajuda espontânea:** surge da utilidade quando um NPC empático vê alguém faminto.
- **Conflito:** pode ocorrer ao conviver, quando o NPC sente ressentimento acima de 0,45 e tem agressividade alta.

## Ainda não implementado

Famílias, organizações, facções, alianças e rivalidades formais. Os laços iniciais de Elden incluem quatro pares de casa, mas não há modelo de família. As relações atuais já servem de base para esses sistemas.
