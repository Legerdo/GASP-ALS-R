"""One-time baseline setup, run via UnrealEditor-Cmd -run=pythonscript.

Explicitly replaces PhysicalAnimation/Ragdoll in GAR's PCA. Do not rerun after
hand-tuning those profiles unless resetting to the sample-derived baseline is intended.
"""
import unreal

pca = unreal.load_asset("/GAR/Characters/UEFN_Mannequin/Rigs/PCA_Gar_Character")
if not pca or not unreal.GarPhysicsControlMigrationLibrary.configure_baseline_profiles(pca):
    raise RuntimeError("Could not configure GAR Physics Control Asset")
if not unreal.EditorAssetLibrary.save_loaded_asset(pca, only_if_is_dirty=False):
    raise RuntimeError("Could not save GAR Physics Control Asset")

blueprint_path = "/GAR/Core/B_Gar_Character"
blueprint = unreal.load_asset(blueprint_path)
cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(blueprint_path))
component = cdo.get_editor_property("physics_control")
component.set_editor_property("physics_control_asset", pca)
component.set_editor_property("default_control_profile_name", unreal.Name("PhysicalAnimation"))
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
    raise RuntimeError("Could not save GAR character defaults")
unreal.log("GAR Physics Control baseline configured and saved")

ability = unreal.load_asset("/GAR/Core/Abilities/GA_Gar_Action_Unconsious")
if not unreal.GarPhysicsControlMigrationLibrary.migrate_ragdoll_velocity(ability):
    raise RuntimeError("Could not migrate ragdoll settling check")
if not unreal.EditorAssetLibrary.save_loaded_asset(ability, only_if_is_dirty=False):
    raise RuntimeError("Could not save ragdoll ability")
