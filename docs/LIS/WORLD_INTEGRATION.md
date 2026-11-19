# LIS: integração com o mundo voxel

## Contrato existente (confirmado em `main`)

`Source/EcosDeAethra/Public/AethraVoxelWorld.h` e `AethraVoxelTypes.h`:

| Contrato | Assinatura |
| --- | --- |
| `GetBlock` | `EAethraBlock AAethraVoxelWorld::GetBlock(FIntVector Cell) const` |
| `SetBlock` | `bool AAethraVoxelWorld::SetBlock(FIntVector Cell, EAethraBlock Block)` |
| `OnBlockChanged` | `DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAethraOnBlockChanged, const FAethraBlockChange&, Change)`, com `Cell`, `Before` e `After` |
| `Seed` | `int32 AAethraVoxelWorld::Seed` (42819) |

O Marco 02 (`claude/marco-02-graficos`) não muda esse contrato. Ele apenas acrescenta blocos ao fim de `EAethraBlock`.

## Adaptador

`FLisVoxelAdapter` (`Source/AethraLIS/Public/LisVoxelAdapter.h`) recebe `FLisVoxelChange { Cell, Before, After, Actor }`, com os blocos como `uint8`. Assim o LIS não depende do módulo do jogo.

1. **`OnBlockChanged(change)`:** só enfileira a mudança. Pode ser chamado no callback da thread de jogo, como recomenda `docs/LIS_INTEGRATION.md`.
2. **`Flush(sim)`:**
   - associa cada mudança ao local mais próximo dentro de `PlaceRadiusCells` (64); o resto é ignorado e contado;
   - classifica os blocos com `Classify`;
   - agrupa por local, tipo (removido ou colocado), recurso e autor;
   - reduz o estoque abstrato: madeira na Floresta e plantação nos Campos, à razão de `BlocksPerUnit` blocos por unidade;
   - emite um evento LIS por grupo com `EmitAtCell`, de modo que só percebem os NPCs presentes no local cuja âncora esteja a até 48 células da mudança.

Uma remoção de madeira percebida pelo lenhador daquele local reduz sua crença sobre o estoque. Ela também tem relevância e valência fortes, eleva a necessidade de propósito e gera ressentimento pelo autor, se conhecido. Tudo isso altera as próximas decisões e pode alterar o objetivo na reavaliação diária. O teste 11 e o experimento Elden cobrem esse fluxo com eventos simulados. Em Elden, três lenhadores e dois outros NPCs viram o corte; dois pescadores e um explorador souberam por conversa.

## Passos para conectar após aprovar o Marco 02

Nenhum foi feito nesta branch, para não editar arquivos da equipe GPT/Codex.

1. Adicionar `"AethraLIS"` às dependências privadas de `Source/EcosDeAethra/EcosDeAethra.Build.cs`.
2. Criar no módulo do jogo um componente ou subsistema (por exemplo `UAethraLisSubsystem`) que:
   - mantém um `FLisSimulation`, ou um `FLisElden` durante os testes, e um `FLisVoxelAdapter`;
   - inscreve-se em `AAethraVoxelWorld::OnBlockChanged` com uma `UFUNCTION` que converte `FAethraBlockChange` em `FLisVoxelChange { Cell, (uint8)Before, (uint8)After, LisNobody }`;
   - chama `Flush` e `Tick(1)` a cada hora de jogo, fora do callback;
   - define `Config.Focus` pelo local mais próximo do jogador.
3. Fornecer o classificador com os ids atuais de `EAethraBlock`. Com o Marco 02, `Wood` e `BirchWood` são madeira; `Leaves`, `PineLeaves` e `BirchLeaves` são folhagem; `TallGrass`, `FlowerRed` e `FlowerYellow` são plantas, que podem ser tratadas como plantação nos Campos; `Water` é água; `Snow` e `Stone` são pedra; `Grass`, `Dirt` e `Sand` são solo. O classificador de `FLisElden` cobre só os ids 0–6 de `main`.
4. Ancorar os locais abstratos em coordenadas reais do mundo voxel, ajustando `World.Places[].Cell`. Como o terreno vem da seed, a Vila de Elden pode ser escolhida perto do spawn, numa área plana do raio de 256 células que o Marco 02 preserva.
5. Gravar o LIS em `Saved/LIS/<seed>.lis` com `SaveToFile`, junto do save voxel e sem alterar seu formato.
6. Para o LIS saber quem editou, `FAethraBlockChange` precisa de um campo de autor (por exemplo `Instigator`). Essa mudança é do contrato da equipe GPT/Codex e deve ir em PR próprio. Até lá, o autor é desconhecido (0) e nenhuma relação com o jogador é criada por edições.
7. Para os NPCs agirem no mundo, por exemplo cortando árvores de verdade, o LIS deve pedir edições por uma fila que o jogo executa depois do callback, com `SetBlock`. Esse caminho ainda não existe.

## Riscos conhecidos

- O mapa abstrato e o mundo voxel podem divergir; é preciso definir qual é a verdade dos estoques quando os dois existirem.
- Cortes grandes geram muitos eventos de bloco; a agregação por `Flush` mantém um evento LIS por grupo.
