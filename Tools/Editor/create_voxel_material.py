"""Builds the Aethra materials in /Game/Materials from scratch (idempotent, no texture assets).

Run headless:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<this file>" -EnablePlugins=PythonScriptPlugin -unattended -nosplash

Vertex colour contract (AethraBlockColor.h / AethraVoxelChunk.cpp):
  RGB = block tint x baked ambient occlusion, A = block id / 31 (EAethraBlock value).
  Plants override A: 1 = tall grass, 0.5 = flower; their UV V = 0 at the ground.
  (An earlier inverted-V note came from a broken mask helper: new ComponentMasks start with R+G on.)

M_AethraVoxel  opaque blocks and the horizon: per-block 16x16 procedural pattern (grass fringe over
               dirt, stone slabs, dirt pebbles, sand ripples, bark and rings, snow), vegetation hue
               drift, soft bevel; detail fades with distance.
M_AethraLeaves masked, two-sided foliage (light through canopies), cut-out leaf clusters, wind sway.
M_AethraPlant  masked, two-sided cross quads: grass blades or a flower, wind sway on the tips.
M_AethraWater  translucent, per-pixel lit: analytic waves, depth-tinted colour and opacity.
"""
import unreal

PATH = "/Game/Materials"
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def srgb_to_linear(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


# Grass sides carry the side tint; below the fringe the material turns it into dirt (same AO).
GRASS_SIDE, DIRT = (98, 140, 54), (113, 78, 50)
DIRT_RATIO = "float3(%s)" % ", ".join("%.4f" % (srgb_to_linear(d) / srgb_to_linear(g)) for g, d in zip(GRASS_SIDE, DIRT))

# Shared helpers: integer hash per texel and smooth value noise. A local struct is the usual way to
# define functions inside a Custom node.
HELPERS = r"""
struct FAethra
{
    float H(int3 p)
    {
        uint h = (uint)p.x * 73856093u ^ (uint)p.y * 19349663u ^ (uint)p.z * 83492791u;
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return (h & 1023u) / 1023.0;
    }
    float V(float2 p, int salt)
    {
        int2 i = (int2)floor(p);
        float2 f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        float a = H(int3(i, salt)), b = H(int3(i + int2(1, 0), salt));
        float c = H(int3(i + int2(0, 1), salt)), d = H(int3(i + int2(1, 1), salt));
        return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
    }
};
FAethra fn;
float3 inside = P - N * 0.5;                    // inside the block, off the face plane
int3 cell = (int3)floor(inside / 100.0);
float3 f = frac(inside / 100.0);
float3 an = abs(N);
bool top = N.z > 0.5;
bool bottom = N.z < -0.5;
float2 uv = an.z > 0.5 ? f.xy : (an.x > 0.5 ? f.yz : f.xz); // on sides uv.y is the height
int2 t = (int2)floor(uv * 16.0);
int face = an.z > 0.5 ? (top ? 0 : 1) : (an.x > 0.5 ? 2 : 3);
int3 key = int3(t, face) + cell * 37;
float g = fn.H(key);                              // texel grain 0..1
float hue = fn.V(P.xy / 2200.0, 11);              // ~20 m patches of warmer / cooler vegetation
float3 drift = lerp(float3(0.90, 1.0, 0.82), float3(1.12, 1.02, 0.86), hue);
"""

VOXEL_CODE = HELPERS + r"""
int id = (int)round(Id * 31.0);
float3 col = C;
float rough = 0.86;
float k = 1.0;
if (id == 1 && !top && !bottom)                   // grass side: ragged green fringe over dirt
{
    int fringe = 2 + (int)(fn.H(int3(t.x, 7, face) + cell * 37) * 3.0);
    if (15 - t.y >= fringe) { col *= DIRT_RATIO; id = 2; }
}
if (id == 1)      { k = 0.82 + 0.30 * g; if (top && g > 0.93) k *= 1.15; col *= drift; }
else if (id == 2) { float clump = fn.H(int3(t / 2, face) + cell * 37); k = 0.78 + 0.22 * clump + 0.12 * g; if (g > 0.95) k = 1.25; }
else if (id == 3 || (id >= 17 && id <= 21))      // stone and the WorldGen rocks: flat slabs, lighter patches, dark seams
{
    float v = fn.V(uv * 4.0 + float2(cell.xy) * 4.0 + face * 13.0, 3);
    k = 0.86 + 0.12 * g;
    if (v > 0.62) k += 0.16;
    if (abs(v - 0.62) < 0.035) k -= 0.28;
    rough = 0.74;
}
else if (id == 4) { k = 0.93 + 0.10 * g; if (top) k *= 0.97 + 0.04 * sin(uv.y * 25.0 + uv.x * 6.0); rough = 0.92; }
else if (id == 5 || id == 13)                     // oak / birch: rings on cut faces, bark on sides
{
    if (top || bottom)
    {
        float r = length(uv - 0.5) * 16.0;
        k = (frac(r / 2.2) < 0.35 ? 0.82 : 1.0) * (0.95 + 0.08 * g);
        if (r > 7.0) k *= 0.7;
    }
    else if (id == 5) { float streak = fn.H(int3(t.x, t.y / 4, face) + cell * 37); k = (0.72 + 0.32 * streak) * (0.88 + 0.12 * g); }
    else { k = 0.96 + 0.05 * g; float dash = fn.H(int3(t.x / 3, t.y, face) + cell * 37); if (dash > 0.84 && t.y % 3 == 0) k = 0.32; }
}
else if (id == 6 || id == 7 || id == 14) { k = 0.70 + 0.45 * g; col *= drift; } // leaves seen on the horizon
else if (id == 11) { k = 0.95 + 0.05 * g; rough = 0.55; }
else if (id == 12) { k = 0.95 + 0.06 * g; rough = 0.08; }
else if (id == 15) { float peb = fn.H(int3(t / 3, face) + cell * 37); k = 0.70 + 0.45 * peb + 0.08 * g; if ((t.x + t.y) % 3 == 0 && g > 0.7) k *= 0.6; } // gravel: pebbles
else if (id == 16) { k = 0.85 + 0.18 * fn.V(uv * 3.0 + float2(cell.xy) * 3.0, 5) + 0.06 * g; rough = 0.6; }   // mud: wet, smooth
else if (id == 22) { float v = fn.V(uv * 3.0 + float2(cell.xy) * 3.0 + face * 7.0, 9); k = 0.97 + 0.04 * g; if (abs(v - 0.5) < 0.03) k = 0.70; rough = 0.45; } // marble: veins
else if (id == 23)                                // stone bricks: 2 courses, offset joints
{
    int row = t.y / 8; int jx = (t.x + (row % 2) * 4) % 8;
    k = (t.y % 8 == 0 || jx == 0) ? 0.62 : 0.88 + 0.14 * fn.H(int3((t.x + (row % 2) * 4) / 8, row, face) + cell * 37) + 0.06 * g;
    rough = 0.8;
}
else if (id == 24) { k = (t.y % 4 == 0 ? 0.68 : 0.92 + 0.10 * g) * (0.9 + 0.15 * fn.H(int3(0, t.y / 4, face) + cell * 37)); } // planks
else if (id == 25) { k = (t.y % 4 == 3 ? 0.66 : 0.95 + 0.06 * g) * ((t.x + (t.y / 4) * 2) % 4 == 0 ? 0.85 : 1.0); rough = 0.7; } // roof tiles
else if (id == 26) { float facet = fn.H(int3(t / 4, face) + cell * 37); k = 0.85 + 0.55 * facet; rough = 0.15; } // Aetherite crystal
else if (id == 31) { k = 0.94 + 0.06 * g; if (t.y % 8 == 4) k = 0.78; rough = 0.5; } // ceramic
float3 e = min(f, 1.0 - f) + an;                  // soft bevel at block edges
float bevel = lerp(0.82, 1.0, smoothstep(0.0, 0.045, min(e.x, min(e.y, e.z))));
k = lerp(k * bevel, 1.0, saturate((D - 3000.0) / 9000.0));
return float4(col * k, rough);
""".replace("DIRT_RATIO", DIRT_RATIO)

LEAVES_CODE = HELPERS + r"""
float cluster = fn.H(int3(t / 2, face) + cell * 37);
float gap = fn.H(int3(t / 4, face + 8) + cell * 37);    // few, larger gaps between leaf clusters
float opacity = (gap < 0.22 && g < 0.6) || (cluster < 0.08) ? 0.0 : 1.0;
opacity = max(opacity, saturate((D - 3500.0) / 2500.0)); // solid far away: no shimmering holes
float k = 0.70 + 0.25 * g + 0.25 * cluster;          // light and dark leaf groups, not per-texel noise
return float4(C * k * drift, opacity);
"""

# Wind: one smooth field over world space, so shared vertices of neighbouring blocks move together.
WIND_CODE = r"""
float s = sin(T * 1.7 + dot(P.xy, float2(0.011, 0.013))) + 0.5 * sin(T * 2.9 + dot(P.xy, float2(-0.021, 0.017)));
return float3(s * 2.2, s * 1.6, 0.0) * Amount;
"""

PLANT_CODE = r"""
int2 t = (int2)floor(saturate(UV) * 15.999); // the chunk sets V = 0 at the ground: t.y = height
int2 cell = (int2)floor(P.xy / 100.0);          // constant per plant: quads stay inside one cell
uint col = (uint)t.x * 2654435761u ^ (uint)cell.x * 73856093u ^ (uint)cell.y * 19349663u;
col ^= col >> 15; col *= 0x2c1b3c6du; col ^= col >> 12;
float3 green = float3(0.13, 0.30, 0.07);
if (Kind > 0.75)                               // tall grass: uneven blades, dark roots, bright tips
{
    float blade = 6.0 + (col % 9u);
    // Thin separate blades: every other texel column, about a quarter skipped.
    bool on = t.x % 2 == 0 && (col >> 8) % 4u != 0u && t.y < blade;
    float tip = t.y / max(blade - 1.0, 1.0);
    return float4(lerp(Tint.rgb * 0.5, Tint.rgb * 1.0 + float3(0.015, 0.01, 0.0), tip), on ? 1.0 : 0.0);
}
bool stem = (t.x == 7 || t.x == 8) && t.y < 10;
float2 c = float2(t) - float2(7.5, 11.0);
bool petal = abs(c.x) + abs(c.y) <= 3.0 && t.y >= 8;
bool core = abs(c.x) <= 0.5 && abs(c.y) <= 0.5;
float3 rgb = core ? float3(0.85, 0.65, 0.12) : petal ? Tint.rgb : green;
return float4(rgb, stem || petal ? 1.0 : 0.0);
"""

WATER_NORMAL_CODE = r"""
float2 p = P.xy;
float2 d1 = float2(0.8, 0.6), d2 = float2(-0.5, 0.86), d3 = float2(0.2, -0.98);
float2 g = d1 * 0.10 * cos(dot(p, d1) * 0.012 + T * 1.1)
         + d2 * 0.07 * cos(dot(p, d2) * 0.021 + T * 1.6)
         + d3 * 0.05 * cos(dot(p, d3) * 0.047 + T * 2.3);
return normalize(float3(-g, 1.0));
"""

WATER_COLOUR_CODE = r"""
float depth = saturate(Depth / 450.0);
float3 colour = lerp(float3(0.10, 0.32, 0.30), float3(0.02, 0.09, 0.16), depth);
float opacity = lerp(0.25, 0.88, depth) * saturate(Depth / 30.0); // soft shoreline
return float4(colour, opacity);
"""


def new_material(name):
    full = f"{PATH}/{name}"
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    return full, unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, PATH, unreal.Material, unreal.MaterialFactoryNew())


def node(material, cls, x=-1000, y=0):
    return mel.create_material_expression(material, cls, x, y)


def custom(material, code, output, description, inputs):
    """inputs: list of (name, expression, pin)."""
    expr = node(material, unreal.MaterialExpressionCustom, -600, 0)
    expr.set_editor_property("code", code)
    expr.set_editor_property("output_type", output)
    expr.set_editor_property("description", description)
    entries = []
    for name, _, _ in inputs:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        entries.append(entry)
    expr.set_editor_property("inputs", entries)
    for name, source, pin in inputs:
        mel.connect_material_expressions(source, pin, expr, name)
    return expr


def mask(material, source, channels):
    expr = node(material, unreal.MaterialExpressionComponentMask, -300, 0)
    for channel in "rgba":  # set all four: a new mask starts with R and G enabled
        expr.set_editor_property(channel, channel in channels)
    mel.connect_material_expressions(source, "", expr, "")
    return expr


def constant(material, value):
    expr = node(material, unreal.MaterialExpressionConstant, -300, 300)
    expr.set_editor_property("r", value)
    return expr


def wind(material, world_pos, amount):
    time = node(material, unreal.MaterialExpressionTime)
    return custom(material, WIND_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, "AethraWind",
                  [("P", world_pos, ""), ("T", time, ""), ("Amount", amount, "")])


def save(full, material):
    mel.recompile_material(material)
    if not eal.save_asset(full, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {full}")
    unreal.log(f"AETHRA: created {full}")


F3, F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT3, unreal.CustomMaterialOutputType.CMOT_FLOAT4
MP = unreal.MaterialProperty

# ---------------------------------------------------------------- blocks
full, m = new_material("M_AethraVoxel")
vc = node(m, unreal.MaterialExpressionVertexColor)
wp = node(m, unreal.MaterialExpressionWorldPosition)
n = node(m, unreal.MaterialExpressionVertexNormalWS)
d = node(m, unreal.MaterialExpressionPixelDepth)
out = custom(m, VOXEL_CODE, F4, "AethraVoxelPattern",
             [("P", wp, ""), ("N", n, ""), ("D", d, ""), ("C", vc, ""), ("Id", vc, "A")])
mel.connect_material_property(mask(m, out, "rgb"), "", MP.MP_BASE_COLOR)
mel.connect_material_property(mask(m, out, "a"), "", MP.MP_ROUGHNESS)
mel.connect_material_property(constant(m, 0.3), "", MP.MP_SPECULAR)
save(full, m)

# ---------------------------------------------------------------- leaves
full, m = new_material("M_AethraLeaves")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
vc = node(m, unreal.MaterialExpressionVertexColor)
wp = node(m, unreal.MaterialExpressionWorldPosition)
n = node(m, unreal.MaterialExpressionVertexNormalWS)
d = node(m, unreal.MaterialExpressionPixelDepth)
out = custom(m, LEAVES_CODE, F4, "AethraLeaves", [("P", wp, ""), ("N", n, ""), ("D", d, ""), ("C", vc, "")])
rgb = mask(m, out, "rgb")
mel.connect_material_property(rgb, "", MP.MP_BASE_COLOR)
mel.connect_material_property(mask(m, out, "a"), "", MP.MP_OPACITY_MASK)
sss = node(m, unreal.MaterialExpressionMultiply)
mel.connect_material_expressions(rgb, "", sss, "A")
mel.connect_material_expressions(constant(m, 0.55), "", sss, "B")
mel.connect_material_property(sss, "", MP.MP_SUBSURFACE_COLOR)
mel.connect_material_property(constant(m, 0.8), "", MP.MP_ROUGHNESS)
mel.connect_material_property(wind(m, wp, constant(m, 1.0)), "", MP.MP_WORLD_POSITION_OFFSET)
save(full, m)

# ---------------------------------------------------------------- plants
full, m = new_material("M_AethraPlant")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
m.set_editor_property("two_sided", True)
vc = node(m, unreal.MaterialExpressionVertexColor)
uv = node(m, unreal.MaterialExpressionTextureCoordinate)
wp = node(m, unreal.MaterialExpressionWorldPosition)
out = custom(m, PLANT_CODE, F4, "AethraPlantPattern",
             [("UV", uv, ""), ("Tint", vc, ""), ("Kind", vc, "A"), ("P", wp, "")])
mel.connect_material_property(mask(m, out, "rgb"), "", MP.MP_BASE_COLOR)
mel.connect_material_property(mask(m, out, "a"), "", MP.MP_OPACITY_MASK)
mel.connect_material_property(constant(m, 0.9), "", MP.MP_ROUGHNESS)
time = node(m, unreal.MaterialExpressionTime)
tip_wind = WIND_CODE.replace("* Amount;", "* UV.y * 2.0;")  # roots stay, tips sway
mel.connect_material_property(custom(m, tip_wind, F3, "AethraPlantWind", [("P", wp, ""), ("T", time, ""), ("UV", uv, "")]),
                              "", MP.MP_WORLD_POSITION_OFFSET)
save(full, m)

# ---------------------------------------------------------------- water
full, m = new_material("M_AethraWater")
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
m.set_editor_property("tangent_space_normal", False)
wp = node(m, unreal.MaterialExpressionWorldPosition)
time = node(m, unreal.MaterialExpressionTime)
scene = node(m, unreal.MaterialExpressionSceneDepth)
pixel = node(m, unreal.MaterialExpressionPixelDepth)
behind = node(m, unreal.MaterialExpressionSubtract)
mel.connect_material_expressions(scene, "", behind, "A")
mel.connect_material_expressions(pixel, "", behind, "B")
normal = custom(m, WATER_NORMAL_CODE, F3, "AethraWaves", [("P", wp, ""), ("T", time, "")])
colour = custom(m, WATER_COLOUR_CODE, F4, "AethraWaterColour", [("Depth", behind, "")])
mel.connect_material_property(normal, "", MP.MP_NORMAL)
mel.connect_material_property(mask(m, colour, "rgb"), "", MP.MP_BASE_COLOR)
mel.connect_material_property(mask(m, colour, "a"), "", MP.MP_OPACITY)
mel.connect_material_property(constant(m, 0.04), "", MP.MP_ROUGHNESS)
mel.connect_material_property(constant(m, 0.8), "", MP.MP_SPECULAR)
save(full, m)
