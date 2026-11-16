"""
Niagara combat effects (called from ue_setup_gameplay.py, or run on its own with -script=ue_setup_fx.py).

Creates:
    /Game/Metalization/Materials/M_FX_Sprite, MI_FX_SpriteHot   additive soft-dot particles (Particle Color x alpha)
    /Game/Metalization/Materials/M_FX_Smoke                     translucent soft puffs
    /Game/Metalization/Materials/M_FX_Fire                      translucent glowing puffs (fireballs)
    /Game/Metalization/Materials/M_FX_BeamCore                  additive beam cores (mesh particles)
    /Game/Metalization/FX/NS_MuzzleFlash, NS_Impact, NS_Explosion, NS_Burst, NS_Beam, NS_Tracer

UE Python can't edit Niagara emitters, so the systems themselves are built by
UMetalFXAuthoring::BuildEffectSystems in the editor-only MetalizationEditor module. Every system is tinted at
runtime through its User.Color parameter by AMetalFX.
"""
import unreal

ROOT = "/Game/Metalization"
MATERIALS = ROOT + "/Materials"
FX = ROOT + "/FX"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

# Soft round dot; WhiteMix pushes the centre towards white for a hot look.
SPRITE_HLSL = """
float2 d = UV * 2 - 1;
float r2 = dot(d, d);
float glow = saturate(1 - r2);
glow *= glow;
float core = saturate(1 - r2 * 3);
float3 c = Color.rgb;
float peak = max(max(c.r, c.g), c.b);
return lerp(c, peak.xxx, WhiteMix * core) * glow * Alpha * Intensity;
"""

# Soft-edged puff with a little lumpiness so overlapping puffs don't look like discs.
SMOKE_HLSL = """
float2 d = UV * 2 - 1;
float r2 = dot(d, d);
float lump = 0.75 + 0.25 * sin(UV.x * 9.0 + UV.y * 5.0) * sin(UV.y * 11.0 - UV.x * 4.0);
return saturate(1 - r2) * saturate(1 - r2) * lump * Alpha;
"""

# Fireball puff: glows like the sprites but is alpha-blended, so it still reads against bright floors and sky.
FIRE_EMISSIVE_HLSL = """
float2 d = UV * 2 - 1;
float core = saturate(1 - dot(d, d) * 2.5);
float3 c = Color.rgb;
float peak = max(max(c.r, c.g), c.b);
return lerp(c, peak.xxx, 0.6 * core) * Intensity;
"""

# Beam core: brightest where the cylinder faces the camera, whiter in the middle.
BEAM_HLSL = """
float f = saturate(abs(dot(normalize(N), normalize(V))));
float3 c = Color.rgb;
float peak = max(max(c.r, c.g), c.b);
return lerp(c, peak.xxx, 0.6 * f * f) * lerp(0.25, 1.0, f * f) * Alpha * Intensity;
"""


def log(msg):
    unreal.log(f"[MZSetup] {msg}")


def setp(obj, **props):
    for key, value in props.items():
        obj.set_editor_property(key, value)
    return obj


def load(path):
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


def fresh_material(name, blend):
    mat = load(f"{MATERIALS}/{name}")
    if mat is None:
        mat = asset_tools.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    else:
        mel.delete_all_material_expressions(mat)
    setp(mat, shading_model=unreal.MaterialShadingModel.MSM_UNLIT, blend_mode=blend, used_with_static_lighting=False,
         used_with_niagara_sprites=True, used_with_niagara_mesh_particles=True, used_with_niagara_ribbons=True)
    return mat


def node(mat, cls, x, y, **props):
    return setp(mel.create_material_expression(mat, cls, x, y), **props)


def custom(mat, code, inputs, x, y, output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
    return node(mat, unreal.MaterialExpressionCustom, x, y, code=code, output_type=output,
                inputs=[setp(unreal.CustomInput(), input_name=n) for n in inputs])


def scalar(mat, name, value, x, y):
    return node(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def finish(mat):
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("material: " + mat.get_path_name())
    return mat


def make_sprite_material():
    """M_FX_Sprite: additive; Emissive = soft dot x Particle Color x alpha x Intensity (params Intensity, WhiteMix)."""
    mat = fresh_material("M_FX_Sprite", unreal.BlendMode.BLEND_ADDITIVE)
    color = node(mat, unreal.MaterialExpressionParticleColor, -700, -100)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, -700, 100)
    code = custom(mat, SPRITE_HLSL, ["UV", "Color", "Alpha", "Intensity", "WhiteMix"], -300, 0)
    mel.connect_material_expressions(uv, "", code, "UV")
    mel.connect_material_expressions(color, "", code, "Color")
    mel.connect_material_expressions(color, "A", code, "Alpha")
    mel.connect_material_expressions(scalar(mat, "Intensity", 4.0, -700, 250), "", code, "Intensity")
    mel.connect_material_expressions(scalar(mat, "WhiteMix", 0.15, -700, 350), "", code, "WhiteMix")
    mel.connect_material_property(code, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(mat)


def make_hot_instance(parent):
    """MI_FX_SpriteHot: M_FX_Sprite brighter and whiter, for flashes and sparks."""
    name = "MI_FX_SpriteHot"
    mi = load(f"{MATERIALS}/{name}")
    if mi is None:
        mi = asset_tools.create_asset(name, MATERIALS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_scalar_parameter_value(mi, "Intensity", 9.0)
    mel.set_material_instance_scalar_parameter_value(mi, "WhiteMix", 0.75)
    eal.save_loaded_asset(mi)
    log("material: " + mi.get_path_name())
    return mi


def make_smoke_material():
    """M_FX_Smoke: translucent unlit; colour from Particle Color, opacity a soft lumpy puff x alpha."""
    mat = fresh_material("M_FX_Smoke", unreal.BlendMode.BLEND_TRANSLUCENT)
    color = node(mat, unreal.MaterialExpressionParticleColor, -700, -100)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, -700, 100)
    code = custom(mat, SMOKE_HLSL, ["UV", "Alpha"], -300, 100, unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    mel.connect_material_expressions(uv, "", code, "UV")
    mel.connect_material_expressions(color, "A", code, "Alpha")
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(code, "", unreal.MaterialProperty.MP_OPACITY)
    return finish(mat)


def make_fire_material():
    """M_FX_Fire: translucent unlit; glowing Particle Color (Intensity), opacity a soft lumpy puff x alpha."""
    mat = fresh_material("M_FX_Fire", unreal.BlendMode.BLEND_TRANSLUCENT)
    color = node(mat, unreal.MaterialExpressionParticleColor, -700, -100)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, -700, 100)
    glow = custom(mat, FIRE_EMISSIVE_HLSL, ["UV", "Color", "Intensity"], -300, -100)
    mel.connect_material_expressions(uv, "", glow, "UV")
    mel.connect_material_expressions(color, "", glow, "Color")
    mel.connect_material_expressions(scalar(mat, "Intensity", 4.0, -700, 250), "", glow, "Intensity")
    puff = custom(mat, SMOKE_HLSL, ["UV", "Alpha"], -300, 150, unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    mel.connect_material_expressions(uv, "", puff, "UV")
    mel.connect_material_expressions(color, "A", puff, "Alpha")
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(puff, "", unreal.MaterialProperty.MP_OPACITY)
    return finish(mat)


def make_beam_material():
    """M_FX_BeamCore: additive mesh-particle material, bright facing the camera and soft at the edges."""
    mat = fresh_material("M_FX_BeamCore", unreal.BlendMode.BLEND_ADDITIVE)
    color = node(mat, unreal.MaterialExpressionParticleColor, -700, -100)
    normal = node(mat, unreal.MaterialExpressionVertexNormalWS, -700, 100)
    camera = node(mat, unreal.MaterialExpressionCameraVectorWS, -700, 200)
    code = custom(mat, BEAM_HLSL, ["N", "V", "Color", "Alpha", "Intensity"], -300, 0)
    mel.connect_material_expressions(normal, "", code, "N")
    mel.connect_material_expressions(camera, "", code, "V")
    mel.connect_material_expressions(color, "", code, "Color")
    mel.connect_material_expressions(color, "A", code, "Alpha")
    mel.connect_material_expressions(scalar(mat, "Intensity", 6.0, -700, 300), "", code, "Intensity")
    mel.connect_material_property(code, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(mat)


def build():
    sprite = make_sprite_material()
    hot = make_hot_instance(sprite)
    smoke = make_smoke_material()
    fire = make_fire_material()
    beam = make_beam_material()
    cylinder = eal.load_asset("/Engine/BasicShapes/Cylinder")
    if not hasattr(unreal, "MetalFXAuthoring"):
        raise RuntimeError("MetalizationEditor module isn't loaded - build the MetalizationEditor target first")
    built = unreal.MetalFXAuthoring.build_effect_systems(FX, sprite, hot, smoke, fire, beam, cylinder)
    log(f"niagara systems: {built}/6 built in {FX}")
    if built != 6:
        raise RuntimeError("Niagara effect build failed; see LogMetalFX in the editor log")


if __name__ == "__main__":
    build()
    log("DONE")
