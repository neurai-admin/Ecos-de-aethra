# Convenções

- Use os prefixos de classe e de tipo da Unreal. Arquivos públicos ficam em `Source/EcosDeAethra/Public`. Detalhes de implementação ficam em `Private`.
- Declare propriedades e eventos refletidos apenas onde houver uso no editor, em Blueprints ou na serialização. Inclua o arquivo `.generated.h` por último no cabeçalho pertinente.
- Expresse posições físicas em centímetros e células em coordenadas inteiras globais. Use as funções de conversão do sistema voxel.
- Mantenha a geração estável para uma mesma seed e versione o formato caso a persistência evolua.
- Reproduza os casos de coordenada negativa, edição na divisa e reabertura ao alterar o motor voxel.
- Use Git LFS para os binários previstos em `.gitattributes`. Antes de adicionar assets, confirme os filtros ativos com `git lfs install` no clone e `git check-attr filter -- caminho/do/asset.uasset`.
- Prepare commits descritivos em branch de trabalho. Revise em PR antes de integrar à branch padrão.
