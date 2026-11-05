"""Builds the rigged athletes from Mixamo downloads in Content/Mixamo.

The downloads and the assets made from them stay out of git, because the
Mixamo license doesn't allow sharing the raw files. Run ImportAthletes.bat to
rebuild them. Without them the game uses its shape-built players.
"""
import os
import unreal

SOURCE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()), "Mixamo")
DEST = "/Game/Athlete"
CHARACTER = "Ch38_nonPBR.fbx"
ANIMATIONS = {
    "Idle.fbx": "A_Idle",
    "Running.fbx": "A_Run",
    "Standing Jump.fbx": "A_Jump",
    "Standing Melee Attack Downward.fbx": "A_Spike",
    "Run To Dive.fbx": "A_Dive",
    "Samba Dancing.fbx": "A_Celebrate",
}

# The kit is the bright, unsaturated part of the texture, so it takes the team
# colour while skin and hair keep theirs.
KIT_SHADER = """float hi = max(C.r, max(C.g, C.b));
float lo = min(C.r, min(C.g, C.b));
float kit = saturate(1.0 - (hi - lo) * 9.0) * saturate((hi - 0.28) * 5.0);
return lerp(C, Team * (0.3 + 0.9 * hi), kit);"""

tools = unreal.AssetToolsHelpers.get_asset_tools()
editing = unreal.MaterialEditingLibrary


def import_file(file_name, asset_name, options):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SOURCE, file_name)
    task.destination_path = DEST
    task.destination_name = asset_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.options = options
    tools.import_asset_tasks([task])


def import_character():
    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = True
    options.import_animations = False
    options.import_materials = True
    options.import_textures = True
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.automated_import_should_detect_type = False
    import_file(CHARACTER, "SK_Athlete", options)
    return unreal.load_asset(DEST + "/SK_Athlete")


def import_animations(skeleton):
    for file_name, asset_name in ANIMATIONS.items():
        options = unreal.FbxImportUI()
        options.import_mesh = False
        options.import_animations = True
        options.import_materials = False
        options.import_textures = False
        options.skeleton = skeleton
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
        options.automated_import_should_detect_type = False
        import_file(file_name, asset_name, options)


def build_kit_material():
    path = DEST + "/M_AthleteTeam"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = tools.create_asset("M_AthleteTeam", DEST, unreal.Material, unreal.MaterialFactoryNew())
    # Without this Unreal quietly draws the default material on skeletal meshes.
    material.set_editor_property("used_with_skeletal_mesh", True)

    diffuse = editing.create_material_expression(material, unreal.MaterialExpressionTextureSample, -700, -100)
    diffuse.set_editor_property("texture", unreal.load_asset(DEST + "/Ch38_1001_Diffuse"))
    normal = editing.create_material_expression(material, unreal.MaterialExpressionTextureSample, -700, 300)
    normal.set_editor_property("texture", unreal.load_asset(DEST + "/Ch38_1001_Normal"))
    normal.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    team = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 100)
    team.set_editor_property("parameter_name", "Team")
    team.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))

    kit = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 0)
    kit.set_editor_property("code", KIT_SHADER)
    kit.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ("C", "Team"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        inputs.append(pin)
    kit.set_editor_property("inputs", inputs)
    editing.connect_material_expressions(diffuse, "RGB", kit, "C")
    editing.connect_material_expressions(team, "", kit, "Team")
    editing.connect_material_property(kit, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    # Matte, like fabric. A shiny kit washes out under the arena lights.
    for value, prop, y in ((0.9, unreal.MaterialProperty.MP_ROUGHNESS, 200), (0.15, unreal.MaterialProperty.MP_SPECULAR, 300)):
        constant = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -300, y)
        constant.set_editor_property("r", value)
        editing.connect_material_property(constant, "", prop)

    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)


mesh = import_character()
import_animations(mesh.skeleton)
build_kit_material()
print("VC26 athletes imported")
