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

// Grey under the hair would otherwise count as fabric.
fabric = z > 154.0 && ax < 11.5 ? 0.0 : fabric;

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

// Short sleeves, bare shins, and socks low or high.
float high = KitB.y;
float sleeve = ax > 32.0 && z > 110.0 ? 1.0 : 0.0;
float shin = z > 21.0 && z < 41.0 && high < 0.5 ? 1.0 : 0.0;
result = lerp(result, skin, fabric * max(sleeve, shin));
float pad = z > 40.0 && z < 57.0 ? KitB.z : 0.0;
result = lerp(result, Knee * (0.6 + 0.5 * hi), pad);
float sock = z < (high > 0.5 ? 40.0 : 21.0) ? 1.0 : 0.0;
result = lerp(result, float3(0.8, 0.8, 0.82) * shade, fabric * sock);

// Arms: a compression sleeve, ink and finger tape. Which side is the hitting arm
// comes from the rig: the right arm lies along -X.
float arm = ax > 30.0 && ax < 66.0 && z > 125.0 ? 1.0 : 0.0;
float ink = 0.0;
if (KitB.x > 1.5)
{
    ink = ax > 46.0 && ax < 66.0 ? 0.55 + 0.35 * sin(ax * 2.1 + P.y * 1.4) : 0.0;
}
else if (KitB.x > 0.5)
{
    ink = (ax > 38.0 && ax < 40.5) || (ax > 41.5 && ax < 42.5) ? 0.85 : 0.0;
}
result = lerp(result, float3(0.03, 0.05, 0.07), arm * ink);
float wanted = KitA.z > 2.5 ? 1.0 : KitA.z > 1.5 ? step(0.0, P.x) : KitA.z > 0.5 ? step(P.x, 0.0) : 0.0;
result = lerp(result, float3(0.025, 0.025, 0.03) * (0.7 + 0.6 * hi), arm * wanted);
float tape = KitC.y * (ax > 73.0 && z > 125.0 && frac(ax / 2.4) < 0.45 ? 1.0 : 0.0);
result = lerp(result, float3(0.82, 0.82, 0.8), tape);

// The scalp shows wherever a style leaves it, from bare skin to a close shave.
float head = z > 150.0 && ax < 11.5 ? 1.0 : 0.0;
float hairline = lerp(160.0, 170.5, saturate((P.y + 2.0) / 10.0));
float scalp = head * saturate((z - hairline) / 1.5);
float style = KitA.y;
float cover = style > 5.5 ? 0.5 : style > 3.5 ? (style > 4.5 ? 0.9 : 0.12) : style > 2.5 ? 0.0 : style > 1.5 ? 0.72 : 0.9;
float3 hairTint = Hair * 0.8 + 0.012;
float3 bare = float3(0.4, 0.25, 0.19) * Skin;  // the top of the head catches more light
result = lerp(result, lerp(bare, hairTint, cover), scalp);

// Facial hair, painted over the skin below the nose.
float grain = frac(sin(dot(P.xz, float2(91.7, 47.3))) * 43758.5);
float front = head * saturate((P.y - 0.5) / 2.0);
float top = 156.6 + 0.32 * ax;  // a beard climbs towards the ears
float jaw = front * (1.0 - smoothstep(top - 0.8, top + 0.6, z)) * smoothstep(151.0, 152.6, z);
float2 m = float2(ax / 2.9, (z - 157.2) / 1.0);
float mouth = smoothstep(0.75, 1.1, length(m));
float curve = 158.75 - 0.07 * ax * ax;
float lip = front * (1.0 - smoothstep(0.45, 0.85, abs(z - curve))) * (1.0 - smoothstep(2.9, 3.9, ax));
float2 c = float2(ax / 3.1, (z - 154.6) / 2.1);
float chin = front * (1.0 - smoothstep(0.75, 1.05, length(c)));
float beard = KitA.x > 3.5 ? max(jaw * mouth, lip) : KitA.x > 2.5 ? max(chin * mouth, lip) : KitA.x > 1.5 ? lip : 0.0;
beard *= 0.72 + 0.28 * grain;
float stubble = KitA.x > 0.5 && KitA.x < 1.5 ? jaw * mouth * 0.55 * step(0.45, grain) : 0.0;
result = lerp(result, Hair * 0.7 + 0.012, max(beard, stubble));

// A headband sits on the forehead and round the skull.
float band = KitC.x * head * (z > 170.0 && z < 172.6 ? 1.0 : 0.0);
result = lerp(result, BandColor, band);

// Volleyball shoes: a coloured upper, a stripe, and a pale rubber sole.
float3 shoe = ShoeBase * (0.55 + 0.7 * hi);
shoe = lerp(shoe, ShoeAccent, z > 5.5 && z < 7.8 ? 1.0 : 0.0);
shoe = lerp(shoe, float3(0.8, 0.8, 0.78), z < 5.0 ? 1.0 : 0.0);
return lerp(result, shoe, z < 13.0 ? 1.0 : 0.0);"""

# Runs per vertex, so one mesh can be many people. Each bump swells or sinks the
# skin along its normal around a landmark measured on the T-pose, in cm. Kit and
# skin share the same bumps, so nothing pokes through.
SHAPE_SHADER = """float ax = abs(P.x);
float z = P.z;
float3 s = float3(ax, P.y, z);
float off = 0.0;
float3 d;

d = (P - float3(0.0, 14.0, 161.0)) / float3(1.8, 3.0, 2.4);
off += FaceA.x * 0.9 * exp(-dot(d, d));
d = (s - float3(6.5, 7.0, 155.5)) / float3(2.8, 4.0, 3.0);
off += FaceA.y * 1.0 * exp(-dot(d, d));
d = (P - float3(0.0, 12.0, 153.5)) / float3(3.0, 3.0, 2.2);
off += FaceA.z * 0.9 * exp(-dot(d, d));
d = (s - float3(7.5, 9.0, 162.0)) / float3(2.6, 3.5, 2.4);
off += FaceB.x * 0.7 * exp(-dot(d, d));
d = (P - float3(0.0, 11.5, 167.5)) / float3(6.0, 2.2, 1.5);
off += FaceB.y * 0.6 * exp(-dot(d, d));
d = (P - float3(0.0, 10.0, 172.0)) / float3(7.0, 3.0, 3.0);
off += FaceB.z * 0.6 * exp(-dot(d, d));
d = (P - float3(0.0, 13.0, 157.3)) / float3(3.0, 1.6, 1.1);
off += FaceC.x * 0.4 * exp(-dot(d, d));

d = (s - float3(24.0, 0.0, 141.0)) / float3(8.0, 9.0, 8.0);
off += BodyA.x * exp(-dot(d, d));
float upper = saturate((ax - 28.0) / 3.0) * saturate((50.0 - ax) / 4.0) * step(125.0, z);
float lower = saturate((ax - 48.0) / 3.0) * saturate((66.0 - ax) / 4.0) * step(125.0, z);
off += BodyA.y * (upper + lower * 0.5);
float torso = saturate((19.0 - ax) / 3.0);
off += BodyA.z * torso * saturate((z - 116.0) / 8.0) * saturate((148.0 - z) / 6.0);
off += BodyB.x * torso * saturate((z - 90.0) / 6.0) * saturate((124.0 - z) / 8.0);
// It fades out above the shorts' hem, whose inside faces the other way.
off += BodyB.y * saturate((z - 61.0) / 8.0) * saturate((96.0 - z) / 6.0) * step(ax, 24.0);
off += BodyB.z * saturate((z - 12.0) / 6.0) * saturate((48.0 - z) / 6.0) * step(ax, 24.0);

// Studs reach 3.2 cm up. They fold into a flat sole, and the shoe above squeezes
// down to meet it, so the foot still stands on the floor.
float sole = lerp(0.6, 13.0, saturate((z - 3.2) / 9.8));
return N * off + float3(0.0, 0.0, z < 13.0 ? sole - z : 0.0);"""

# The hair is one haircut, so the others are cut from it. Lashes share the mesh.
HAIR_SHADER = """float ax = abs(P.x);
float z = P.z;
float lash = P.y > 9.0 && z < 167.5 && ax < 6.0 ? 1.0 : 0.0;
float keep = 1.0;
float style = Style.x;
if (style > 5.5)
{
    keep = ax < 6.5 && z > 168.0 ? 1.0 : 0.0;
}
else if (style > 4.5)
{
    keep = 1.0;
}
else if (style > 3.5)
{
    keep = ax < 2.8 ? 1.0 : 0.0;
}
else if (style > 1.5)
{
    keep = 0.0;
}
else if (style > 0.5)
{
    keep = z > 169.0 ? 1.0 : 0.0;
}
return A * max(keep, lash);"""

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


def connect_pins(custom, pins):
    inputs = []
    for name, _, _ in pins:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        inputs.append(pin)
    custom.set_editor_property("inputs", inputs)
    for name, node, output in pins:
        editing.connect_material_expressions(node, output, custom, name)


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
               for i, name in enumerate(("Team", "Shorts", "Print", "Skin", "Hair", "ShoeBase", "ShoeAccent", "Knee", "BandColor"))}
    black = unreal.LinearColor(0, 0, 0, 1)
    shape = {name: parameter(unreal.MaterialExpressionVectorParameter, name, black, 1600 + i * 120)
             for i, name in enumerate(("FaceA", "FaceB", "FaceC", "BodyA", "BodyB", "KitA", "KitB", "KitC"))}
    number = parameter(unreal.MaterialExpressionScalarParameter, "Number", 7.0, 2600)
    shorts_top = parameter(unreal.MaterialExpressionScalarParameter, "ShortsTop", 90.0, 2700)
    texcoord = editing.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -700, 2800)

    # Skinned positions only exist in the vertex shader, so they're passed along.
    rest = editing.create_material_expression(material, unreal.MaterialExpressionPreSkinnedPosition, -900, 2900)
    carried = editing.create_material_expression(material, unreal.MaterialExpressionVertexInterpolator, -700, 2900)
    editing.connect_material_expressions(rest, "", carried, "")

    kit = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 0)
    kit.set_editor_property("code", KIT_SHADER)
    kit.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = [("C", diffuse, "RGB"), ("Number", number, ""), ("ShortsTop", shorts_top, ""), ("UV", texcoord, ""), ("P", carried, "")]
    pins += [(name, colours[name], "") for name in colours]
    pins += [(name, shape[name], "") for name in ("KitA", "KitB", "KitC")]
    connect_pins(kit, pins)
    editing.connect_material_property(kit, "", unreal.MaterialProperty.MP_BASE_COLOR)

    normal_ws = editing.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -500, 3000)
    reshape = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 500)
    reshape.set_editor_property("code", SHAPE_SHADER)
    reshape.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    connect_pins(reshape, [("P", rest, ""), ("N", normal_ws, "")] + [(name, shape[name], "") for name in ("FaceA", "FaceB", "FaceC", "BodyA", "BodyB")])
    editing.connect_material_property(reshape, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
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

    style = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 720)
    style.set_editor_property("parameter_name", "HairStyle")
    style.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 1))
    band = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 840)
    band.set_editor_property("parameter_name", "BandColor")
    band.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 1))
    rest = editing.create_material_expression(material, unreal.MaterialExpressionPreSkinnedPosition, -900, 960)
    carried = editing.create_material_expression(material, unreal.MaterialExpressionVertexInterpolator, -700, 960)
    editing.connect_material_expressions(rest, "", carried, "")

    # The hair texture is near black, so the colour replaces it and the texture only shades.
    mixed = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 100)
    mixed.set_editor_property("code", "float inBand = Style.y > 0.5 && P.z > 170.0 && P.z < 172.6 ? 1.0 : 0.0;\nreturn lerp(C + Tint, Band, inBand);")
    mixed.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    connect_pins(mixed, [("C", diffuse, "RGB"), ("Tint", tint, ""), ("Style", style, ""), ("Band", band, ""), ("P", carried, "")])
    editing.connect_material_property(mixed, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    cut = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 300)
    cut.set_editor_property("code", HAIR_SHADER)
    cut.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    connect_pins(cut, [("A", diffuse, "A"), ("Style", style, ""), ("P", carried, "")])
    editing.connect_material_property(cut, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    # Curls stand off the head.
    normal_ws = editing.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -500, 1100)
    lift = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 500)
    lift.set_editor_property("code", "return N * (Style.x > 4.5 && Style.x < 5.5 ? 1.8 : 0.0);")
    lift.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    connect_pins(lift, [("N", normal_ws, ""), ("Style", style, "")])
    editing.connect_material_property(lift, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)


# Set VC26_MATERIALS_ONLY to rebuild just the materials after changing them.
if not os.environ.get("VC26_MATERIALS_ONLY"):
    mesh = import_character()
    import_animations(mesh.skeleton)
build_kit_material()
build_hair_material()
print("VC26 athletes imported")
