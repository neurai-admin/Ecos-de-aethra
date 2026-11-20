# Proposta: autor das alterações de bloco (`Instigator`) e leitura de coluna

Branch: `claude/voxel-contract-lis-01`, a partir de `codex/marco-02-correcoes` (`095df36`). Esta proposta precisa da revisão da equipe GPT/Codex antes de entrar em qualquer branch principal.

## Contrato atual

`FAethraBlockChange { Cell, Before, After }`, emitido por `AAethraVoxelWorld::OnBlockChanged` depois que `SetBlock(Cell, Block)` valida, grava o save e reconstrói os chunks. O evento não diz quem fez a alteração.

## Extensão retrocompatível

| Mudança | Compatibilidade |
| --- | --- |
| `EAethraInstigatorKind { Unknown, Player, Npc, Environment }` e `FAethraInstigator { Kind, Id }` | Tipos novos |
| `FAethraBlockChange::Instigator` acrescentado ao fim | Ouvintes que não leem o campo continuam iguais; o padrão é `Unknown`, 0 |
| `SetBlockBy(Cell, Block, By)` | Mesma validação, save, reconstrução e broadcast de `SetBlock` |
| `SetBlock(Cell, Block)` | Assinatura e comportamento mantidos; chama `SetBlockBy` com autor `Unknown` |
| `GetColumn(Point, OutColumn)` | Somente leitura: geração da coluna mais edições salvas, numa única geração de terreno |
| `AAethraCharacter::EditBlock` | Passa a usar `SetBlockBy(..., FAethraInstigator::Player())` |

O parâmetro chama-se `By` porque `AActor` já tem um membro `Instigator` (a UHT não permite sombreá-lo).

## Identificadores estáveis

- `Player`: índice local do jogador (0 no jogo atual). Não usa ponteiro nem id de ator, que mudam ao reiniciar.
- `Npc`: id persistente da identidade no LIS (`FLisIdentity::Id`, gravado no save do LIS).
- `Environment`: id do sistema (por exemplo, crescimento de vegetação); 0 por padrão.

## Impactos no sistema voxel

- **Save voxel:** não muda. O autor não é gravado (`SaveFormat` continua 1), então saves antigos e novos são idênticos.
- **Desempenho:** `SetBlock` só acrescenta uma cópia de 8 bytes. `GetColumn` evita as 128 gerações de coluna que leituras célula a célula com `GetBlock` custariam.
- **Recursão:** a proteção `bNotifying` vale também para `SetBlockBy`. Quem reagir ao evento deve enfileirar novas alterações, não chamá-las dentro do callback.
- **Blueprints:** os nós existentes continuam iguais; `SetBlockBy` é um nó novo.

## Testes

`Aethra.Voxel.InstigatorCompatibility`:
- `SetBlock` antigo continua aceito e informa autor `Unknown`, id 0;
- `SetBlockBy` informa o jogador e o id persistente do NPC;
- a validação é a mesma (célula fora do mundo e alteração sem efeito são recusadas);
- `GetColumn` coincide célula a célula com `GetBlock`, edições incluídas, e é ar fora do mundo;
- um mundo novo restaura do disco as três edições.

Os 6 testes voxel existentes continuam passando.
