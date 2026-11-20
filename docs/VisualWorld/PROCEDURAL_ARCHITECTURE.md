# Arquitetura procedural (CivGen-lite)

Código: `AAethraVisualWorld::PlanSettlement` e `BuildSettlement` (`Private/VisualWorld/AethraVisualWorld.cpp`).

## O que existe

Um assentamento é planejado pelo terreno, não posto à mão:

1. **Local:** ao longo do rio principal, perto do spawn. O ponto escolhido tem margem seca e plana, o rio tem largura de ponte e a outra margem é seca.
2. **Platôs:** o gerador recebe dois platôs, um no centro e um do outro lado da ponte, antes de gerar qualquer chunk. O terreno se achata com borda suave.
3. **Construções:** casa, torre redonda e praça ficam no platô central, orientadas pelo eixo do rio. A ponte atravessa o rio entre `BridgeA` e `BridgeB`: o número de arcos acompanha a largura do rio e o tabuleiro fica acima do nível da água.
4. **Materiais:** vêm de `FAmeSelector::Building` (cultura EldenStone, riqueza 0,7: reboco, mármore e cerâmica azul) e da rocha local.

Tudo vira peças AHG comuns, que podem ser removidas e são persistidas. As capturas C1 e B1 mostram a casa, a torre e a ponte.

## Como cresce

- **Culturas:** já há três paletas (EldenStone, RiverTimber e HighlandSlate). Escolher a cultura pelo bioma e pela rocha é a próxima etapa.
- **Gramática:** casa, torre, praça e ponte são funções de montagem. Uma gramática de quarteirões, muralha e ruas usa as mesmas peças.
- **Riqueza:** o parâmetro de riqueza já muda telhados, guarnições e colunas.
- **Vila viva:** os NPCs do LIS (PR #5) poderão ocupar e modificar essas peças pela mesma API (`FAhgLayer::Add` e `Remove`).

## Limites

- Há um único assentamento por mundo.
- Não há ruas, muralha nem interior.
- A cidade grande da referência é inspiração de escala e silhueta, não meta desta entrega.
