"""Elden villager: one reusable rigged base body plus profession accessories, for ECOS DE AETHRA.

Original geometry made of bevelled, softly shaded blocks (voxel world with a modern finish).
No third-party assets. Run headless:
  blender -b --factory-startup --python Tools/Blender/make_villager.py -- <output dir>
Writes SK_Villager.fbx (skinned body, 18 bones), one FBX per accessory and preview.png.

Conventions: centimetres (unit scale 0.01), the character faces -Y, its left is +X, feet at Z = 0.
Every part is skinned 100% to one bone (rigid, voxel style), so joints never stretch.
Colour regions are not material slots: UV0.x holds the region index (0.5, 1.5, ...), and one
material picks the colour from per-NPC parameters. One mesh section = one draw call per villager.
Accessories are modelled in character space; the game attaches them to a bone at runtime.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.path.join(os.getcwd(), "SourceArt", "Villager")
os.makedirs(OUT, exist_ok=True)

# Colour regions of the body (UV0.x index). Must match AethraLisNpc.cpp.
REGIONS = ["Skin", "Tunic", "Trousers", "Leather", "Hair", "Pupil", "EyeWhite", "Accent"]
R = {name: i for i, name in enumerate(REGIONS)}

# Bones: name, head, tail, parent. Z up, cm.
BONES = [
    ("root", (0, 0, 0), (0, -10, 0), None),
    ("pelvis", (0, 0, 76), (0, 0, 92), "root"),
    ("spine", (0, 0, 92), (0, 0, 106), "pelvis"),
    ("chest", (0, 0, 106), (0, 0, 130), "spine"),
    ("neck", (0, 0, 130), (0, 0, 135), "chest"),
    ("head", (0, 0, 135), (0, 0, 171), "neck"),
]
for side, sx in (("l", 1), ("r", -1)):
    BONES += [
        (f"upperarm_{side}", (22 * sx, 0, 127), (22 * sx, 0, 101), "chest"),
        (f"lowerarm_{side}", (22 * sx, 0, 101), (22 * sx, 0, 78), f"upperarm_{side}"),
        (f"hand_{side}", (22 * sx, 0, 78), (22 * sx, 0, 66), f"lowerarm_{side}"),
        (f"thigh_{side}", (8 * sx, 0, 74), (8 * sx, 0, 42), "pelvis"),
        (f"calf_{side}", (8 * sx, 0, 42), (8 * sx, 0, 11), f"thigh_{side}"),
        (f"foot_{side}", (8 * sx, 0, 11), (8 * sx, -12, 3), f"calf_{side}"),
    ]
BONE_INDEX = {b[0]: i for i, b in enumerate(BONES)}


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.01  # 1 unit = 1 cm: UE imports cm with no armature scale
    return scene


class Builder:
    """Collects bevelled boxes in one bmesh, tagging region (UV) and bone per vertex."""

    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.bone = self.bm.verts.layers.int.new("bone")

    def box(self, center, size, region, bone="root", bevel=0.2, taper_top=1.0, taper_bottom=1.0):
        bm = self.bm
        before = set(bm.faces)
        cx, cy, cz = center
        hx, hy, hz = (s * 0.5 for s in size)
        verts = []
        for x in (-1, 1):
            for y in (-1, 1):
                for z in (-1, 1):
                    t = taper_top if z > 0 else taper_bottom
                    verts.append(bm.verts.new((cx + x * hx * t, cy + y * hy * t, cz + z * hz)))
        for f in [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]:
            bm.faces.new([verts[i] for i in f])
        made = [f for f in bm.faces if f not in before]
        bmesh.ops.recalc_face_normals(bm, faces=made)
        edges = list({e for f in made for e in f.edges})
        bmesh.ops.bevel(bm, geom=edges + verts, offset=min(size) * bevel, segments=2, affect="EDGES", profile=0.5)
        for f in bm.faces:
            if f in before:
                continue
            f.smooth = True
            for loop in f.loops:
                loop[self.uv].uv = (region + 0.5, 0.5)
                loop.vert[self.bone] = BONE_INDEX[bone]

    def to_object(self, name, armature=None):
        mesh = bpy.data.meshes.new(name)
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        self.bm.to_mesh(mesh)
        mesh.materials.append(bpy.data.materials.get("M_Villager") or bpy.data.materials.new("M_Villager"))
        if armature:
            groups = [obj.vertex_groups.new(name=b[0]) for b in BONES]
            per_bone = {}
            for v in self.bm.verts:
                per_bone.setdefault(v[self.bone], []).append(v.index)
            for index, verts in per_bone.items():
                groups[index].add(verts, 1.0, "REPLACE")
            obj.parent = armature
            obj.modifiers.new("Armature", "ARMATURE").object = armature
        self.bm.free()
        return obj


def build_armature():
    data = bpy.data.armatures.new("Villager")
    arm = bpy.data.objects.new("Armature", data)
    bpy.context.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    made = {}
    for name, head, tail, parent in BONES:
        b = data.edit_bones.new(name)
        b.head, b.tail = Vector(head), Vector(tail)
        b.roll = 0.0
        if parent:
            b.parent = made[parent]
        made[name] = b
    bpy.ops.object.mode_set(mode="OBJECT")
    return arm


def build_body(arm):
    b = Builder()
    for sx, side in ((1, "l"), (-1, "r")):
        b.box((8 * sx, -2.5, 6), (14.5, 23, 12), R["Leather"], f"foot_{side}", 0.25)
        b.box((8 * sx, 0, 13.5), (15, 15, 4), R["Leather"], f"calf_{side}", 0.25)          # boot cuff
        b.box((8 * sx, 0, 27.5), (11.5, 12, 30), R["Trousers"], f"calf_{side}", 0.22)
        b.box((8 * sx, 0, 58), (14, 14.5, 33), R["Trousers"], f"thigh_{side}", 0.22)
    # Tunic with a flared hem, belt and buckle.
    b.box((0, 0, 81), (34, 21, 18), R["Tunic"], "pelvis", 0.2, taper_top=0.9)
    b.box((0, 0, 72), (34.8, 21.8, 3), R["Accent"], "pelvis", 0.3)
    b.box((0, 0, 93), (32.5, 20.5, 5), R["Leather"], "pelvis", 0.2)
    b.box((0, -10.4, 93), (6, 1.6, 5.6), R["Accent"], "pelvis", 0.25)
    b.box((0, 0, 100), (31, 19, 13), R["Tunic"], "spine", 0.2)
    b.box((0, 0, 118), (37, 22, 24), R["Tunic"], "chest", 0.24, taper_bottom=0.88)
    b.box((0, -1, 130.5), (24, 19, 5), R["Accent"], "chest", 0.3)                       # scarf
    b.box((0, 0, 133), (11, 11, 6), R["Skin"], "neck", 0.2)
    # Head: large, slightly narrower at the jaw.
    b.box((0, 0, 153), (34, 32, 36), R["Skin"], "head", 0.3, taper_bottom=0.88)
    b.box((0, -16.4, 148), (5, 4, 6), R["Skin"], "head", 0.3)                          # nose
    b.box((0, -15.9, 141.8), (7, 1.2, 1.5), R["Pupil"], "head", 0.3)                   # mouth
    for sx in (1, -1):
        b.box((7.5 * sx, -16.0, 154), (7, 1.4, 7.5), R["EyeWhite"], "head", 0.3)
        b.box((6.8 * sx, -16.7, 153.4), (3.6, 1.4, 4.6), R["Pupil"], "head", 0.3)
        b.box((7.5 * sx, -16.4, 160.5), (8, 1.6, 2), R["Hair"], "head", 0.3)            # brows
        b.box((17.4 * sx, 0, 152), (2.8, 6.5, 9), R["Skin"], "head", 0.3)               # ears
        b.box((17.3 * sx, 5, 160), (3.2, 20, 14), R["Hair"], "head", 0.3)               # sides
    b.box((0, 1.5, 169), (36, 34, 8), R["Hair"], "head", 0.35)
    b.box((0, 15, 157), (36, 6, 24), R["Hair"], "head", 0.3)
    b.box((0, -15.2, 165.2), (30, 4, 5), R["Hair"], "head", 0.35)                      # fringe
    for sx, side in ((1, "l"), (-1, "r")):
        b.box((22 * sx, 0, 115.5), (12, 12.5, 25), R["Tunic"], f"upperarm_{side}", 0.24, taper_bottom=0.86)
        b.box((22 * sx, 0, 102.5), (11.5, 12, 3), R["Accent"], f"upperarm_{side}", 0.3)
        b.box((22 * sx, 0, 90), (9.5, 10, 23), R["Skin"], f"lowerarm_{side}", 0.24)
        b.box((22 * sx, -0.5, 72), (11, 11.5, 12), R["Skin"], f"hand_{side}", 0.3)
    return b.to_object("SK_Villager", arm)


def build_accessories():
    hx, hz = -22, 72  # right hand centre (the character's right is -X)
    items = {
        # Hats sit on the head (crown top at Z 173). Region 0 = main colour, 1 = band/detail.
        "SM_HatStraw": [((0, 0, 168), (54, 52, 3.5), 0, 0.3), ((0, 0, 174.5), (32, 31, 11), 0, 0.3), ((0, 0, 170.8), (33, 32, 2.8), 1, 0.25)],
        "SM_HatBeanie": [((0, 1, 172.5), (37, 35, 10), 0, 0.35), ((0, 1, 167.5), (37.6, 35.6, 4.5), 1, 0.3), ((0, 1, 179.5), (8, 8, 5), 1, 0.4)],
        "SM_HatWork": [((0, 1, 171.5), (36.5, 34.5, 9), 0, 0.3), ((0, -19.5, 167.5), (30, 10, 2.2), 0, 0.3), ((0, 1, 167.6), (37, 35, 2.4), 1, 0.25)],
        "SM_HatMerchant": [((0, 1, 177), (30, 29, 18), 0, 0.3), ((0, 1, 169.5), (34, 33, 4), 1, 0.3), ((13, 6, 182), (2, 3, 14), 1, 0.4)],
        "SM_HatHelmet": [((0, 1, 172.5), (37, 35, 11), 0, 0.35), ((0, 1, 167), (39, 37, 2.5), 1, 0.25), ((0, 1, 179), (4, 28, 4), 1, 0.3)],
        # Long hair behind the shoulders (region 0 takes the hair colour in game).
        "SM_HairLong": [((0, 16.5, 146), (33, 6, 36), 0, 0.3), ((0, 13, 167), (36, 9, 10), 0, 0.3)],
        # Tools in the right hand, pointing forward (-Y). Region 0 = wood, 1 = metal.
        "SM_Axe": [((hx, -16, hz), (3.6, 62, 3.6), 0, 0.3), ((hx, -45, hz - 4), (3, 9, 15), 1, 0.25)],
        "SM_Hoe": [((hx, -30, hz), (3.4, 96, 3.4), 0, 0.3), ((hx, -77, hz - 6), (12, 3, 10), 1, 0.25)],
        "SM_Hammer": [((hx, -10, hz), (3.2, 30, 3.2), 0, 0.3), ((hx, -25, hz), (6.5, 7, 13), 1, 0.25)],
        "SM_Basket": [((hx, -8, hz - 12), (20, 18, 13), 0, 0.2), ((hx, -8, hz - 3), (3, 18, 3), 0, 0.3)],
        # Merchant's satchel on the left hip with a strap across the chest.
        "SM_Satchel": [((18.5, -4, 86), (5, 18, 15), 0, 0.3), ((0, -11.6, 114), (36, 1.6, 4), 1, 0.25)],
    }
    made = {}
    for name, boxes in items.items():
        b = Builder()
        for center, size, region, bevel in boxes:
            b.box(center, size, region, "root", bevel)
        made[name] = b.to_object(name)
    return made


def export_fbx(path, objects):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE",
        object_types={"ARMATURE", "MESH"}, add_leaf_bones=False, bake_anim=False, use_armature_deform_only=False,
        primary_bone_axis="Y", secondary_bone_axis="X", armature_nodetype="NULL", mesh_smooth_type="FACE")


def region_colours(obj, colours):
    """Preview only: vertex colours from the UV region, so Workbench shows the palette."""
    mesh = obj.data
    attr = mesh.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
    uv = mesh.uv_layers["UVMap"].data
    for i, loop in enumerate(mesh.loops):
        attr.data[i].color = (*colours[min(int(uv[i].uv[0]), len(colours) - 1)], 1)


def preview(body, arm, accessories):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.color_type = "VERTEX"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x, scene.render.resolution_y = 1600, 760
    world = bpy.data.worlds.new("W")
    world.color = (0.55, 0.66, 0.78)
    scene.world = world
    looks = [  # skin, tunic, trousers, leather, hair, accent ; hat ; tool ; hat colours
        ((0.86, 0.66, 0.5), (0.42, 0.55, 0.2), (0.33, 0.27, 0.2), (0.35, 0.21, 0.11), (0.3, 0.17, 0.08), (0.8, 0.7, 0.4), "SM_HatStraw", "SM_Hoe", ((0.86, 0.72, 0.38), (0.5, 0.16, 0.1))),
        ((0.62, 0.43, 0.3), (0.62, 0.16, 0.12), (0.22, 0.24, 0.3), (0.3, 0.18, 0.1), (0.1, 0.08, 0.07), (0.24, 0.25, 0.3), "SM_HatBeanie", "SM_Axe", ((0.24, 0.3, 0.2), (0.75, 0.7, 0.62))),
        ((0.95, 0.78, 0.64), (0.75, 0.45, 0.16), (0.3, 0.3, 0.33), (0.42, 0.26, 0.13), (0.82, 0.62, 0.3), (0.35, 0.35, 0.38), "SM_HatWork", "SM_Hammer", ((0.5, 0.32, 0.17), (0.3, 0.2, 0.1))),
        ((0.75, 0.55, 0.42), (0.45, 0.2, 0.55), (0.2, 0.16, 0.22), (0.38, 0.22, 0.12), (0.45, 0.45, 0.47), (0.9, 0.75, 0.3), "SM_HatMerchant", "SM_Satchel", ((0.32, 0.12, 0.38), (0.9, 0.75, 0.3))),
        ((0.48, 0.33, 0.24), (0.2, 0.42, 0.6), (0.3, 0.25, 0.2), (0.33, 0.2, 0.1), (0.5, 0.2, 0.08), (0.9, 0.9, 0.85), None, "SM_HairLong", None),
    ]
    for i, (skin, tunic, trousers, leather, hair, accent, hat, extra, hat_cols) in enumerate(looks):
        dx = (2 - i) * 72
        copy = body.copy()
        copy.data = body.data.copy()
        copy.modifiers.clear()
        copy.parent = None
        copy.location = (dx, 0, 0)
        bpy.context.collection.objects.link(copy)
        region_colours(copy, [skin, tunic, trousers, leather, hair, (0.05, 0.05, 0.06), (0.95, 0.95, 0.92), accent])
        for name, cols in ((hat, hat_cols), (extra, None)):
            if not name:
                continue
            acc = accessories[name].copy()
            acc.data = accessories[name].data.copy()
            acc.location = (dx, 0, 0)
            bpy.context.collection.objects.link(acc)
            if name == "SM_HairLong":
                cols = (hair, hair)
            region_colours(acc, list(cols or ((0.55, 0.38, 0.2), (0.62, 0.64, 0.68))))
    for o in [body, arm] + list(accessories.values()):
        o.hide_render = True
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    bpy.context.collection.objects.link(cam)
    cam.location = (0, -560, 120)
    cam.rotation_euler = (math.radians(88), 0, 0)
    cam.data.lens = 50
    cam.data.clip_end = 5000
    scene.camera = cam
    scene.render.filepath = os.path.join(OUT, "preview.png")
    bpy.ops.render.render(write_still=True)


def main():
    reset_scene()
    arm = build_armature()
    body = build_body(arm)
    accessories = build_accessories()
    export_fbx(os.path.join(OUT, "SK_Villager.fbx"), [body, arm])
    for name, obj in accessories.items():
        export_fbx(os.path.join(OUT, f"{name}.fbx"), [obj])
    preview(body, arm, accessories)
    print("VILLAGER_OK", len(body.data.vertices), "vertices,", len(body.data.polygons), "faces,", len(BONES), "bones,",
          len(accessories), "accessories ->", OUT)


main()
