"""One-time profile-Chooser migration, executed in Unreal Editor; saves four named assets.

Rebuilds CT_Gar_PAProfile's missing output column with a native two-name struct.
Missing per-state PCA profiles are seeded from existing baselines, not retuned.
Existing PA profiles and existing per-state PCA tuning are preserved. Do not rerun
after editing the Chooser: its rows are reset to the migration's initial mapping.
"""
import unreal

assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'End PIE first'
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Save or discard pending asset edits first'
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Save or discard pending map edits first'

helper = unreal.get_default_object(unreal.load_class(None, '/Script/GAREditor.GarPhysicsControlMigrationLibrary'))
blueprint = unreal.load_asset('/GAR/Core/B_Gar_Character')
table = unreal.load_asset('/GAR/Core/Data/Character/CT_Gar_PAProfile')
pca = unreal.load_asset('/GAR/Characters/UEFN_Mannequin/Rigs/PCA_Gar_Character')
pa = unreal.load_asset('/GAR/Characters/UEFN_Mannequin/Meshes/PA_UEFN_Mannequin')
assert all((blueprint, table, pca, pa)), 'Missing GAR migration asset'

with unreal.ScopedEditorTransaction('Restore GAR Physics Control profile Chooser'):
    assert helper.call_method('CreateProfileVariants', (pca,))
    assert helper.call_method('EnsureFreeConstraintProfile', (pa,))
    assert helper.call_method('ConfigureProfileChooser', (table,))
    blueprint.modify()
    character = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/GAR/Core/B_Gar_Character'))
    component = character.get_editor_property('physics_control')
    component.modify()
    component.set_editor_property('profile_chooser', table)
    component.set_editor_property('base_control_profile_name', unreal.Name('PhysicalAnimation'))
    settings = component.get_editor_property('ragdoll_settings_by_tag')
    for name in ('Gar.LocomotionAction.FreeFalling', 'Gar.LocomotionAction.Dying'):
        tag = unreal.GameplayTag()
        tag.import_text('(TagName="' + name + '")')
        if tag not in settings:
            settings[tag] = component.get_editor_property('default_ragdoll_settings')
    component.set_editor_property('ragdoll_settings_by_tag', settings)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

audit = unreal.get_default_object(unreal.load_class(None, '/Script/GAREditor.GarBlueprintMigrationLibrary'))
assert not audit.call_method('ValidateChooser', (table,)), 'Chooser has invalid bindings'
assert audit.call_method('ExportProperty', (blueprint, 'Status')) == 'BS_UpToDate', 'Character Blueprint failed to compile'
for asset in (table, pca, pa, blueprint):
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()
print('GAR_PHYSICS_PROFILE_CHOOSER_MIGRATED')
