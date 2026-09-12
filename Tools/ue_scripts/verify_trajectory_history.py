"""Bounded PIE regression for Mover trajectory history on a flat GAR test level.

Run after starting a fresh PIE session. Exercises idle, movement and jump/landing;
restores performance settings and ends only the captured PIE world. No asset writes.
"""
import json
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world and 'UEDPIE_' in world.get_path_name()
pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
mesh = next(m for m in pawn.get_components_by_class(unreal.SkeletalMeshComponent)
            if m.get_anim_instance() and m.get_anim_instance().get_class().get_name() == 'AB_Gar_C')
anim = mesh.get_anim_instance()
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
old_fps = unreal.SystemLibrary.get_console_variable_float_value('t.MaxFPS')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
unreal.SystemLibrary.execute_console_command(world, 't.MaxFPS 30')
trajectory_probe = dict(elapsed=0.0, done=False, error=None, observations=0, idle_observations=0,
                        max_idle_history_z_error=0.0, max_idle_past_velocity_z=0.0,
                        max_idle_current_z_error=0.0, states={}, air_states={}, invalid=0,
                        min_history_z=None, max_history_z=None, samples=[])
trajectory_flags = set()
next_observation = 0.0


def finish_trajectory(error=None):
    if trajectory_probe['done']:
        return
    trajectory_probe.update(done=True, error=error)
    unreal.unregister_slate_post_tick_callback(trajectory_handle)
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    current = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    unreal.SystemLibrary.execute_console_command(
        current or unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),
        't.MaxFPS ' + str(old_fps))
    print('GAR_TRAJECTORY_PROBE', json.dumps(trajectory_probe))
    if current == world:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()


def trajectory_tick(delta):
    global next_observation
    try:
        if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() != world:
            finish_trajectory('PIE ended externally')
            return
        trajectory_probe['elapsed'] += delta
        t = trajectory_probe['elapsed']
        if t >= 5 and 'fps' not in trajectory_flags:
            trajectory_flags.add('fps')
            unreal.SystemLibrary.execute_console_command(world, 't.MaxFPS 60')
        if 9 <= t < 11:
            pawn.add_movement_input(unreal.Vector(-1, 0, 0), 1.0, True)
        if t >= 12 and 'jump' not in trajectory_flags:
            trajectory_flags.add('jump')
            pawn.jump()
        if t >= 12.2 and 'release' not in trajectory_flags:
            trajectory_flags.add('release')
            pawn.stop_jumping()
        if t >= next_observation:
            next_observation = t + 0.1
            samples = anim.get_editor_property('Trajectory').get_editor_property('samples')
            past = [s for s in samples if s.get_editor_property('time_in_seconds') <= 0]
            future = [s for s in samples if s.get_editor_property('time_in_seconds') > 0]
            state = str(anim.get_editor_property('StateName'))
            trajectory_probe['states'][state] = trajectory_probe['states'].get(state, 0) + 1
            trajectory_probe['invalid'] += int(anim.get_editor_property('NoValidAnim'))
            if abs(pawn.get_velocity().z) > 10:
                trajectory_probe['air_states'][state] = trajectory_probe['air_states'].get(state, 0) + 1
            trajectory_probe['observations'] += 1
            if 2 <= t < 9 and past and future:
                mesh_z = mesh.get_world_location().z
                heights = [s.get_editor_property('position').z for s in past]
                history_error = max(abs(z - mesh_z) for z in heights)
                current_error = abs(future[0].get_editor_property('position').z - mesh_z)
                past_velocity_z = abs(anim.get_editor_property('Trj_PastVelocity').z)
                trajectory_probe['idle_observations'] += 1
                for key, value in [('max_idle_history_z_error', history_error),
                                   ('max_idle_current_z_error', current_error),
                                   ('max_idle_past_velocity_z', past_velocity_z)]:
                    trajectory_probe[key] = max(trajectory_probe[key], value)
                trajectory_probe['min_history_z'] = min(heights)
                trajectory_probe['max_history_z'] = max(heights)
                if len(trajectory_probe['samples']) < 4:
                    trajectory_probe['samples'].append(dict(t=t, mesh_z=mesh_z, oldest_z=heights[0],
                                                           current_z=future[0].get_editor_property('position').z))
        if t >= 18:
            finish_trajectory()
    except Exception as exc:
        finish_trajectory(str(exc))


trajectory_handle = unreal.register_slate_post_tick_callback(trajectory_tick)
print('GAR_TRAJECTORY_PROBE_STARTED')
