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

# Turns a white football kit into a team's volleyball kit. The fabric is the pale,
# unsaturated part of the texture. P is the T-pose position in cm, which picks out
# body parts: the arms lie along X, so sleeves end at a fixed distance from the
# chest, and legs are bands of height. UV finds the printed number.
KIT_SHADER = """float hi = max(C.r, max(C.g, C.b));
float lo = min(C.r, min(C.g, C.b));
float fabric = saturate(1.0 - (hi - lo - 0.05) * 14.0) * saturate((hi - 0.1) * 8.0);
float shade = lerp(0.75, hi, 0.6) * 0.9 + 0.35;
float ax = abs(P.x);
float z = P.z;

float3 kit = (z < ShortsTop ? Shorts : Team) * shade;
float3 skin = float3(0.62, 0.4, 0.31) * Skin * (0.75 + 0.4 * hi);

// The printed 7 goes, and a number in seven bars goes on the back and the chest.
int bars[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
int number = (int)(Number + 0.5);
float2 centres[2] = {float2(0.555, 0.772), float2(0.165, 0.31)};
float2 sizes[2] = {float2(0.05, 0.062), float2(0.03, 0.038)};
float digit = 0.0;
for (int place = 0; place < 2; ++place)
{
    float2 q = (UV - centres[place]) / sizes[place];
    q.y = -q.y;
    if (place == 0 && abs(q.x) < 1.5 && abs(q.y) < 1.4)
    {
        kit = Team;
        fabric = 1.0;  // the printed 7 is too dark to count as fabric
    }
    for (int side = 0; side < 2; ++side)
    {
        int value = side == 0 ? number % 10 : number / 10;
        if (side == 1 && value == 0)
        {
            continue;
        }
        float2 d = q;
        if (number >= 10)
        {
            d.x += side == 0 ? -0.62 : 0.62;
            d.x *= 1.25;
        }
        if (value == 1)
        {
            d.x += 0.5;  // a lone bar would sit off centre
        }
        int b = bars[value];
        float t = 0.17;
        float across = abs(d.x) < 0.5 + t ? 1.0 : 0.0;
        float upper = d.y > 0.0 && d.y < 1.0 + t ? 1.0 : 0.0;
        float lower = d.y < 0.0 && d.y > -1.0 - t ? 1.0 : 0.0;
        float on = 0.0;
        on = max(on, (b & 1) != 0 && abs(d.y - 1.0) < t ? across : 0.0);
        on = max(on, (b & 64) != 0 && abs(d.y) < t ? across : 0.0);
        on = max(on, (b & 8) != 0 && abs(d.y + 1.0) < t ? across : 0.0);
        on = max(on, (b & 32) != 0 && abs(d.x + 0.5) < t ? upper : 0.0);
        on = max(on, (b & 2) != 0 && abs(d.x - 0.5) < t ? upper : 0.0);
        on = max(on, (b & 16) != 0 && abs(d.x + 0.5) < t ? lower : 0.0);
        on = max(on, (b & 4) != 0 && abs(d.x - 0.5) < t ? lower : 0.0);
        digit = max(digit, on);
    }
}
kit = lerp(kit, Print, digit);

float3 result = lerp(C * Skin, kit, fabric);

// Short sleeves, bare shins, and low socks. Football boots stay as they are.
float sleeve = ax > 32.0 && z > 110.0 ? 1.0 : 0.0;
float shin = z > 21.0 && z < 41.0 ? 1.0 : 0.0;
result = lerp(result, skin, fabric * max(sleeve, shin));
float pad = z > 40.0 && z < 57.0 ? 1.0 : 0.0;
result = lerp(result, float3(0.02, 0.02, 0.025), pad);
float sock = z < 21.0 ? 1.0 : 0.0;
result = lerp(result, float3(0.8, 0.8, 0.82) * shade, fabric * sock);
float boot = z < 11.0 ? 1.0 : 0.0;
return lerp(result, C, boot);"""

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
    def parameter(kind, name, default, y):
        node = editing.create_material_expression(material, kind, -700, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", default)
        return node

    white = unreal.LinearColor(1, 1, 1, 1)
    colours = {name: parameter(unreal.MaterialExpressionVectorParameter, name, white, 500 + i * 120)
               for i, name in enumerate(("Team", "Shorts", "Print", "Skin"))}
    number = parameter(unreal.MaterialExpressionScalarParameter, "Number", 7.0, 1000)
    shorts_top = parameter(unreal.MaterialExpressionScalarParameter, "ShortsTop", 90.0, 1100)
    texcoord = editing.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -700, 1200)

    # Skinned positions only exist in the vertex shader, so they're passed along.
    rest = editing.create_material_expression(material, unreal.MaterialExpressionPreSkinnedPosition, -900, 1300)
    carried = editing.create_material_expression(material, unreal.MaterialExpressionVertexInterpolator, -700, 1300)
    editing.connect_material_expressions(rest, "", carried, "")

    kit = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 0)
    kit.set_editor_property("code", KIT_SHADER)
    kit.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = (("C", diffuse, "RGB"), ("Team", colours["Team"], ""), ("Shorts", colours["Shorts"], ""),
            ("Print", colours["Print"], ""), ("Skin", colours["Skin"], ""), ("Number", number, ""),
            ("ShortsTop", shorts_top, ""), ("UV", texcoord, ""), ("P", carried, ""))
    inputs = []
    for name, _, _ in pins:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        inputs.append(pin)
    kit.set_editor_property("inputs", inputs)
    for name, node, output in pins:
        editing.connect_material_expressions(node, output, kit, name)
    editing.connect_material_property(kit, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    # Matte, like fabric. A shiny kit washes out under the arena lights.
    for value, prop, y in ((0.9, unreal.MaterialProperty.MP_ROUGHNESS, 200), (0.15, unreal.MaterialProperty.MP_SPECULAR, 300)):
        constant = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -300, y)
        constant.set_editor_property("r", value)
        editing.connect_material_property(constant, "", prop)

    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)


def build_hair_material():
    path = DEST + "/M_AthleteHair"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = tools.create_asset("M_AthleteHair", DEST, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("used_with_skeletal_mesh", True)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)

    diffuse = editing.create_material_expression(material, unreal.MaterialExpressionTextureSample, -700, 0)
    diffuse.set_editor_property("texture", unreal.load_asset(DEST + "/Ch38_1002_Diffuse"))
    normal = editing.create_material_expression(material, unreal.MaterialExpressionTextureSample, -700, 300)
    normal.set_editor_property("texture", unreal.load_asset(DEST + "/Ch38_1002_Normal"))
    normal.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    tint = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 600)
    tint.set_editor_property("parameter_name", "Hair")
    tint.set_editor_property("default_value", unreal.LinearColor(0.03, 0.02, 0.015, 1))

    # The hair texture is near black, so the colour replaces it and the texture only shades.
    mixed = editing.create_material_expression(material, unreal.MaterialExpressionAdd, -300, 100)
    editing.connect_material_expressions(diffuse, "RGB", mixed, "A")
    editing.connect_material_expressions(tint, "", mixed, "B")
    editing.connect_material_property(mixed, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(diffuse, "A", unreal.MaterialProperty.MP_OPACITY_MASK)

    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)


# Set VC26_MATERIALS_ONLY to rebuild just the materials after changing them.
if not os.environ.get("VC26_MATERIALS_ONLY"):
    mesh = import_character()
    import_animations(mesh.skeleton)
build_kit_material()
build_hair_material()
print("VC26 athletes imported")
