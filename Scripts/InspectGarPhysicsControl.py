"""Read-only inspection of the sample and GAR; output goes to Saved/PhysicsControlInspection."""
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_saved_dir()) / "PhysicsControlInspection"
output.mkdir(parents=True, exist_ok=True)

for path in (
    "/Game/Blueprints/SandboxCharacter_Mover_Ragdoll",
    "/Game/Blueprints/MovementModes/BP_MovementMode_Ragdoll",
    "/GAR/Core/B_Gar_Character",
    "/GAR/Core/Abilities/GA_Gar_Action_Unconsious",
):
    asset = unreal.load_asset(path)
    if not asset:
        continue
    text = unreal.GarPhysicsControlMigrationLibrary.export_blueprint_graphs(asset)
    (output / (asset.get_name() + ".graphs.txt")).write_text(text, encoding="utf-8")
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(output / (asset.get_name() + ".copy"))
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    unreal.Exporter.run_asset_export_task(task)

for path in (
    "/Game/Characters/UEFN_Mannequin/Rigs/PCA_SandboxCharacter",
    "/GAR/Characters/UEFN_Mannequin/Rigs/PCA_Gar_Character",
):
    asset = unreal.load_asset(path)
    lines = []
    for name in ("my_character_setup_data", "my_profiles", "parent_asset", "additional_profile_assets",
                 "my_initial_control_and_modifier_updates", "my_additional_controls_and_modifiers",
                 "my_additional_sets", "physics_asset"):
        try:
            value = asset.get_editor_property(name)
        except Exception as error:
            lines.append(name + ": " + str(error))
            continue
        if isinstance(value, unreal.StructBase):
            text = value.export_text()
        elif isinstance(value, unreal.Map):
            text = "\n".join(str(k) + "=" + v.export_text() for k, v in value.items())
        elif isinstance(value, unreal.Array):
            text = "\n".join(v.export_text() if isinstance(v, unreal.StructBase) else str(v) for v in value)
        else:
            text = str(value)
        lines.append(name + ":\n" + text)
    (output / (asset.get_name() + ".txt")).write_text("\n\n".join(lines), encoding="utf-8")

character_class = unreal.EditorAssetLibrary.load_blueprint_class("/GAR/Core/B_Gar_Character")
character = unreal.get_default_object(character_class)
component = character.get_editor_property("physics_control")
lines = ["PhysicsControl=" + component.get_path_name()]
for name in ("physics_control_asset", "default_control_profile_name", "control_profile_by_tag",
             "default_ragdoll_tag", "default_ragdoll_settings", "ragdoll_settings_by_tag"):
    value = component.get_editor_property(name)
    lines.append(name + "=" + (value.export_text() if isinstance(value, unreal.StructBase) else str(value)))
(output / "GarCharacterDefaults.txt").write_text("\n".join(lines), encoding="utf-8")

unreal.log("Physics Control inspection exported to " + str(output))
