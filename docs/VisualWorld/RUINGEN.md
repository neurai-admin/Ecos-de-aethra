# Aethra RuinGen

Código: `Public/VisualWorld/AethraRuinGen.h` e o `.cpp` correspondente; desenho em `AAethraVisualWorld::BuildRuins`.

Atende ao adendo das ruínas: civilizações antigas geradas proceduralmente na superfície e no subsolo. As ruínas reforçam exploração, lore, descoberta e recompensa, e não são cópia de estruturas de outro jogo.

## Culturas antigas (originais)

| Cultura | Materiais | Onde aparece |
| --- | --- | --- |
| Aldarim | Calcário talhado, mármore, bronze | Calcário e mármore; templos de planície |
| Skarn | Basalto, ardósia, ferro | Basalto e ardósia; fortes e torres nas alturas |
| Mirethi | Arenito, cerâmica azul, bronze | Arenito e granito; santuários |

## Tipos

- **Superfície:** templo, muralha quebrada, torre caída, santuário e restos de estátua.
- **Subsolo:** salão subterrâneo (com colunas, altar e estantes de biblioteca), cripta (com sarcófagos e tampas deslocadas) e mina abandonada (com escoras de madeira, trilho, caixas e minério deixado).

O tipo e a cultura vêm do lugar:
- **Altitude acima de 170 m:** torre ou forte Skarn.
- **Floresta:** santuário coberto de musgo.
- **Perto de água:** fortes de rio.
- **Planície:** templo, estátua ou muralha.
- **Subsolo:** mina nas cavernas naturais, cripta nas obscuras, salão nas encantadas e gigantes.

## Variação e decadência

Cada sítio tem:
- **preservação** de 0,15 a 0,85;
- **idade** de 400 a 3.000 anos;
- **importância**;
- **raridade** de 0 a 3 (comum, incomum, rara, lendária).

`Build()` aplica a decadência:
- partes altas somem primeiro;
- colunas quebradas deixam o fuste caído ao lado;
- pedras assentam com leve inclinação;
- blocos expostos ganham musgo.

O teste 21 confere que um templo bem conservado tem mais colunas de pé (12) que um arruinado (0).

## Mais que decoração

Cada sítio também traz contratos para sistemas futuros:
- **`Lore`:** tags `culture:…`, `kind:…`, `era:…`;
- **`Cache`:** o ponto onde uma recompensa será colocada, por exemplo a tigela de bronze sobre o altar do templo;
- **raridade** e **importância**.

Ainda não existem loot, textos de lore nem missões.

## Mundo da seed 125

- **Total:** 24 ruínas (10 na superfície e 14 no subsolo), de 8 tipos e 3 culturas.
- **Demonstração:** um templo Aldarim perto da vila e um salão subterrâneo na caverna encantada (capturas R1 e D2).
- **Integração:** as ruínas da superfície ficam sobre o chão real do WorldGen (teste 18), as árvores não nascem dentro delas e são visíveis de longe.

## Limites

- As peças das ruínas são geradas, não editáveis. Removê-las exigiria uma camada de alterações como a das árvores cortadas.
- Ainda faltam pontes antigas, castelos, cidades soterradas, bibliotecas grandes e altares de caverna isolados. Esses tipos estão no roadmap e usam as mesmas peças AHG.
