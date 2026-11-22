"""Builds the Aethra materials in /Game/Materials from scratch (idempotent, no texture assets).

Run headless:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<this file>" -EnablePlugins=PythonScriptPlugin -unattended -nosplash

Vertex colour contract (AethraBlockColor.h / AethraVoxelChunk.cpp):
  RGB = block tint x baked ambient occlusion, A = block id / 31 (EAethraBlock value).
  Plants are small meshes (blades, stems, corollas): UV.x = 0 on blades (UV.y = height), 1 + radius on corollas.
  Leaf cards: UV 0..1 per card, A = the card's seed.
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


# Grass sides carry the side tint; below a soft, wavy moss lip the material paints them as soil.
GRASS_SIDE, DIRT = (98, 140, 54), (113, 78, 50)
DIRT_RATIO = "float3(%s)" % ", ".join("%.4f" % (srgb_to_linear(d) / srgb_to_linear(g)) for g, d in zip(GRASS_SIDE, DIRT))

# Shared helpers: hash and smooth value noise. A local struct is the usual way to define functions inside
# a Custom node. Everything here is continuous (no texel grid): ECOS DE AETHRA paints its blocks.
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
float3 f = frac(inside / 100.0);
float3 an = abs(N);
bool top = N.z > 0.5;
bool bottom = N.z < -0.5;
float2 uv = an.z > 0.5 ? f.xy : (an.x > 0.5 ? f.yz : f.xz); // on sides uv.y is the height
int face = an.z > 0.5 ? (top ? 0 : 1) : (an.x > 0.5 ? 2 : 3);
// World-aligned coordinates (cm): every pattern runs across block borders.
float2 w = an.z > 0.5 ? inside.xy : (an.x > 0.5 ? inside.yz : inside.xz);
float n1 = fn.V(w / 38.0 + face * 17.0, 1);       // broad brush strokes
float n2 = fn.V(w / 9.0 + face * 5.0, 2);         // fine grain
float g = 0.55 * n1 + 0.45 * n2;
float hue = fn.V(P.xy / 2200.0, 11);              // ~20 m patches of warmer / cooler vegetation
float3 drift = lerp(float3(0.90, 1.0, 0.82), float3(1.12, 1.02, 0.86), hue);
"""

VOXEL_CODE = HELPERS + r"""
int id = (int)round(Id * 31.0);
float3 col = C;
float rough = 0.86;
float k = 1.0;
if (id == 1 && !top && !bottom)                   // grass side: a soft wavy moss lip over soil
{
    float lip = 0.80 + 0.10 * fn.V(float2((w.x + w.y) / 22.0, 3.0), 7);
    float moss = smoothstep(lip - 0.05, lip + 0.05, f.z);
    col = lerp(col * DIRT_RATIO, col, moss);
    if (moss < 0.5) id = 2;
}
if (id == 1)      { k = 0.84 + 0.26 * g; col *= drift; }
else if (id == 2) { k = 0.80 + 0.28 * g; }
else if (id == 3 || (id >= 17 && id <= 21))      // stone and the WorldGen rocks: layered slabs with soft seams
{
    float v = fn.V(w / 28.0 + face * 13.0, 3);
    k = 0.86 + 0.12 * n2 + (v > 0.6 ? 0.12 : 0.0);
    k -= 0.25 * (1.0 - smoothstep(0.0, 0.035, abs(v - 0.6)));
    rough = 0.74;
}
else if (id == 4) { k = 0.94 + 0.08 * n2; if (top) k *= 0.97 + 0.04 * sin(w.y * 0.25 + w.x * 0.06); rough = 0.92; }
else if (id == 5 || id == 13)                     // oak / birch: rings on cut faces, bark on sides
{
    if (top || bottom)
    {
        float r = length(uv - 0.5);
        k = (0.84 + 0.16 * smoothstep(0.25, 0.55, frac(r * 7.0))) * (r > 0.44 ? 0.72 : 1.0);
    }
    else if (id == 5) { k = 0.70 + 0.36 * fn.V(float2(w.x / 6.0, w.y / 45.0), 4) + 0.06 * n2; }
    else { k = 0.97 - 0.62 * smoothstep(0.74, 0.8, fn.V(float2(w.x / 7.0, w.y / 4.0), 4)); }
}
else if (id == 6 || id == 7 || id == 14) { k = 0.72 + 0.4 * g; col *= drift; } // leaves seen on the horizon
else if (id == 11) { k = 0.95 + 0.05 * g; rough = 0.55; }
else if (id == 12) { k = 0.95 + 0.06 * g; rough = 0.08; }
else if (id == 15) { float peb = fn.V(w / 7.0 + face * 3.0, 6); k = 0.62 + 0.5 * smoothstep(0.25, 0.45, peb) * (0.8 + 0.4 * n1); } // gravel: rounded pebbles
else if (id == 16) { k = 0.85 + 0.18 * fn.V(w / 33.0, 5) + 0.05 * n2; rough = 0.55; }  // mud: wet, smooth
else if (id == 22) { float v = fn.V(w / 33.0 + face * 7.0, 9); k = 0.97 + 0.03 * n2 - 0.27 * (1.0 - smoothstep(0.0, 0.03, abs(v - 0.5))); rough = 0.45; } // marble: veins
else if (id == 23)                                // stone bricks: two courses, offset joints, worn edges
{
    float row = floor(uv.y * 2.0);
    float jx = frac(uv.x * 2.0 + row * 0.5);
    float mortar = 1.0 - smoothstep(0.03, 0.07, min(min(frac(uv.y * 2.0), 1.0 - frac(uv.y * 2.0)), min(jx, 1.0 - jx)));
    k = lerp(0.86 + 0.14 * n1, 0.6, mortar);
    rough = 0.8;
}
else if (id == 24) { float board = frac(uv.y * 4.0); k = (0.86 + 0.16 * fn.V(float2(w.x / 30.0, floor(uv.y * 4.0) * 9.0 + w.y / 200.0), 8)) * lerp(0.7, 1.0, smoothstep(0.0, 0.08, min(board, 1.0 - board))); } // planks
else if (id == 25) { k = (0.97 - 0.3 * smoothstep(0.65, 1.0, frac(uv.y * 4.0))) * (0.92 + 0.08 * n1); rough = 0.7; } // roof tiles: overlapping rows
else if (id == 26) { float facet = fn.V(w / 14.0 + face * 9.0, 10); k = 0.85 + 0.55 * smoothstep(0.45, 0.55, facet); rough = 0.15; } // Aetherite crystal
else if (id == 31) { k = (0.94 + 0.06 * n1) * (1.0 - 0.18 * (1.0 - smoothstep(0.0, 0.04, abs(frac(uv.y * 2.0) - 0.5)))); rough = 0.5; } // ceramic
k = lerp(k, 1.0, saturate((D - 3000.0) / 9000.0));
return float4(col * k, rough);
""".replace("DIRT_RATIO", DIRT_RATIO)

# Foliage clusters: each card (UV 0..1, its seed in vertex alpha S) is a lobed clump of leaves with
# a few gaps; light inside, darker at the rim.
LEAVES_CODE = r"""
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
float2 q = UV - 0.5;
float r = length(q) * 2.0;
float ang = atan2(q.y, q.x);
float lobes = 0.8 + 0.12 * sin(ang * 5.0 + S * 40.0) + 0.07 * sin(ang * 11.0 + S * 17.0);
float leaf = fn.V(UV * 9.0 + S * 31.0, 4);
float far = saturate((D - 3500.0) / 2500.0);       // far away: no small gaps (no shimmer)
float opacity = (r < lobes && (leaf > 0.24 || far > 0.5)) ? 1.0 : 0.0;
float hue = fn.V(P.xy / 2200.0, 11);
float3 drift = lerp(float3(0.90, 1.0, 0.82), float3(1.12, 1.02, 0.86), hue);
float k = (0.72 + 0.4 * fn.V(UV * 4.0 + S * 13.0, 6)) * (1.05 - 0.3 * r);
return float4(C * k * drift, opacity);
"""

# Wind: one smooth field over world space, so shared vertices of neighbouring blocks move together.
WIND_CODE = r"""
float s = sin(T * 1.7 + dot(P.xy, float2(0.011, 0.013))) + 0.5 * sin(T * 2.9 + dot(P.xy, float2(-0.021, 0.017)));
return float3(s * 2.2, s * 1.6, 0.0) * Amount;
"""

PLANT_CODE = r"""
// Blades and stems (UV.x = 0): dark at the root, bright at the tip. Corollas (UV.x = 1 + radius):
// a darker heart, the petal colour outward. Colour comes from the vertex (stems green, petals tinted).
if (UV.x > 0.5)
{
    float r = UV.x - 1.0;
    return float4(lerp(Tint.rgb * 0.35 + float3(0.06, 0.04, 0.0), Tint.rgb * 1.05, smoothstep(0.2, 0.5, r)), 1.0);
}
return float4(lerp(Tint.rgb * 0.45, Tint.rgb * 1.08, saturate(UV.y)), 1.0);
"""

WATER_NORMAL_CODE = r"""
bool flowing = C.b >= 0.5;                        // block world: RG flow direction, B speed
float2 dir = flowing ? C.rg * 2.0 - 1.0 : float2(0.0, 0.0);
float speed = flowing ? (C.b - 0.5) * 2.0 : 0.0;
float2 p = P.xy - dir * T * (30.0 + 170.0 * speed); // the ripples travel downstream
float2 d1 = float2(0.8, 0.6), d2 = float2(-0.5, 0.86), d3 = float2(0.2, -0.98);
float2 g = d1 * 0.10 * cos(dot(p, d1) * 0.012 + T * 1.1)
         + d2 * 0.07 * cos(dot(p, d2) * 0.021 + T * 1.6)
         + d3 * 0.05 * cos(dot(p, d3) * 0.047 + T * 2.3);
float2 side = float2(-dir.y, dir.x);              // current streaks along the flow
float along = dot(p, dir), across = dot(p, side);
g += side * (0.05 * speed) * cos(across * 0.11 + 2.0 * sin(along * 0.004));
return normalize(float3(-g, 1.0));
"""

WATER_COLOUR_CODE = r"""
if (C.b >= 0.5)                                   // block world: one continuous river sheet
{
    float deep = saturate(Depth / 300.0);
    float3 river = lerp(float3(0.05, 0.19, 0.18), float3(0.01, 0.06, 0.10), deep);
    // Opaque enough that the block bed never shows its steps; the shoreline fades with the vertex alpha.
    return float4(river, lerp(0.82, 0.95, deep) * Edge);
}
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
m.set_editor_property("two_sided", True)
uvl = node(m, unreal.MaterialExpressionTextureCoordinate)
out = custom(m, LEAVES_CODE, F4, "AethraLeaves", [("P", wp, ""), ("D", d, ""), ("C", vc, ""), ("S", vc, "A"), ("UV", uvl, "")])
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
out = custom(m, PLANT_CODE, F4, "AethraPlantPattern", [("UV", uv, ""), ("Tint", vc, "")])
mel.connect_material_property(mask(m, out, "rgb"), "", MP.MP_BASE_COLOR)
mel.connect_material_property(mask(m, out, "a"), "", MP.MP_OPACITY_MASK)
mel.connect_material_property(constant(m, 0.9), "", MP.MP_ROUGHNESS)
time = node(m, unreal.MaterialExpressionTime)
tip_wind = WIND_CODE.replace("* Amount;", "* UV.y * 2.5;")  # roots stay, tips and corollas sway
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
vc = node(m, unreal.MaterialExpressionVertexColor)
normal = custom(m, WATER_NORMAL_CODE, F3, "AethraWaves", [("P", wp, ""), ("T", time, ""), ("C", vc, "")])
colour = custom(m, WATER_COLOUR_CODE, F4, "AethraWaterColour", [("Depth", behind, ""), ("C", vc, ""), ("Edge", vc, "A")])
mel.connect_material_property(normal, "", MP.MP_NORMAL)
mel.connect_material_property(mask(m, colour, "rgb"), "", MP.MP_BASE_COLOR)
mel.connect_material_property(mask(m, colour, "a"), "", MP.MP_OPACITY)
mel.connect_material_property(constant(m, 0.04), "", MP.MP_ROUGHNESS)
mel.connect_material_property(constant(m, 0.8), "", MP.MP_SPECULAR)
save(full, m)
