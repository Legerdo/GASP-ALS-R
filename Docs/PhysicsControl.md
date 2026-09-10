# Physics Control / Ragdoll

`PCA_Gar_Character` is a **Physics Control Asset** (`UPhysicsControlAsset`). Its
Preview Profiles buttons invoke the same named Control Profiles used at runtime;
they are not a separate set of preview-only settings.

## Ownership

| Asset | Role |
| --- | --- |
| `PA_UEFN_Mannequin` | Bodies, collision shapes, masses, joint anchors and default joint limits |
| `PCA_Gar_Character` | Control/body-modifier creation, sets, drive strengths, damping, gravity and simulation/blend settings |

The legacy PA **Physical Animation Profiles** are no longer read. Their spring
values are not numerically interchangeable with Physics Control strengths. The
PA **Constraint Profiles** are different: they describe the existing joints and
remain usable independently of Physics Control. A PCA does not replace a PA.
The PA selected in the PCA editor is for preview; runtime uses the mesh's PA.

## Baseline profiles

- `PhysicalAnimation`: simulated bodies with gravity disabled, enabled
  world-space and parent-space animation drives. World strengths 3, parent
  angular strength 15/damping ratio 3, feet world strengths 10.
- `Ragdoll`: simulated bodies with gravity enabled, world-space drives disabled,
  parent-space angular strength 10/damping ratio 3 (sample-style powered ragdoll).
  Set the parent-space angular strength to 0 for a limp ragdoll.
- `Kinematic`, if present, remains a preview/debug option; normal gameplay does
  not select it.

The two gameplay profiles explicitly reset All-set data/multipliers first, then
apply set-specific settings. Profiles are incremental recipes, not automatically
exclusive states. Both directions of a transition must restore their settings.

`UGarPhysicsControlComponent` owns transitions. Normally it selects
`DefaultControlProfileName` (`PhysicalAnimation`). Ragdoll action tags select
`DefaultRagdollSettings.ControlProfileName` (`Ragdoll`). `ControlProfileByTag` and
`RagdollSettingsByTag` provide extension points for future variants. The old three
ragdoll variants have not been reproduced.

To restore tag-dependent behavior later, author additional Control Profiles in the
PCA and map their names in `ControlProfileByTag` (normal animation) or
`RagdollSettingsByTag` (ragdoll actions). Reusing old PA profile names is fine,
but their Physics Control values must be authored/tuned anew. Neither the number
of profiles nor their names must match the PA. No legacy component or Chooser is
required for the baseline two-state setup.

Mesh collision must include physics, even when individual body modifiers use
QueryAndPhysics. Animation/bone refresh stays enabled off-screen because controls
need fresh animation targets. The mesh component transform remains Mover-owned;
physics affects its bodies, not the scene transform used by animation targets.
On ragdoll entry, simulation starts immediately so
the triggering event can also apply an impulse. On exit, a pose snapshot is saved
and animation target velocity feed-forward is suppressed for the transition frame.
The built-in ability's automatic settling check reads pelvis physics velocity via
`GetRagdollVelocity`, not Actor/capsule velocity. A stationary tracking capsule
does not mean that the body has stopped falling. Its existing automatic end
behavior is retained.

As in the sample, normal animation uses unrestricted angular joint limits and
disables collision between the two legs. Ragdoll restores PA default joint limits
and leg collision. GAR applies this runtime policy without adding a `Free` PA
Constraint Profile or modifying the PA asset. Legacy joint motors are disabled;
Physics Control supplies the motors.

## Mover

`ProduceInput` records pelvis position and a chest-derived heading in
`FGarCharacterMoverInputs`. The ragdoll movement mode consumes that snapshot;
it does not read the local mesh during simulation/resimulation. It sphere-traces
down from the pelvis, adjusts capsule height above the floor and follows without
a capsule sweep, matching `BP_MovementMode_Ragdoll`. GAR uses the current capsule
size and Mover up direction instead of the sample's fixed dimensions.

The names `Ragdolling` / `Ragdolling In Air` remain for existing Grounded/InAir
tag consumers; they share one follow algorithm. Stance-driven capsule resizing is
suspended during ragdoll so it cannot issue competing teleport effects.

## Tools and checks

`Scripts/ConfigureGarPhysicsControl.py` is a **one-time reset** to the baseline
above. Run through Unreal Editor, never through ordinary system Python. It replaces
the two named PCA profiles; do not rerun after manual tuning unless a reset is wanted.
`Scripts/InspectGarPhysicsControl.py` is read-only and exports diagnostic text to
the project's `Saved/PhysicsControlInspection` folder.

Automation tests: `GAR.PhysicsControl.InputSnapshot` and
`GAR.PhysicsControl.RuntimeTransitions`. The latter creates a temporary test map
and tests a real GAR character in PIE: impulse response/recovery and two consecutive
ragdoll/normal transitions (observing the active interval before the ability's
automatic exit). It is not a multiplayer or visual-quality certification.

Verified on UE 5.8.2, Windows Editor Development, 2026-09-10: build succeeded;
both tests passed in NullRHI PIE (asset-loading warnings remain). The impulse test
observed 18.27 cm peak pelvis displacement and 0.35 cm recovery error. Both ragdoll
cycles fell to the floor and restored physical animation. The generated report is
`Saved/Automation/PhysicsControl/index.json` in the host project.

## Known issue at this checkpoint

Manual play testing confirms always-on physical animation and ragdoll entry, but
reports a warp on ragdoll recovery. If the mesh ends up below the floor, it does
not return above ground; entering ragdoll again then moves the camera below the
floor and the character continues falling. The cause is not yet diagnosed or fixed.

The passing automation above does not establish safe post-recovery mesh placement:
it checks restored simulation/drives and the Mover mode, but does not assert that
the mesh remains above the floor after recovery. This regression needs reproduction
and a dedicated recovery/second-entry test before recovery can be considered reliable.
