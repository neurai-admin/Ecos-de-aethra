# HydroGen — água contextual

Código: `Public/VisualWorld/AethraHydroGen.h` e o `.cpp` correspondente. Shaders: `M_AethraHydro`, `M_AethraFall` e `M_AethraMist`.

## A cor vem da física simplificada, não de uma tabela

Cada corpo d'água tem propriedades:
- profundidade e temperatura;
- **salinidade**;
- turbidez, sedimento, matéria orgânica e algas;
- velocidade da corrente e agitação.

A partir delas, `ComputeOptics` calcula por canal (r, g, b):
- **Absorção:** água pura (absorve o vermelho), matéria orgânica (absorve o azul), algas e sedimento.
- **Espalhamento:** turbidez, algas e sedimento.
- **Cor de água profunda iluminada (in-scatter):** o albedo de espalhamento simples, normalizado para uma refletância de 2,5% a 30%.
- **Profundidade visível:** a profundidade em que o verde cai a 10%.

No shader, a cor final é `SceneColor·T + inscatter·(1−T)·Luz`, com `T = exp(−2σd)` e d a profundidade real da cena atrás da superfície. A água rasa mostra o fundo e a funda mostra a própria cor (teste 08).

**A salinidade não muda a cor.** Ela muda a densidade: `ρ = 1000·(1 − 6,8e-6·(T − 4)²) + 0,78·S`. Já existe `FloatFraction(ρ_corpo)` para a flutuabilidade futura. O teste 07 verifica três coisas:
- a mesma água com e sem sal tem a mesma óptica;
- um corpo de 1.010 kg/m³ afunda na água doce e flutua no mar;
- o lago salgado é mais denso que o mar.

## Perfis (9 predefinidos)

O gerador usa: TropicalSea (mar), GreenRiver ou ClearStream (rio principal, pela seed; a 125 tem GreenRiver), ClearStream (riachos), Waterfall e, para os lagos, ClearLake acima de 70 m ou DarkLake abaixo. São 6 perfis no mundo da seed 125.

| Perfil | Tipo | Salinidade | Característica |
| --- | --- | --- | --- |
| ClearStream | riacho | 0 | Pouca matéria, rápido |
| GreenRiver | rio | 0 | Algas: verde-esmeralda (o rio principal da seed 125) |
| SiltRiver | rio | 0 | Sedimento: barrento |
| DarkLake | lago baixo | 0 | Matéria orgânica: escuro |
| ClearLake | lago alto | 0 | Transparente |
| TropicalSea | mar | 35 | Turquesa (o mar de todo mundo gerado nesta versão) |
| DeepOcean | mar profundo | 35 | Azul profundo (predefinido, ainda não usado pelo gerador) |
| SaltLake | lago salgado | 120 | Denso |
| Waterfall | queda | 0 | Agitação máxima |

## O que é visual e o que é simulado

| Item | Simulado | Visual |
| --- | --- | --- |
| Cor e transparência pela profundidade | Sim: óptica por corpo e profundidade real da cena | — |
| Densidade e flutuabilidade | Sim: dado pronto, ainda não usado por física | — |
| Direção e velocidade do rio | Sim: vem da drenagem (D8) | A animação da superfície usa esses valores |
| Movimento de água, vazão, enchentes | **Não** | — |
| **Cachoeiras** | **Não** | Faixa animada que acompanha o terreno, mais névoa na base. **Não é hidrodinâmica** |
| Ondas | Não | Normais animadas no espaço do mundo |

Sobre as cachoeiras:
- **Onde ficam:** são escolhidas onde a drenagem cai 4 m ou mais entre nós. A posição é levada pelo domain warp até onde o rio realmente aparece.
- **Altura do terreno:** no terreno renderizado, essa queda é uma encosta íngreme ao longo de 16 m, e não um paredão. Por isso a faixa segue o chão como uma cascata.
- **Limitação conhecida:** o canal do riacho nem sempre é escavado o bastante perto da queda, e a cascata da captura D1 é discreta. Um passo de "escavar degrau" no gerador está no roadmap.

## Dia e noite

A luz da água acompanha o sol: o parâmetro `Light` vai de 1 de dia a 0,16 à noite. Sem isso, a água brilharia no escuro.
