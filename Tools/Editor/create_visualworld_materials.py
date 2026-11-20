"""Visual World materials (/Game/VisualWorld), built from scratch and idempotent. No texture files:
surfaces are numbers in a palette texture the game creates at runtime from the AME registry
(FAmeRegistry::CreatePalette) and HydroGen (FHydroGen::CreatePalette). T_PaletteDefault is only the
parameter default the material compiler needs (a 64x4 linear PNG, regenerated here).

Run headless:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="Tools/Editor/create_visualworld_materials.py" -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi

Palette contract (AethraMaterialEngine.h): column = surface id; row 0 base rgb + variation,
row 1 accent rgb + wear, row 2 roughness, metallic, pattern, scale.
Vertex contract: UV1 = (surface A, surface B); vertex colour R occlusion, G wetness, A blend to B.
Instances: custom data 0 = tint, custom data 1 = surface + 1 (0 = use UV1).
Water palette (AethraHydroGen.h): column = body; row 0 absorption + current speed, row 1 scattering +
choppiness, row 2 in-scatter + visible depth. Water vertices: UV0 flow (m/s), UV1.x body,
colour R depth/16 m, G speed/4, B shore foam.
"""
import os
import struct
import zlib

import unreal

PATH = "/Game/VisualWorld"
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty
F1, F3, F4 = (unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
              unreal.CustomMaterialOutputType.CMOT_FLOAT4)

COMMON = r"""
struct FAethraVW
{
    float H(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
    float V(float2 p)
    {
        float2 i = floor(p), f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(H(i), H(i + float2(1, 0)), f.x), lerp(H(i + float2(0, 1)), H(i + float2(1, 1)), f.x), f.y);
    }
    float F(float2 p) { return 0.5 * V(p) + 0.3 * V(p * 2.03 + 7.1) + 0.2 * V(p * 4.11 + 3.3); }
    // Voronoi: x = distance to the nearest cell centre, y = to the second, z = cell hash.
    float3 Cells(float2 p)
    {
        float2 i = floor(p);
        float d1 = 8.0, d2 = 8.0, id = 0.0;
        for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
        {
            float2 c = i + float2(x, y);
            float2 o = float2(H(c), H(c + 19.7));
            float d = length(c + o - p);
            if (d < d1) { d2 = d1; d1 = d; id = H(c + 3.1); }
            else if (d < d2) { d2 = d; }
        }
        return float3(d1, d2, id);
    }
};
"""

SURFACE = COMMON + r"""
FAethraVW fn;
int a = (int)round(UV.x);
int b = (int)round(UV.y);
bool terrain = UV2.x > 0.5;                 // terrain: albedo (rgb) and roughness (a) come resolved per vertex
float blend = 0.0;
if (CD1 > 0.5) { a = (int)round(CD1 - 1.0); b = a; }
float3 P = WP / 100.0;
float3 n = abs(N);
// Plane for 2D patterns: walls use (along, up), floors (x, y).
float2 q = n.z > max(n.x, n.y) ? P.xy : (n.x > n.y ? float2(P.y, P.z) : float2(P.x, P.z));
float3 outC[2];
float outR[2];
float outH[2];
int ids[2] = { a, b };
[unroll] for (int k = 0; k < 2; k++)
{
    int id = clamp(ids[k], 0, 63);
    float4 r0 = Palette.Load(int3(id, 0, 0));
    float4 r1 = Palette.Load(int3(id, 1, 0));
    float4 r2 = Palette.Load(int3(id, 2, 0));
    float3 base = r0.rgb, acc = r1.rgb;
    float var = r0.a, wear = r1.a, rough = r2.r;
    int pat = (int)round(r2.b);
    float s = r2.a;
    float m = 0.0, occ = 1.0;
    float2 u = q * s;
    if (pat == 0) { m = fn.F(u * 2.0); }
    else if (pat == 1 || pat == 2) // grass, meadow
    {
        m = fn.F(u * 3.0) * 0.7 + fn.V(float2(u.x * 9.0, u.y * 9.0)) * 0.3;
        if (pat == 2) { float2 c = floor(u * 6.0); if (fn.H(c) > 0.965) { float2 f = frac(u * 6.0) - 0.5; if (length(f) < 0.22) { base = float3(0.9, 0.86, 0.75) * (0.7 + 0.6 * fn.H(c + 1.3)); m = 0.0; } } }
    }
    else if (pat == 3) { m = 0.5 + 0.5 * sin(u.x * 5.0 + fn.V(u * 0.7) * 6.0); m = m * 0.6 + fn.H(floor(u * 30.0)) * 0.4; }
    else if (pat == 4) { float3 c = fn.Cells(u * 2.0); m = c.z; occ = lerp(0.55, 1.0, saturate((c.y - c.x) * 6.0)); }
    else if (pat == 5) // strata: bands along height, wavy
    {
        float band = frac(P.z * s * 1.3 + fn.V(P.xy * 0.15) * 1.7);
        m = smoothstep(0.35, 0.5, band) * (1.0 - smoothstep(0.85, 0.98, band));
        occ = lerp(0.75, 1.0, smoothstep(0.0, 0.06, band));
        m = m * 0.8 + fn.F(u * 4.0) * 0.2;
    }
    else if (pat == 6) { m = fn.F(u * 3.0) * 0.5 + step(0.82, fn.H(floor(u * 40.0))) * 0.5; }
    else if (pat == 7) { float3 c = fn.Cells(u * 1.5); m = c.z * 0.6; occ = lerp(0.45, 1.0, saturate((c.y - c.x) * 8.0)); }
    else if (pat == 8) { m = fn.F(u * 1.5) * 0.4; base += step(0.985, fn.H(floor(u * 60.0))) * 0.15; }
    else if (pat == 9) { m = smoothstep(0.3, 0.8, fn.F(u * 2.0)); }
    else if (pat == 10) // bark: vertical streaks
    {
        float2 b2 = float2(atan2(P.y, P.x) * 1.5 + P.x * 3.0, P.z * 0.6);
        m = fn.V(float2(b2.x * s * 2.0, b2.y * s * 0.3));
        occ = lerp(0.6, 1.0, smoothstep(0.2, 0.5, fn.V(float2((P.x + P.y) * s * 6.0, P.z * s))));
    }
    else if (pat == 11) { float3 c = fn.Cells(P.xy * s * 1.2 + P.z * 0.7); m = c.z; occ = lerp(0.6, 1.0, saturate(c.x * 2.2)); }
    else if (pat == 12) // planks
    {
        float row = floor(u.y * s * 0.25 * 4.0);
        float seam = frac(u.y * s * 0.25 * 4.0);
        m = fn.V(float2(u.x * 0.6, row * 3.1)) * 0.6 + fn.V(float2(u.x * 8.0, u.y * 40.0)) * 0.2 + fn.H(float2(row, 1.0)) * 0.2;
        occ = lerp(0.55, 1.0, smoothstep(0.0, 0.06, seam) * smoothstep(1.0, 0.94, seam));
    }
    else if (pat == 13 || pat == 14) // cut stone, brick
    {
        float2 size = pat == 13 ? float2(0.75, 0.38) : float2(0.24, 0.07);
        float2 g = q / size;
        g.x += 0.5 * fmod(floor(g.y), 2.0);
        float2 f = frac(g), cell = floor(g);
        float joint = pat == 13 ? 0.035 : 0.12;
        float edge = min(min(f.x, 1.0 - f.x) * size.x / size.y, min(f.y, 1.0 - f.y));
        float mortar = 1.0 - smoothstep(joint * 0.6, joint, edge);
        m = mortar;
        base *= 0.88 + 0.24 * fn.H(cell);
        occ = lerp(1.0, 0.7, mortar) * lerp(0.9, 1.0, fn.F(q * 3.0));
    }
    else if (pat == 15) // roof tiles in rows, scalloped
    {
        float2 g = float2(q.x * 3.0, q.y * 4.0);
        g.x += 0.5 * fmod(floor(g.y), 2.0);
        float2 f = frac(g);
        float scallop = length(float2(f.x - 0.5, f.y * 0.8));
        m = smoothstep(0.42, 0.5, scallop);
        base *= 0.85 + 0.3 * fn.H(floor(g));
        occ = lerp(0.6, 1.0, smoothstep(0.0, 0.25, f.y));
    }
    else if (pat == 16) { m = smoothstep(0.55, 0.85, fn.F(u * 0.8)) * 0.6; occ = lerp(0.9, 1.0, fn.V(u * 6.0)); }
    else if (pat == 17) { m = smoothstep(0.55, 0.9, fn.F(u * 1.2)); base *= 0.9 + 0.2 * fn.V(float2(u.x * 40.0, u.y * 1.0)); }
    else // ore: host rock with veins of the accent
    {
        float v = abs(fn.V(u * 1.4) - 0.5);
        m = 1.0 - smoothstep(0.02, 0.07, v);
        m = max(m, step(0.9, fn.H(floor(u * 8.0))));
    }
    float drift = fn.V(P.xy / 14.0) - 0.5;
    float3 col = lerp(base, acc, saturate(m)) * (1.0 + var * drift * 1.6) * occ;
    // Exposure and edges: upward faces of worn materials bleach a little.
    col = lerp(col, col * 1.12 + 0.03, wear * saturate(N.z) * 0.4);
    outC[k] = col;
    outR[k] = saturate(rough + (1.0 - occ) * 0.15);
    // Relief for the bump: joints and cavities sink, the pattern's accent rises, a fine grain on top
    // (no surface is perfectly smooth: the anti-"plasticine" rule).
    outH[k] = (saturate(m) - 0.5) * 0.6 - (1.0 - occ) + (fn.V(P.xy * 23.0 + P.z * 17.0) - 0.5) * 0.35;
}
float3 c = lerp(outC[0], outC[1], saturate(blend));
float r = lerp(outR[0], outR[1], saturate(blend));
float4 r3 = Palette.Load(int3(clamp(a, 0, 63), 3, 0));
Hgt = lerp(outH[0], outH[1], saturate(blend)) * r3.g;
Glw = r3.b * (0.75 + 0.5 * fn.V(P.xy * 0.7 + P.z)); // enchanted surfaces only (0 elsewhere)
if (terrain)
{
    // Keep the vertex albedo; the pattern only modulates brightness (so a stray interpolated id
    // between two surfaces changes texture detail, never colour).
    int id = clamp(a, 0, 63);
    float3 pal = Palette.Load(int3(id, 0, 0)).rgb;
    float lumPal = max(dot(pal, float3(0.3, 0.59, 0.11)), 1e-3);
    float factor = clamp(dot(c, float3(0.3, 0.59, 0.11)) / lumPal, 0.45, 1.6);
    c = VC.rgb * factor;
    r = saturate(VC.a + (r - Palette.Load(int3(id, 2, 0)).r));
}
else
{
    c *= VC.r;                    // instanced pieces and trees: vertex colour is white unless baked
}
c *= 1.0 + CD0 * 0.3;             // per-instance tint
return float4(c, r);
"""

# Tree sway: per-instance wind strength (custom data 2, 0 for terrain and pieces) times the square of
# the vertex height fraction (UV0.y from the flora mesh); the tint (custom data 0) dephases trees.
TREEWIND = r"""
float s = CD2 * H * H * 18.0;
float ph = CD0 * 23.0;
return float3(sin(Tm * 1.05 + ph) * s, cos(Tm * 0.83 + ph * 1.7) * s * 0.6, 0.0);
"""

# Bump from the procedural relief (height in cm, from AethraSurface's Hgt output), with screen-space
# derivatives (surface-gradient bump): no normal map textures. Returns a world-space normal.
BUMP = r"""
float3 n = normalize(N);
float3 dpdx = ddx(WP), dpdy = ddy(WP);
float dhx = ddx(Hgt), dhy = ddy(Hgt);
float3 r1 = cross(dpdy, n), r2 = cross(n, dpdx);
float det = dot(dpdx, r1);
float3 g = sign(det) * (dhx * r1 + dhy * r2);
float3 b = abs(det) * n - g;
return dot(b, b) > 1e-12 ? normalize(b) : n;
"""

METAL = r"""
int a = (int)round(CD1 > 0.5 ? CD1 - 1.0 : UV.x);
return Palette.Load(int3(clamp(a, 0, 63), 2, 0)).g;
"""

FOLIAGE = COMMON + r"""
FAethraVW fn;
int a = clamp((int)round(UV.x), 0, 63);
float4 r0 = Palette.Load(int3(a, 0, 0));
float4 r1 = Palette.Load(int3(a, 1, 0));
float3 P = WP / 100.0;
float3 col = lerp(r0.rgb, r1.rgb, saturate(VC.r * 0.9 + fn.V(P.xy * 1.3) * 0.3));
col *= 1.0 + (fn.V(P.xy / 9.0) - 0.5) * r0.a * 1.5;
col *= lerp(0.8, 1.05, VC.r);                 // a little darker at the base of the blade
if (VC.g > 0.5 && CD1 > 0.5)                  // flower petals
{
    int f = (int)round(CD1);
    col = f == 1 ? float3(0.92, 0.9, 0.84) : f == 2 ? float3(0.75, 0.06, 0.05) : f == 3 ? float3(0.95, 0.72, 0.08) : float3(0.45, 0.22, 0.75);
}
return col * 1.12 * (1.0 + CD0 * 0.3); // matches the terrain grass where the blades end
"""

WIND = r"""
float w = sin(T * 1.9 + WP.x * 0.011 + WP.y * 0.013) + 0.4 * sin(T * 3.7 + WP.x * 0.03);
return float3(w * 5.0, w * 3.0, 0.0) * VC.r;
"""

HYDRO = COMMON + r"""
FAethraVW fn;
int id = clamp((int)round(UV1.x), 0, 15);
float4 r0 = Water.Load(int3(id, 0, 0));
float4 r1 = Water.Load(int3(id, 1, 0));
float4 r2 = Water.Load(int3(id, 2, 0));
float3 sigma = r0.rgb + r1.rgb;
float chop = r1.a;
float3 ins = r2.rgb;
// Optical path to the bottom along the view ray (metres), from the scene depth behind the surface.
float path = max(SceneZ - PixelZ, 0.0) / 100.0;
float vertical = path * max(abs(V.z), 0.15);
float3 T = exp(-sigma * path) * exp(-sigma * vertical); // down along the ray, light back up
// Waves: a few directional sines carried by the current.
float2 p = WP.xy / 100.0 - Flow * Tm;
float2 g = 0.0;
float2 dirs[4] = { float2(0.8, 0.6), float2(-0.5, 0.86), float2(0.3, -0.95), float2(-0.9, -0.2) };
float freq[4] = { 0.9, 1.7, 2.9, 4.6 };
[unroll] for (int k = 0; k < 4; k++)
{
    float ph = dot(dirs[k], p) * freq[k] + Tm * (1.2 + k * 0.7);
    g += dirs[k] * cos(ph) * freq[k] * (0.05 / (k + 1));
}
g += (float2(fn.V(p * 3.0 + Tm * 0.3), fn.V(p * 3.0 - Tm * 0.27)) - 0.5) * 0.25;
g *= 0.25 + chop;
float3 Nw = normalize(float3(-g.x, -g.y, 1.0));
// Foam: shallow edges and fast water, broken up by noise.
float speed = VC.g * 4.0;
float shore = saturate(1.0 - vertical / 0.35);
float foamNoise = fn.F(p * 4.0 + Tm * 0.4);
float foam = saturate(shore * 1.3 - foamNoise * 0.6) + saturate((speed - 1.2) * 0.4) * smoothstep(0.55, 0.8, foamNoise);
foam = saturate(foam);
return float4(Nw, foam);
"""

HYDRO_COLOR = r"""
int id = clamp((int)round(UV1.x), 0, 15);
float4 r0 = Water.Load(int3(id, 0, 0));
float4 r1 = Water.Load(int3(id, 1, 0));
float4 r2 = Water.Load(int3(id, 2, 0));
float3 sigma = r0.rgb + r1.rgb;
float path = max(SceneZ - PixelZ, 0.0) / 100.0;
float vertical = path * max(abs(V.z), 0.15);
float3 T = exp(-sigma * path) * exp(-sigma * vertical);
float3 c = Scene * T + r2.rgb * (1.0 - T) * Light;
c = lerp(c, float3(0.9, 0.93, 0.95) * Light, Foam * 0.85);
return c;
"""

FALL = COMMON + r"""
FAethraVW fn;
float2 uv = UV0;
float t = Tm;
float streak = fn.V(float2(uv.x * 14.0, uv.y * 1.5 - t * 3.2)) * 0.6 + fn.V(float2(uv.x * 40.0, uv.y * 4.0 - t * 5.0)) * 0.4;
float edge = smoothstep(0.0, 0.12, uv.x) * smoothstep(1.0, 0.88, uv.x);
float a = saturate(streak * 1.4 - 0.15) * edge;
float bottom = VC.r; // 0 at the lip, 1 at the pool
float foam = smoothstep(0.55, 1.0, bottom) * 0.6 + 0.4 * streak;
return float4(lerp(float3(0.55, 0.7, 0.75), float3(0.95, 0.97, 1.0), foam), saturate(a * 0.85 + bottom * 0.25));
"""

MIST = COMMON + r"""
FAethraVW fn;
float3 p = WP / 100.0;
float n = fn.F(p.xy * 0.35 + float2(Tm * 0.15, -Tm * 0.11) + p.z * 0.2);
float soft = pow(saturate(abs(dot(normalize(Nrm), normalize(V)))), 1.5);
return saturate(n * 1.6 - 0.45) * soft * 0.35;
"""


def node(m, cls, x=-800, y=0):
    return mel.create_material_expression(m, cls, x, y)


def custom(m, code, output, desc, inputs, x=-400, y=0, extra=()):
    e = node(m, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property("code", code)
    e.set_editor_property("output_type", output)
    e.set_editor_property("description", desc)
    if extra:  # additional outputs: the code assigns them by name
        outs = []
        for name, kind in extra:
            o = unreal.CustomOutput()
            o.set_editor_property("output_name", name)
            o.set_editor_property("output_type", kind)
            outs.append(o)
        e.set_editor_property("additional_outputs", outs)
    entries = []
    for name, _, _ in inputs:
        c = unreal.CustomInput()
        c.set_editor_property("input_name", name)
        entries.append(c)
    e.set_editor_property("inputs", entries)
    for name, src, pin in inputs:
        mel.connect_material_expressions(src, pin, e, name)
    return e


def mask(m, src, channels, x=-600, y=0):
    e = node(m, unreal.MaterialExpressionComponentMask, x, y)
    for c in "rgba":
        e.set_editor_property(c, c in channels)
    mel.connect_material_expressions(src, "", e, "")
    return e


def const(m, v):
    e = node(m, unreal.MaterialExpressionConstant, -300, 400)
    e.set_editor_property("r", v)
    return e


def scalar(m, name, v):
    e = node(m, unreal.MaterialExpressionScalarParameter, -900, 500)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", v)
    return e


def write_png(path, w, h):
    raw = b"".join(b"\x00" + bytes([128, 128, 128, 255]) * w for _ in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def default_palette():
    full = f"{PATH}/T_PaletteDefault"
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    png = os.path.join(unreal.Paths.project_saved_dir(), "T_PaletteDefault.png")
    write_png(png, 64, 4)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", PATH)
    task.set_editor_property("destination_name", "T_PaletteDefault")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    tex = eal.load_asset(full)
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    eal.save_asset(full, only_if_is_dirty=False)
    return tex


def new_material(name):
    full = f"{PATH}/{name}"
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    return full, tools.create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())


def texobj(m, name, tex):
    e = node(m, unreal.MaterialExpressionTextureObjectParameter, -1100, -200)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", tex)
    e.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return e


def save(full, m):
    mel.recompile_material(m)
    if not eal.save_asset(full, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {full}")
    unreal.log(f"AETHRA_VW: created {full}")


def instance_data(m, index, x, y):
    e = node(m, unreal.MaterialExpressionPerInstanceCustomData, x, y)
    e.set_editor_property("data_index", index)
    return e


def main():
    tex = default_palette()

    # Surface: terrain, rocks, bark, leaves, architecture.
    full, m = new_material("M_AethraSurface")
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    pal = texobj(m, "Palette", tex)
    uv1 = node(m, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    uv1.set_editor_property("coordinate_index", 1)
    vcol = node(m, unreal.MaterialExpressionVertexColor, -1300, 150)
    vc = node(m, unreal.MaterialExpressionAppendVector, -1100, 150) # the default output is rgb only
    mel.connect_material_expressions(vcol, "", vc, "A")
    mel.connect_material_expressions(vcol, "A", vc, "B")
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1100, 300)
    nrm = node(m, unreal.MaterialExpressionVertexNormalWS, -1100, 450)
    cd0, cd1 = instance_data(m, 0, -1100, 600), instance_data(m, 1, -1100, 700)
    uv2 = node(m, unreal.MaterialExpressionTextureCoordinate, -1300, 0)
    uv2.set_editor_property("coordinate_index", 2)
    out = custom(m, SURFACE, F4, "AethraSurface", [("Palette", pal, ""), ("UV", mask(m, uv1, "rg"), ""), ("UV2", mask(m, uv2, "rg"), ""), ("VC", vc, ""), ("WP", wp, ""), ("N", nrm, ""), ("CD0", cd0, ""), ("CD1", cd1, "")],
                 extra=[("Hgt", F1), ("Glw", F1)])
    glow = node(m, unreal.MaterialExpressionMultiply, -150, 450)
    mel.connect_material_expressions(mask(m, out, "rgb", -300, 450), "", glow, "A")
    mel.connect_material_expressions(out, "Glw", glow, "B")
    mel.connect_material_property(glow, "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask(m, out, "rgb", -150, 0), "", MP.MP_BASE_COLOR)
    mel.connect_material_property(mask(m, out, "a", -150, 150), "", MP.MP_ROUGHNESS)
    m.set_editor_property("tangent_space_normal", False)
    bump = custom(m, BUMP, F3, "AethraBump", [("Hgt", out, "Hgt"), ("WP", wp, ""), ("N", nrm, "")], -250, 250)
    mel.connect_material_property(bump, "", MP.MP_NORMAL)
    metal = custom(m, METAL, F1, "AethraMetal", [("Palette", pal, ""), ("UV", mask(m, uv1, "rg"), ""), ("CD1", cd1, "")], -400, 300)
    mel.connect_material_property(metal, "", MP.MP_METALLIC)
    mel.connect_material_property(const(m, .4), "", MP.MP_SPECULAR)
    uv0 = node(m, unreal.MaterialExpressionTextureCoordinate, -1300, 900)
    cd2 = instance_data(m, 2, -1100, 800)
    tsw = node(m, unreal.MaterialExpressionTime, -1100, 900)
    sway = custom(m, TREEWIND, F3, "AethraTreeWind", [("Tm", tsw, ""), ("H", mask(m, uv0, "g"), ""), ("CD0", cd0, ""), ("CD2", cd2, "")], -400, 600)
    mel.connect_material_property(sway, "", MP.MP_WORLD_POSITION_OFFSET)
    save(full, m)

    # Foliage: grass blades and flowers, two-sided, with wind.
    full, m = new_material("M_AethraFoliage")
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    pal = texobj(m, "Palette", tex)
    uv1 = node(m, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    uv1.set_editor_property("coordinate_index", 1)
    vc = node(m, unreal.MaterialExpressionVertexColor, -1100, 150)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1100, 300)
    cd0, cd1 = instance_data(m, 0, -1100, 600), instance_data(m, 1, -1100, 700)
    col = custom(m, FOLIAGE, F3, "AethraFoliage", [("Palette", pal, ""), ("UV", mask(m, uv1, "rg"), ""), ("VC", vc, ""), ("WP", wp, ""), ("CD0", cd0, ""), ("CD1", cd1, "")])
    mel.connect_material_property(col, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(const(m, .78), "", MP.MP_ROUGHNESS)
    # Blades are lit like the ground they grow from (world up normal): no black backlit blades and no
    # visible edge where the grass ends; the colour variation still separates them.
    m.set_editor_property("tangent_space_normal", False)
    up = node(m, unreal.MaterialExpressionConstant3Vector, -300, 600)
    up.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    mel.connect_material_property(up, "", MP.MP_NORMAL)
    tm = node(m, unreal.MaterialExpressionTime, -1100, 800)
    wind = custom(m, WIND, F3, "AethraWind", [("T", tm, ""), ("WP", wp, ""), ("VC", vc, "")], -400, 400)
    mel.connect_material_property(wind, "", MP.MP_WORLD_POSITION_OFFSET)
    save(full, m)

    # Water: optics from the body's palette column, depth from the scene behind the surface.
    full, m = new_material("M_AethraHydro")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Forward-shaded translucent surface (sun specular and sky reflections per pixel); name varies by version.
    modes = [n for n in dir(unreal.TranslucencyLightingMode) if n.startswith("TLM_")]
    unreal.log(f"AETHRA_VW: translucency modes {modes}")
    pick = next((n for n in ("TLM_SURFACE_FORWARD_SHADING", "TLM_SURFACE", "TLM_SURFACE_PER_PIXEL_LIGHTING") if n in modes), None)
    if pick:
        m.set_editor_property("translucency_lighting_mode", getattr(unreal.TranslucencyLightingMode, pick))
    m.set_editor_property("tangent_space_normal", False)
    wat = texobj(m, "Water", tex)
    uv0 = node(m, unreal.MaterialExpressionTextureCoordinate, -1100, -50)
    uv1 = node(m, unreal.MaterialExpressionTextureCoordinate, -1100, 50)
    uv1.set_editor_property("coordinate_index", 1)
    vc = node(m, unreal.MaterialExpressionVertexColor, -1100, 150)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1100, 250)
    tm = node(m, unreal.MaterialExpressionTime, -1100, 350)
    sz = node(m, unreal.MaterialExpressionSceneDepth, -1100, 450)
    pz = node(m, unreal.MaterialExpressionPixelDepth, -1100, 550)
    cam = node(m, unreal.MaterialExpressionCameraVectorWS, -1100, 650)
    sc = node(m, unreal.MaterialExpressionSceneColor, -1100, 750)
    light = scalar(m, "Light", 1.0)
    wave = custom(m, HYDRO, F4, "AethraHydro", [("Water", wat, ""), ("UV1", mask(m, uv1, "rg"), ""), ("Flow", mask(m, uv0, "rg"), ""), ("VC", vc, ""),
                                                ("WP", wp, ""), ("Tm", tm, ""), ("SceneZ", mask(m, sz, "r"), ""), ("PixelZ", pz, ""), ("V", cam, "")])
    colour = custom(m, HYDRO_COLOR, F3, "AethraHydroColor", [("Water", wat, ""), ("UV1", mask(m, uv1, "rg"), ""), ("SceneZ", mask(m, sz, "r"), ""),
                                                            ("PixelZ", pz, ""), ("V", cam, ""), ("Scene", mask(m, sc, "rgb"), ""), ("Light", light, ""), ("Foam", mask(m, wave, "a"), "")], -400, 300)
    mel.connect_material_property(mask(m, wave, "rgb", -150, 0), "", MP.MP_NORMAL)
    mel.connect_material_property(colour, "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(const(m, 0.0), "", MP.MP_BASE_COLOR)
    mel.connect_material_property(const(m, 1.0), "", MP.MP_OPACITY)
    mel.connect_material_property(const(m, .05), "", MP.MP_ROUGHNESS)
    mel.connect_material_property(const(m, .5), "", MP.MP_SPECULAR)
    save(full, m)

    # Waterfall curtain: scrolling streaks, foam toward the pool.
    full, m = new_material("M_AethraFall")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    uv0 = node(m, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    vc = node(m, unreal.MaterialExpressionVertexColor, -1100, 150)
    tm = node(m, unreal.MaterialExpressionTime, -1100, 300)
    light = scalar(m, "Light", 1.0)
    out = custom(m, FALL, F4, "AethraFall", [("UV0", mask(m, uv0, "rg"), ""), ("VC", vc, ""), ("Tm", tm, "")])
    lit = node(m, unreal.MaterialExpressionMultiply, -150, 0)
    mel.connect_material_expressions(mask(m, out, "rgb"), "", lit, "A")
    mel.connect_material_expressions(light, "", lit, "B")
    mel.connect_material_property(lit, "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask(m, out, "a", -150, 200), "", MP.MP_OPACITY)
    save(full, m)

    # Mist: soft, unlit puffs.
    full, m = new_material("M_AethraMist")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1100, 0)
    tm = node(m, unreal.MaterialExpressionTime, -1100, 150)
    nrm = node(m, unreal.MaterialExpressionVertexNormalWS, -1100, 300)
    cam = node(m, unreal.MaterialExpressionCameraVectorWS, -1100, 450)
    light = scalar(m, "Light", 1.0)
    alpha = custom(m, MIST, F1, "AethraMist", [("WP", wp, ""), ("Tm", tm, ""), ("Nrm", nrm, ""), ("V", cam, "")])
    fade = node(m, unreal.MaterialExpressionDepthFade, -150, 200)
    mel.connect_material_expressions(alpha, "", fade, "Opacity")
    mel.connect_material_property(fade, "", MP.MP_OPACITY)
    white = node(m, unreal.MaterialExpressionMultiply, -150, 0)
    mel.connect_material_expressions(const(m, .95), "", white, "A")
    mel.connect_material_expressions(light, "", white, "B")
    mel.connect_material_property(white, "", MP.MP_EMISSIVE_COLOR)
    save(full, m)
    unreal.log("AETHRA_VW_MATERIALS_OK")


main()
