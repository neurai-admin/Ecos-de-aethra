"""Imports the original ECOS DE AETHRA fonts (SourceArt/Fonts, made by Tools/Fonts/make_aethra_fonts.py)
as Font Face assets in /Game/UI/Fonts. The game builds its UFonts from these faces (AethraHUD).

Run headless:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="Tools/Editor/import_aethra_fonts.py" -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi
"""
import os

import unreal

PATH = "/Game/UI/Fonts"
FACES = [("AethraPixel-Regular.ttf", "FF_AethraPixel_Regular"), ("AethraPixel-Bold.ttf", "FF_AethraPixel_Bold"),
         ("AethraRuna-Regular.ttf", "FF_AethraRuna_Regular")]
eal = unreal.EditorAssetLibrary
source = os.path.join(unreal.Paths.project_dir(), "SourceArt", "Fonts")

for file, name in FACES:
    full = f"{PATH}/{name}"
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(source, file))
    task.set_editor_property("destination_path", PATH)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    face = eal.load_asset(full)
    if not isinstance(face, unreal.FontFace):
        raise RuntimeError(f"{file} did not import as a FontFace: {face}")
    # Pixel fonts: no hinting (it would bend the square pixels) and the data inside the asset.
    face.set_editor_property("hinting", unreal.FontHinting.NONE)
    face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
    if not eal.save_asset(full, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {full}")
    unreal.log(f"AETHRA_FONTS: imported {full}")

unreal.SystemLibrary.quit_editor()  # when run in the full editor (-ExecutePythonScript)
