"""Builds /Game/Materials/M_AethraVoxel from scratch (idempotent).

Run headless:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<this file>" -unattended -nosplash

Base colour = chunk vertex colour (block tint + baked AO) x a procedural
16x16 texel pattern per block face with a soft bevel at block edges.
No texture assets are needed; the pattern fades out with distance to avoid shimmer.
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
