# Sample locomotion controller migration

Target: `/GAR/Core/AnimationInstances/AB_Gar`.
Source: the currently loaded `SandboxCharacter_Mover_ABP` and
`CHT_MoverCharacterAnimations_PoseMatch` in this GameAnimationSample project.

## Scope

- Keep the user's copied state controller / common Blend Stack, Steering order, and GAR overlay / montage path.
- Copy the 12 state-entry, selection, completion, play-rate, and blend-space helper functions required by that controller.
- Convert gameplay-state enum comparisons and 50 Chooser enum columns to GameplayTag predicates/queries. Equal, not-equal, wildcard, and multi-enum OR rows retain their runtime meaning.
- Add an animation-local `BlendStackLocomotion` context to `UGarAnimationInstance`. Its state comes from GAR's existing ASC tags and Mover data; it never writes animation-direction tags to the ASC.
- Keep gameplay authority in the existing abilities/Mover. Sliding now has Sample-style animation states; rolling retains its Montage path. Remove the Sample-only debug-flight state from the pasted controller.
- Reuse existing GAR assets without overwriting them. Add 399 assets, including 366 animation sequences, the Chooser and its six owned databases, one standalone database, six schemas, two blend spaces, two structs, curve compression settings, and handplant audio dependencies.

The source-to-target inventory is [LocomotionMigrationAssets.json](LocomotionMigrationAssets.json).
`exists: true` means the migration reused an existing GAR asset; `false` means a newly duplicated asset.
This inventory records this migration only, not every difference between GAR and the latest Sample.

## State mapping

| Sample concept | GAR selection context |
| --- | --- |
| OnGround / InAir | `Gar.LocomotionMode.Grounded` / `.InAir` |
| Traversing | `Gar.LocomotionAction.Traversal` |
| Sliding | `Gar.LocomotionAction.Sliding` |
| Stand / Crouch | `Gar.Stance.Standing` / `.Crouching` |
| GAR Prone | `Gar.Stance.Lying` (including `.Front` / `.Back`) |
| Walk / Run / Sprint | `Gar.Gait.Walking` / `.Running` / `.Sprinting` |
| Moving / Idle | Existing GAR `IsMoving()` / its inverse |
| F / B / LR / LL / RL / RR | Animation-local `Gar.Animation.MovementDirection.*` tags |

`LastFrame` is the previous animation update. `Recent` is the last state that survived
the debounce interval: 0.2 seconds for movement mode, 0.1 seconds for movement direction.
It is deliberately not an alias for `PreviousGameplayTags`.

`UpdateBlendStackLocomotion` runs immediately after the existing GAR `Update_Trajectory`.
The trajectory predictor itself is unchanged. Added data includes future facing, angular
velocity/circling, direction history, and slope. Direction uses the Sample's standard strafe
thresholds with hysteresis (60/120 degrees from forward/back, 40/140 while strafing).
Sample's debug strafe/aim-style console switches are not imported.

The new Chooser's output tags update `CurrentDatabaseTags`, preserving the existing GAR
foot-placement and start/spin consumers. The four node references inside the copied
per-animation graph target its connected `State Machine Blend Stack Input`.

## Animation adaptation

- New locomotion clips receive missing GAR `pose_*` and `enable_footik_*` curves. Existing
  authored curves, contact, phase, speed, and warping curves are retained. These categorical
  pose defaults are an initial integration, not a per-frame artistic retuning of every clip.
- Absent layering curves retain GAR's existing zero-default behavior.
- Footstep notifications use GAR's existing Walk/Run/Scuff effects. Handplant notifications
  use the imported Sample handplant sound attached to the corresponding hand.
- `UGarAnimNotifyState_BlendStackTransition` replaces Sample early-transition notifications.
  It preserves the always / gait-not-equal condition and the re-transition / to-loop destination,
  and ignores notifications from blending-out sources.
- Baked Sample animation modifiers are detached from the new clips. Their baked curves remain.
- GAR animation sequences use the `A_` prefix; the Sample `/Game/` originals retain `M_`.
- Skeleton and mirror references target the existing GAR skeleton/mirror table. Existing
  skeleton assets and blend profiles are not overwritten.
- Pose History also collects `hand_l` and `hand_r`, as required by the imported traversal schema.
- The imported Run slope BlendSpace enables sync-phase matching, as required by Pose Search.

## Migration/tooling caveat

`FPoseSearchColumn` intentionally clears its row-to-database references in its copy constructor.
Do not copy an entire `ColumnsStructs` array to replace a few enum columns. Replace only those
columns, preserve/restore pose-match rows, and rebuild the non-serialized row/database mapping.
An empty search index can report an asynchronous failure even when the earlier build log says
“BuildIndex Succeeded”; check nonzero database asset counts and final build status.

The migration uses the editor-only `UGarBlueprintMigrationLibrary` and staged Python scripts
under the project's `Tools/ue_scripts`. The graph/asset-copy stages never save automatically.
Existing assets are not overwritten by the asset-copy stage. Do not replay stages blindly on
already migrated graphs. The separate `rename_gar_animation_prefix.py` uses Asset Tools, whose
rename operation saves renamed assets and their referencing packages automatically.

## Verification status

- Normal `GameAnimationSampleEditor Win64 Development` build succeeded, and the editor was restarted.
- AB_Gar compiled with zero errors and zero warnings after a clean-session reload.
- Chooser input-binding validation returned no errors.
- All 399 new assets exist on disk; their Asset Registry hard/soft package dependencies contain
  no `/Game/` references.
- Final 35-second PIE probe: idle loop/break, locomotion transitions/loop, normal jump, extended
  airtime and landing. The extended jump modifies only the PIE instance's jump settings and
  restores them; it is not a gameplay change. All seven logical states were visited, 17 GAR
  clips/blend spaces were selected, and `NoValidAnim` remained false throughout the final probe.
  In-air samples were exclusively In Air Transition / In Air Loop. No Blueprint runtime errors
  occurred in this final interval. This is state/selection validation, not a visual review of
  every clip or a multiplayer/network test.
- The Live Coding-only `bIsActionRunning` Layering error disappeared after the normal build/restart.
- Internal database mappings were restored after detecting the copy-constructor behavior.
  All six owned databases completed indexing successfully, with 4 / 16 / 90 / 50 / 169 / 78
  animation entries (idle transitions / loops / spins / stops / transitions / traversal).
  The standalone idle database also completed indexing, with six entries. The final PIE probe
  ran after reindexing completed; an earlier probe overlapped BlendSpace-triggered reindexing.
- `GAR.Animation.BlendStack.StateHistory` passed in the editor.
- A mixed-graph node-export audit triggered an editor assertion. All migration assets
  had already been saved. `ExportNodes` now rejects mixed graphs before invoking the engine
  exporter. Normal build, editor restart and clean-session validation completed afterward.

The whole AB is not completely independent of Sample metadata: the existing legacy
`S_ChooserOutputs` reference remains, and the copied selection function retains an unused
`StateMachineState` input typed as `E_ExperimentalStateMachineState`. That input has no graph
connections and does not drive selection. Grounded/in-air/stance/gait/direction predicates and
the Chooser use GAR tags; no Sample animation database reference remains in AB_Gar's package
dependencies. Removing unused signature/legacy metadata is separate from this migration.

## Backup

The user's original saved `AB_Gar.uasset` and pre-migration in-memory T3D exports were retained in
`C:/Users/Owner/AppData/Local/Temp/GarLocomotionMigration-e04b30da` before graph edits.
No original Sample assets were modified.

## Animation prefix normalization (2026-09-12)

- Renamed all 386 GAR `M_` AnimSequence assets to `A_` using Unreal Asset Tools,
  including the previously present Relaxed idle-break and slide clips. No name collisions.
- Updated the six referencing packages (Chooser, three standalone databases and two
  BlendSpaces). No old-name assets, redirectors or hard/soft package referencers remain.
- All 386 sequences retain identical duration, frame count, float-curve key times/values
  and notify configuration. Sample `/Game/` originals remain unchanged.
- The inventory's target paths and the asset-copy destination rule now use `A_`.
- Post-rename verification: AB_Gar compiled with 0 errors / 0 warnings, Chooser validation
  returned no errors, all nine affected standalone/owned database indexes are ready,
  and no dirty packages remain. No post-rename PIE/visual test was run.
- Pre-rename copies of the 386 sequences, six referencers and AB_Gar (393 files) are in
  `C:/Users/Owner/AppData/Local/Temp/GarAnimationPrefix-b16d9fe9ac764a52a45537dd32cf608d`.

## GAR special states (2026-09-12)

- Added `Slide Transition`, `Slide Loop`, and GAR's `Prone` to the existing State Controller
  (ten states total). They drive the same common Blend Stack; no parallel legacy locomotion
  graph or second full-body pose selector was added. The Default Slot, layering and migrated
  Procedural_Feet path remain connected as before.
- Correction to the initial migration assumption: GAR's Sliding ability does **not** play a
  Montage. It owns gameplay/movement lifetime; animation was previously selected by the old
  GAR Chooser/MM path. Rolling really does use `AM_Gar_Roll` and remains unchanged.
- Slide entry/loop functions and completion rules come from Sample. The stale Sample binding
  `OnStateEntry_TransitionToSlide` is corrected to the actual `OnStateEntry_SlideTransition`.
  Feet-out/knees-out selection uses the ability's existing `Gar.SlidingAction.KneedsOut` tag
  (historical spelling preserved), rather than Sample's movement-direction enum/history.
  Authored start times and blend settings are retained. Normal Sample slide-exit selection
  remains in the idle/locomotion Chooser tables.
- Direct selection of the slide loop sequences initially produced `Loop=false`. The GAR
  `OnStateEntry_SlideLoop` function now explicitly enables looping after selection, preserving
  the other selected inputs and leaving sequence assets untouched. Short flat-ground slides
  may legitimately end during the long authored entry clip, before reaching Slide Loop.
- GAR remains physically Grounded during Sliding. The Grounded-exit alias therefore reads
  the action-aware `BlendStackLocomotion.MovementMode`, not the raw Grounded tag. Otherwise
  Slide and Locomotion states oscillate every frame. Traversal retains priority over Sliding
  in that animation-local mode; neither operation writes new tags to the ASC.
- Prone entry matches the hierarchical Lying tag, requires Grounded, and excludes Sliding /
  Traversal. Leaving prone returns through the ground-state conduit or the In Air alias.
  Sliding can take over from Prone. Front/back and idle/moving do not require separate states.
- `OnUpdate_Prone` evaluates the existing `CHT_PoseSearchDatabases` and calls engine
  `PoseSearchLibrary.MotionMatch` using GAR's `PoseHistory`. It searches continuously, with
  the shared stack's current asset/time, previous mirroring/database, and database-change
  invalidation. The pure search result is captured once per update in `ProneSearchResult`.
  Continuing results do not restart playback; a new result updates `BlendStackInputs` and
  forces the next stack blend, including jumps within the same animation asset.
- Existing GAR prone, face-up, crawl and stance-transition assets/DBs are reused without
  modification. The old Chooser may also select its existing stand/crouch transition DBs
  during getting up/down. Selected database tags still update `CurrentDatabaseTags`.
  Mirroring follows the prone search result only while StateName is Prone, so it cannot leak
  into subsequent Sample states. Existing GAR mirror-table and blend-time logic are reused.
- No gameplay ability, Mover physics, foot Control Rig, or animation sequence is edited by
  this special-state repair. Runtime C++ changes are limited to the animation-local Sliding
  mode mapping; the editor helper adds schema-aware connections for non-K2 state nodes.

Staged tooling: `Tools/ue_scripts/migrate_gar_special_states.py`. Do not replay the copy / state
creation methods on an already migrated AB. Bounded PIE probes are in
`verify_gar_special_states.py` and `verify_gar_slide_loops.py`; these restore test settings and
end their own PIE session. They test state/asset selection, not keyboard bindings or networking.

Pre-repair AB, Chooser and runtime source backups:
`C:/Users/Owner/AppData/Local/Temp/GarSpecialStates-2b25ddd6a0a24fa49252eaece91127ce`.

Special-state verification:

- Normal Editor C++ build succeeded; AB compiled with 0 errors / 0 warnings and both modified
  assets were saved. Chooser input validation returned no errors.
- Saved-asset 30-second PIE probe: front/back prone idle and moving, getting up/down, both slide
  entries/exits, rolling Montage and jump/landing; no invalid selections. Prone continued its
  current search pose on 231 of 244 sampled frames rather than restarting every frame.
- Final 23-second extended-slide PIE probe: both `A_Relaxed_Slide_FootOut_Loop` and
  `A_Relaxed_Slide_KneesOut_Loop` reached Slide Loop with the looping flag true, then returned
  to normal movement. No invalid selections. The test altered deceleration/speed only on its
  temporary PIE movement-mode object and restored those values before ending PIE.
- Final 35-second normal-locomotion regression probe visited all seven original states,
  including Idle Break and In Air Loop. Invalid selections: zero; airborne samples were
  exclusively In Air Transition / In Air Loop. Combined probes exercised all ten states.
- Final asset audit: AB is UpToDate; Chooser validation is empty; all six owned Sample-style
  DBs and six existing prone DBs are ready; no dirty content packages remain. Neither edited
  package adds a Sample animation/database/schema dependency.
- `GAR.Animation.BlendStack.StateHistory` passed after the repair. Temporary foreground-
  throttling changes for automation/PIE were restored.
- Earlier probes exposed the Grounded/Sliding oscillation and the non-looping flag; both were
  corrected. Other early probes overlapped database reindexing after Blueprint compilation.
  Final successful probes used the ready indexes; no missing animations were silently added
  as fallbacks. These checks do not replace visual playtesting or multiplayer testing.

## Native locomotion structs (2026-09-12)

Only the following two Blueprint structs were migrated; the other seven GAR user-defined
structs and the unused Sample `S_ChooserOutputs` local declarations are outside this change.

- `S_BlendStackInputs` → `/Script/GAR.GarBlendStackInputs`, defined as `FGarBlendStackInputs`
  in `Source/Gar/Public/State/GarBlendStackInputs.h`.
- `S_CHT_MoverCharacterAnimations_OUT` → `/Script/GAR.GarLocomotionChooserOutput`, defined
  as `FGarLocomotionChooserOutput` in `Source/Gar/Public/State/GarLocomotionChooserOutput.h`.

Both remain BlueprintType/editable, with double-precision times and name-array animation
labels. Blend Stack defaults remain zero time / Linear; Chooser defaults remain 0.3 seconds /
QuadraticInOut. Chooser `BlendProfile` is still a name resolved against the skeleton, while
Blend Stack `BlendProfile` remains an object reference. The native bool is named `bLoop`.
No locomotion, ability, animation, or procedural-foot behavior was intentionally changed.

`GarLocomotionStructMigration.cpp` provides the narrowly scoped, unsaved editor migration.
It maps the original GUID-suffixed members to native names, updates variable/local declarations,
split pins, Make/Break/Set-fields nodes, property-access paths and Chooser context/bindings.
Each output FInstancedStruct is converted field by field, including default/fallback values.
Other Chooser columns are visited **in place**: copying FPoseSearchColumn would clear its
database references. CDO values are retained via text because native and UDS layouts differ.

Pre-migration AB, Chooser and both UDS backups:
`C:/Users/Owner/AppData/Local/Temp/GarNativeLocomotionStructs-ff2355d7943e487a90b0c38c72b8cea5`.

Structural verification (`Tools/ue_scripts/audit_gar_native_structs.py`):

- Normal Editor C++ build succeeded; AB compile: 0 errors, 0 warnings; Chooser validation empty.
- All 1,127 nodes and 1,938 directed K2 pin links retained; normalized link/default hashes match.
- All 50 Chooser tables retain row order/results and animation/database references.
- All 572 output/default/fallback instances (2,860 fields) retain their values; independent
  normalized per-table output hashes match before/after, including omitted native defaults.
- Owned database entries/schemas match; all six owned and six existing prone DB indexes ready.
- Graph exports contain neither old type; after saving/rescanning both old assets have zero
  package referencers. Only AB_Gar and its locomotion Chooser were changed by the conversion.

The earlier copy/graph-creation scripts and asset manifest document staged Sample import, not
an idempotent native-struct migration. Do not replay them on this AB or recreate the retired
UDS assets. The PIE probes accept both historical GUID-suffixed and native `Anim` field names.

Runtime verification after saving the native types:

- 30-second special-state probe: front/back prone idle/movement, getting up/down, both slide
  entries, roll Montage and jump/landing; no invalid selections. Prone continued its current
  pose on 202/214 sampled frames; the roll Montage played on 14 sampled frames.
- 23-second extended-slide probe: both authored loop clips reached Slide Loop with `bLoop=true`,
  then exited normally; no invalid selections.
- 35-second locomotion probe: all seven original states visited, including Idle Break and
  In Air Loop; 0 invalid selections. All 59 airborne samples used In Air states. Together
  with the special-state probes, all ten states were exercised.
- Both now-unreferenced UDS assets were deleted through the editor, with backups retained.
  Deletion temporarily dirtied four Control Rig packages through editor recompilation; those
  were reloaded from disk, **not saved**. No Control Rig or Sample source asset was changed.
- PIE frame-limit/throttling overrides were restored; no dirty packages remained before
  the final cold-reload verification.
- Cold restart with both UDS files absent: all connection/default, Chooser output/reference,
  and database-entry hashes still match the pre-migration snapshot. Both AB variables resolve
  to native `GarBlendStackInputs`; AB is UpToDate, Chooser validation empty, remaining GAR UDS
  count is seven, no dirty packages and no PIE session remain.

## Unused Sample Chooser locals cleanup (2026-09-12)

Removed only `ChooserOutputs`, `MotionMatchSelectionOutput`, and `ChosenOutputs` from
`AB_Gar.SetBlendStackAnimFromChooser` after checking their names and variable GUIDs have
no node references. The six other locals, including native `CHT_OUT`, remain unchanged.
The Sample `/Game/Blueprints/Data/S_ChooserOutputs` asset itself is untouched; GAR's seven
editor/example structs are also unchanged.

AB compiled with 0 errors / 0 warnings. Before/after audits match for all 1,127 nodes,
1,938 directed K2 connections, pin defaults, Chooser rows/references and database data.
After saving AB and rescanning its package, its dependency on `S_ChooserOutputs` is gone.
Chooser validation is empty and no dirty packages remain. No runtime logic was changed.

Pre-cleanup AB backup:
`C:/Users/Owner/AppData/Local/Temp/GarUnusedChooserLocals-b224accbc66c4ee89426e7050f789865/AB_Gar.uasset`.
