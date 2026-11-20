"""Imports the Elden villager (Tools/Blender/make_villager.py output) into /Game/Characters/Villager.

Run headless (idempotent; replaces existing assets):
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="Tools/Editor/import_villager.py" -EnablePlugins=PythonScriptPlugin -unattended -nosplash

M_Villager: one opaque material for the body and every accessory. UV0.x of each vertex holds its
colour region (0.5 = region 0, 1.5 = region 1, ...); the material returns parameter ColorN for
region N. The game gives each villager a dynamic instance with its own colours, so a villager is
one mesh section (one draw call) plus its hat and tool.
"""
import os

import unreal

PATH = "/Game/Characters/Villager"
SOURCE = os.path.join(unreal.Paths.project_dir(), "SourceArt", "Villager")
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty

DEFAULTS = [  # skin, tunic, trousers, leather, hair, pupil, eye white, accent
    (0.62, 0.40, 0.27), (0.20, 0.30, 0.06), (0.10, 0.07, 0.04), (0.10, 0.04, 0.015),
    (0.07, 0.03, 0.01), (0.01, 0.01, 0.012), (0.85, 0.85, 0.8), (0.6, 0.45, 0.15),
]

PICK = """
int i = clamp((int)floor(Idx), 0, 7);
float3 c[8] = { C0, C1, C2, C3, C4, C5, C6, C7 };
return c[i];
"""


def node(material, cls, x, y):
    return mel.create_material_expression(material, cls, x, y)


def build_material():
    full = f"{PATH}/M_Villager"
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    m = tools.create_asset("M_Villager", PATH, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    idx = node(m, unreal.MaterialExpressionComponentMask, -700, 0)
    for channel in "rgba":
        idx.set_editor_property(channel, channel == "r")
    mel.connect_material_expressions(uv, "", idx, "")
    pick = node(m, unreal.MaterialExpressionCustom, -300, 0)
    pick.set_editor_property("code", PICK)
    pick.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pick.set_editor_property("description", "VillagerRegion")
    entries = []
    for name in ["Idx"] + [f"C{i}" for i in range(8)]:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        entries.append(entry)
    pick.set_editor_property("inputs", entries)
    mel.connect_material_expressions(idx, "", pick, "Idx")
    for i, colour in enumerate(DEFAULTS):
        p = node(m, unreal.MaterialExpressionVectorParameter, -700, 150 + 120 * i)
        p.set_editor_property("parameter_name", f"Color{i}")
        p.set_editor_property("default_value", unreal.LinearColor(*colour, 1.0))
        mel.connect_material_expressions(p, "", pick, f"C{i}")
    mel.connect_material_property(pick, "", MP.MP_BASE_COLOR)
    rough = node(m, unreal.MaterialExpressionScalarParameter, -300, 300)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.72)
    mel.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    spec = node(m, unreal.MaterialExpressionConstant, -300, 400)
    spec.set_editor_property("r", 0.35)
    mel.connect_material_property(spec, "", MP.MP_SPECULAR)
    mel.recompile_material(m)
    eal.save_asset(full, only_if_is_dirty=False)
    unreal.log(f"AETHRA: created {full}")
    return m


def import_fbx(name, skeletal):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", skeletal)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("create_physics_asset", False)
    if skeletal:
        data = ui.get_editor_property("skeletal_mesh_import_data")
        data.set_editor_property("import_morph_targets", False)
    else:
        data = ui.get_editor_property("static_mesh_import_data")
        data.set_editor_property("combine_meshes", True)
        data.set_editor_property("generate_lightmap_u_vs", False)
        data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    data.set_editor_property("convert_scene", True)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(SOURCE, f"{name}.fbx"))
    task.set_editor_property("destination_path", PATH)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    tools.import_asset_tasks([task])
    asset = eal.load_asset(f"{PATH}/{name}")
    if not asset:
        raise RuntimeError(f"AETHRA: import failed for {name}")
    return asset


def main():
    # The legacy FBX importer honours FbxImportUI (Interchange would ignore these options).
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
    material = build_material()
    body = import_fbx("SK_Villager", True)
    body.set_editor_property("materials", [unreal.SkeletalMaterial(material_interface=material, material_slot_name="Villager")])
    eal.save_asset(body.get_path_name(), only_if_is_dirty=False)
    skeleton = body.get_editor_property("skeleton")
    if skeleton:
        eal.save_asset(skeleton.get_path_name(), only_if_is_dirty=False)
    unreal.log(f"AETHRA: imported {body.get_path_name()} skeleton {skeleton.get_path_name() if skeleton else None}")
    for fbx in sorted(os.listdir(SOURCE)):
        if fbx.startswith("SM_") and fbx.endswith(".fbx"):
            mesh = import_fbx(fbx[:-4], False)
            mesh.set_material(0, material)
            eal.save_asset(mesh.get_path_name(), only_if_is_dirty=False)
            unreal.log(f"AETHRA: imported {mesh.get_path_name()}")
    unreal.log("AETHRA_IMPORT_OK")


main()
