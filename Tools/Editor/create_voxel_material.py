"""Builds /Game/Materials/M_AethraVoxel and M_AethraPlant from scratch (idempotent).

Run headless:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<this file>" -unattended -nosplash

Base colour = chunk vertex colour (block tint + baked AO) x a procedural
16x16 texel pattern per block face with a soft bevel at block edges.
No texture assets are needed; the pattern fades out with distance to avoid shimmer.
M_AethraPlant: masked, two-sided cross quads; procedural 16x16 grass blades or a
flower (vertex alpha 1 = grass, 0.5 = flower; the chunk sets V = 0 at the ground).
"""
import unreal

PATH, NAME = "/Game/Materials", "M_AethraVoxel"
FULL = f"{PATH}/{NAME}"

# Custom HLSL: P = absolute world position (cm), N = world vertex normal, D = pixel depth (cm).
CODE = r"""
float3 inside = P - N * 0.5;                  // stay inside the block, off the face plane
int3 texel = (int3)floor(inside / 6.25);       // 16 texels per 100 cm block
uint h = (uint)texel.x * 73856093u ^ (uint)texel.y * 19349663u ^ (uint)texel.z * 83492791u;
h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
float grain = 0.88 + 0.20 * ((h & 1023u) / 1023.0);

float3 f = frac(inside / 100.0);
float3 edge = min(f, 1.0 - f) + abs(N);        // ignore the axis along the normal
float bevel = lerp(0.80, 1.0, smoothstep(0.0, 0.045, min(edge.x, min(edge.y, edge.z))));

return lerp(grain * bevel, 1.0, saturate((D - 2500.0) / 6000.0));
"""

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary

if eal.does_asset_exist(FULL):
    eal.delete_asset(FULL)
material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())


def node(cls, x, y):
    return mel.create_material_expression(material, cls, x, y)


vertex_color = node(unreal.MaterialExpressionVertexColor, -700, -100)
world_pos = node(unreal.MaterialExpressionWorldPosition, -1100, 100)
normal = node(unreal.MaterialExpressionVertexNormalWS, -1100, 220)
depth = node(unreal.MaterialExpressionPixelDepth, -1100, 320)

custom = node(unreal.MaterialExpressionCustom, -750, 150)
custom.set_editor_property("code", CODE)
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
custom.set_editor_property("description", "AethraVoxelPattern")
inputs = []
for name in ("P", "N", "D"):
    entry = unreal.CustomInput()
    entry.set_editor_property("input_name", name)
    inputs.append(entry)
custom.set_editor_property("inputs", inputs)
mel.connect_material_expressions(world_pos, "", custom, "P")
mel.connect_material_expressions(normal, "", custom, "N")
mel.connect_material_expressions(depth, "", custom, "D")

base = node(unreal.MaterialExpressionMultiply, -350, 0)
mel.connect_material_expressions(vertex_color, "", base, "A")
mel.connect_material_expressions(custom, "", base, "B")
mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

for prop, value, y in ((unreal.MaterialProperty.MP_ROUGHNESS, 0.88, 200),
                       (unreal.MaterialProperty.MP_SPECULAR, 0.30, 300)):
    constant = node(unreal.MaterialExpressionConstant, -350, y)
    constant.set_editor_property("r", value)
    mel.connect_material_property(constant, "", prop)

mel.recompile_material(material)
if not eal.save_asset(FULL, only_if_is_dirty=False):
    raise RuntimeError(f"Could not save {FULL}")
unreal.log(f"AETHRA: created {FULL}")

# ---------------------------------------------------------------- plants
PLANT = f"{PATH}/M_AethraPlant"
PLANT_CODE = r"""
int2 t = (int2)floor(saturate(float2(UV.x, 1.0 - UV.y)) * 15.999); // texture V points down; t.y = height
int2 cell = (int2)floor(P.xy / 100.0);          // constant per plant: quads stay inside one cell
uint col = (uint)t.x * 2654435761u ^ (uint)cell.x * 73856093u ^ (uint)cell.y * 19349663u;
col ^= col >> 15; col *= 0x2c1b3c6du; col ^= col >> 12;
float3 green = float3(0.13, 0.30, 0.07);
if (Kind > 0.75)                               // tall grass: uneven blades, lighter tips
{
    float blade = 5.0 + (col % 10u);
    bool on = (col >> 8) % 3u != 0u && t.y < blade;
    float tip = t.y / max(blade - 1.0, 1.0);          // dark roots, sun-bleached tips: reads against the ground
    return float4(lerp(Tint.rgb * 0.45, Tint.rgb * 1.12 + float3(0.02, 0.015, 0.0), tip), on ? 1.0 : 0.0);
}
bool stem = (t.x == 7 || t.x == 8) && t.y < 10;
float2 c = float2(t) - float2(7.5, 11.0);
bool petal = abs(c.x) + abs(c.y) <= 3.0 && t.y >= 8;
bool core = abs(c.x) <= 0.5 && abs(c.y) <= 0.5;
float3 rgb = core ? float3(0.85, 0.65, 0.12) : petal ? Tint.rgb : green;
return float4(rgb, stem || petal ? 1.0 : 0.0);
"""

if eal.does_asset_exist(PLANT):
    eal.delete_asset(PLANT)
material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    "M_AethraPlant", PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
material.set_editor_property("two_sided", True)

vertex_color = node(unreal.MaterialExpressionVertexColor, -1100, -100)
uv = node(unreal.MaterialExpressionTextureCoordinate, -1100, 50)
world_pos = node(unreal.MaterialExpressionWorldPosition, -1100, 200)

custom = node(unreal.MaterialExpressionCustom, -700, 50)
custom.set_editor_property("code", PLANT_CODE)
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
custom.set_editor_property("description", "AethraPlantPattern")
inputs = []
for name in ("UV", "Tint", "Kind", "P"):
    entry = unreal.CustomInput()
    entry.set_editor_property("input_name", name)
    inputs.append(entry)
custom.set_editor_property("inputs", inputs)
mel.connect_material_expressions(uv, "", custom, "UV")
mel.connect_material_expressions(vertex_color, "", custom, "Tint")
mel.connect_material_expressions(vertex_color, "A", custom, "Kind")  # default pin is RGB only
mel.connect_material_expressions(world_pos, "", custom, "P")

rgb = node(unreal.MaterialExpressionComponentMask, -400, 0)
for channel in ("r", "g", "b"):
    rgb.set_editor_property(channel, True)
mask = node(unreal.MaterialExpressionComponentMask, -400, 150)
mask.set_editor_property("a", True)
mel.connect_material_expressions(custom, "", rgb, "")
mel.connect_material_expressions(custom, "", mask, "")
mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
roughness = node(unreal.MaterialExpressionConstant, -400, 300)
roughness.set_editor_property("r", 0.9)
mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

mel.recompile_material(material)
if not eal.save_asset(PLANT, only_if_is_dirty=False):
    raise RuntimeError(f"Could not save {PLANT}")
unreal.log(f"AETHRA: created {PLANT}")
