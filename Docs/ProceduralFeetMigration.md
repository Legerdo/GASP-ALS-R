# Procedural feet migration

Source: `/Game/Blueprints/SandboxCharacter_Mover_ABP:Procedural_Feet` and
`/Game/Blueprints/ControlRigs/CR_Biped_FootPlacement` in this project (UE 5.8.2).

## GAR adaptation

- `AB_Gar:Procedural_Feet` replaces the old Foot Placement for Mover / Leg IK segment.
  The input still comes after Offset Root Bone; the remaining GAR animation graph is retained.
- The movement-mode enum blend becomes GAR's `Blend Poses by Gameplay Tag`. Sliding,
  traversal and in-air use the basic Leg IK branch, with the Sample's blend durations.
  The unrelated Sample debug-flying branch is not imported.
- The basic branch retains GAR's separate left/right Leg IK nodes and their
  `enable_footik_l` / `enable_footik_r` alpha curves.
- The procedural branch uses `/GAR/Core/ControlRigs/CR_Gar_Biped_FootPlacement`.
  It uses the Sample's `contact_l` / `contact_r` for foot pinning; `footspeed_*` is not
  required by this branch. Slope warping and world-Z damping follow the grounded tag.
- Compatibility for old Neutral clips: only when an authored contact curve is absent,
  a valid `footspeed_* < 60 cm/s` supplies contact. An explicit authored contact value
  of zero remains zero, and missing both curves does not invent contact. No animation
  assets are rewritten for this compatibility path.
- GAR's existing action, ragdoll and lying-pose exclusion remains in front of the
  procedural branch. `FootPlacement_Enable` provides a manual AB default/runtime A/B
  switch (true = rig, false = basic Leg IK); `FootPlacement_Debug` defaults to false.
- The Rig's own full-leg IK also honors the independent GAR enable curves. Before
  solving, it caches the incoming leg pose. After solving, it blends the disabled
  fraction back in local space for thigh/calf/foot/toe, leaving the pelvis policy intact.
  At curve value 1 the Sample result is unchanged. Contact/pinning weight is also gated
  by that leg's enable curve.
- Mover's `OnBasedMovementApplied` delegate supplies the world-space moving-base delta.
  The consumed delta is reset after pose evaluation. No CMC cast is used.
- The Sample's become-relevant reset and 50 cm teleport threshold are retained.
  Previous mesh transform and reset consumption are updated after pose evaluation.
- The Rig preview mesh is remapped to GAR. The original Sample assets are not edited.
  `ControlRig` is an explicit GAR plugin dependency; no C++ runtime changes are needed.

## Tooling / backup

`Tools/ue_scripts/migrate_gar_procedural_feet.py` contains explicitly invoked stages:
`backup`, `copy`, `mover_inputs`, `rig_leg_weights`, `rig_legacy_contacts`, `defaults`. Do not replay copy stages
on an already migrated Blueprint. Intermediate compiler diagnostics are expected while
the generated Blueprint skeleton and Sample input types are being replaced; saving is
performed separately only after validation.

The saved AB_Gar and in-memory AB/Sample/Rig exports before this migration are in
`C:/Users/Owner/AppData/Local/Temp/GarProceduralFeet-85pfv4xn`.

An initial RigVM variable-add operation crashed because it was passed `FRigPose` instead
of `/Script/ControlRig.RigPose`. The saved AB_Gar's SHA-256 was verified unchanged after
the crash. The migration script now resolves and validates the full struct object path.

## Verification

- AB_Gar: final compiler result 0 errors / 0 warnings. Rig: `BS_UpToDate`.
- Both assets saved through the editor. Rig package dependencies contain no `/Game/`
  references; AB_Gar's rig reference points to the GAR copy.
- Transient-rig tests disable each leg independently: local-transform error is zero
  for the disabled leg while the opposite leg is still corrected.
- Transient-rig tests confirm legacy speed 0 pins and speed 120 does not pin, while
  authored contact 0/1 overrides either legacy speed. All four cases pass for both feet.
- A 35-second PIE probe after all six owned DB indexes were ready visited all seven
  locomotion states with `NoValidAnim = 0` and no Blueprint runtime errors. In-air samples
  were exclusively In Air Transition / In Air Loop. Both feet reached pin weight 1 and
  pinned during movement. No non-finite bone positions were observed.
- The first post-restart probe overlapped asynchronous DB indexing and recorded 12
  selection misses; the warmed repeat recorded none. No DB assets were changed here.
- Final 14-second PIE mode test: 234 crouched frames, 104 legacy-contact pinned frames,
  45 disabled frames with zero Rig pose-cache updates, and 44 pinned frames after
  re-enabling. `NoValidAnim = 0`, no runtime error. This verifies the basic-IK bypass,
  reactivation reset, and the old Neutral crouch clips after the compatibility change.
- This is functional verification, not a claim that visible foot sliding is eliminated
  on every clip, slope, moving platform, or multiplayer setup. Toe-displacement samples
  across replant/teleport boundaries were not treated as a sliding-quality benchmark.
- A direct standalone comparison to the unloaded legacy Sample generated class was not
  usable because that class returned an empty hierarchy outside its AnimGraph setup;
  it is not reported as a passed pose-equivalence test.
- The transient reference-rig test marked the original Sample package dirty. That
  package alone was reloaded without saving; final dirty-package list is empty.
