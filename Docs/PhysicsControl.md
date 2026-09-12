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

`UGarPhysicsControlComponent.ProfileChooser` selects both a Physics Control Profile
and a PA Constraint Profile. `/GAR/Core/Data/Character/CT_Gar_PAProfile` retains its
legacy asset name but now reads `FGameplayTagContainer` and writes the native
`FGarPhysicsControlProfileChooserResult` (`ControlProfileName`, `ConstraintProfileName`).
The first matching Chooser row wins. `Default` is its fallback output.

| Tag condition, in priority order | Control Profile | Constraint Profile |
| --- | --- | --- |
| Dying | Dying | None (PA defaults) |
| FreeFalling | FreeFalling | None |
| Unconsious | Unconsious | None |
| Traversal | Traversal | Free |
| InAir | InAir | Free |
| Lying, including LyingFront/LyingBack | Lying | Free |
| Rolling **or** Sliding | Rolling | Free |
| Fallback | Default | Free |

This restores the selection structure, not the former eight physical behaviors.
The five normal profiles initially copy the existing `PhysicalAnimation` recipe;
the three ragdoll profiles copy `Ragdoll`. Their strengths/damping are not yet
individually tuned. Existing named PA profiles remain available to select in the
Chooser. The initial mapping preserves the previously working joint-limit policy.

`BaseControlProfileName` (`PhysicalAnimation`) is a reset recipe applied before a
selected Control Profile, not another tag mapping. Profiles are incremental;
the base recipe must reset any properties that state-specific recipes override.
Changing only the selected Constraint Profile still updates the joints.
`RagdollSettingsByTag` retains lifecycle settings such as freezing and blend time,
but no longer selects a Control Profile. The ragdoll task remains the lifecycle
owner: evaluation removes stale ragdoll action tags and inserts the active task's
tag, including on entry before the ability tag exists and on exit before it clears.

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

The initial Chooser mapping uses a new `Free` PA Constraint Profile for unrestricted
angular limits during normal animation; ragdoll uses PA default limits. These are
data choices, no longer forced angle-limit overrides in C++. Collision between
the two legs remains disabled normally and enabled for ragdoll. After every PA
profile change GAR disables its joint motors; Physics Control supplies the motors.
`None` explicitly restores default PA settings, rather than keeping the previous
profile. An unknown non-empty profile name is reported as an error.

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

`Scripts/MigrateGarPhysicsProfileChooser.py` performs the one-time Chooser migration
in Unreal Editor and saves only the character, Chooser, PCA and PA. It adds missing
variant profiles without overwriting existing tuning, and adds `Free` without
changing the old PA profiles. It **rebuilds the Chooser rows**, so do not rerun it
after editing the table unless that reset is intended.

`GAR.PhysicsControl.ProfileChooser` tests all eight selections, tag inheritance,
row priority, independent constraint output, and ragdoll entry/exit tag timing.

Profile-Chooser migration verified on UE 5.8.2, 2026-09-12: Editor Development
build succeeded; migration commandlet reported 0 errors and 0 warnings. Saved
assets were reloaded and all ten representative inputs (including both lying
stances and sliding) produced the expected profile pair. The seeded profiles'
values were confirmed equal to their existing baselines.

All seven `GAR.PhysicsControl` tests passed across the suite run and the isolated
`DownStateRecovery` rerun. The latter was rerun because an editor diagnostic
mistakenly connected to the test process and its Python errors contaminated the
first report; no test assertions were changed. The rerun had 0 errors and 5
asset-loading/network warnings. Reports: `Saved/Automation/PhysicsProfileChooser`
and `Saved/Automation/PhysicsProfileChooserDownState` in the host project.

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
