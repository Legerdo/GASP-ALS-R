"""Idempotently migrate GAR's character defaults to Physics Control.

Run through Unreal Editor so the uassets are written by Unreal rather than by an
external binary editor:

UnrealEditor-Cmd.exe GameAnimationSample.uproject -run=pythonscript \
    -script=Plugins/GASP-ALS-R/Scripts/MigrateGarPhysicsControl.py -EnablePlugins=PythonScriptPlugin
"""

import unreal


SOURCE_PCA = "/Game/Characters/UEFN_Mannequin/Rigs/PCA_SandboxCharacter"
GAR_PCA = "/GAR/Characters/UEFN_Mannequin/Rigs/PCA_Gar_Character"
GAR_PCA_OBJECT = GAR_PCA + ".PCA_Gar_Character"
GAR_PHYSICS_ASSET = "/GAR/Characters/UEFN_Mannequin/Meshes/PA_UEFN_Mannequin"
GAR_CHARACTER = "/GAR/Core/B_Gar_Character"
GAR_GETTING_UP_ABILITY = "/GAR/Core/Abilities/GA_Gar_Action_GettingUp"
GAR_LEGACY_PHYSICAL_ANIMATION_BLUEPRINTS = (
    "/GAR/Example/Projectile/B_GarExtra_Projectile",
    "/GAR/Example/UI/W_GarExtra_Hud",
)


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError("Required asset could not be loaded: {}".format(path))
    return asset


def load_or_duplicate_pca():
    existing = unreal.load_asset(GAR_PCA_OBJECT)
    if existing:
        return existing

    source_pca = require_asset(SOURCE_PCA)
    duplicate = unreal.EditorAssetLibrary.duplicate_asset(source_pca.get_path_name(), GAR_PCA)
    if not duplicate:
        raise RuntimeError("Unable to duplicate {} to {}".format(SOURCE_PCA, GAR_PCA))
    return duplicate


def migrate_blueprint_references(asset_path):
    blueprint = require_asset(asset_path)
    if not unreal.GarPhysicsControlMigrationLibrary.migrate_legacy_physical_animation_references(blueprint):
        raise RuntimeError("Unable to compile {}".format(asset_path))
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError("Unable to save {}".format(asset_path))


def migrate():
    physics_control_asset = load_or_duplicate_pca()
    physics_control_asset.set_editor_property("physics_asset", require_asset(GAR_PHYSICS_ASSET))
    unreal.EditorAssetLibrary.save_loaded_asset(physics_control_asset, only_if_is_dirty=False)

    blueprint = require_asset(GAR_CHARACTER)
    generated_class = unreal.EditorAssetLibrary.load_blueprint_class(GAR_CHARACTER)
    if not generated_class:
        raise RuntimeError("Generated class could not be loaded: {}".format(GAR_CHARACTER))

    character_cdo = unreal.get_default_object(generated_class)
    component = character_cdo.get_editor_property("physics_control")
    if not component:
        raise RuntimeError("B_Gar_Character has no PhysicsControl native component")

    component.set_editor_property("physics_control_asset", physics_control_asset)
    component.set_editor_property("base_control_profile_name", unreal.Name("PhysicalAnimation"))

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError("Unable to save {}".format(GAR_CHARACTER))

    migrate_blueprint_references(GAR_GETTING_UP_ABILITY)
    for asset_path in GAR_LEGACY_PHYSICAL_ANIMATION_BLUEPRINTS:
        migrate_blueprint_references(asset_path)

    unreal.log("Migrated GAR physics control defaults: {} -> {}".format(GAR_CHARACTER, GAR_PCA))


migrate()
