# _VoECSExample (ECS + Flecs) — Step-by-step Guide

This sample demonstrates how to structure ECS gameplay (via **flecs**) and render lots of entities efficiently using **The Forge** (GPU instancing + per-frame buffers).

## 1) What this sample teaches

### ECS concepts (flecs)
1. **Components are plain data**
   - See: `PositionComponent`, `MoveComponent`, `SpriteComponent`, `WorldBoundsComponent`, `AvoidComponent`.

2. **Systems implement behavior**
   - Movement: `MoveSystem(ecs_iter_t* it)` updates `PositionComponent` from `MoveComponent`.
   - Avoidance: `AvoidanceSystem(ecs_iter_t* it)` modifies velocity + color when near “avoid” entities.

3. **Queries select subsets of entities**
   - Sprite entities query excludes `AvoidComponent`:
     - `gECSSpriteQuery` uses `EcsNot` on the `AvoidComponent` term.
   - Avoid entities query includes `AvoidComponent`:
     - `gECSAvoidQuery` uses `EcsAnd` on the same term.
   - Avoidance uses a nested query iterator to compare sprites vs avoiders.

4. **Scalability / multithreading**
   - Thread count is controlled by `ecs_set_threads(gECSWorld, ...)`.
   - Runtime toggle is wired via UI checkbox `gMultiThread`.
   - The avoidance system is marked as `multi_threaded = true` in the system descriptor.

### Rendering concepts (The Forge)
1. **ECS → packed instance data**
   - Each frame, ECS is progressed, then entities are iterated and written to `gSpriteData[]` (contiguous array).

2. **Per-frame GPU buffers (frames-in-flight)**
   - `gDataBufferCount = 2`.
   - `pSpriteVertexBuffers[gFrameIndex]` is updated each frame with just the data needed for rendering.

3. **Instanced drawing**
   - The draw call uses `cmdDrawIndexedInstanced(..., instanceCount = gDrawSpriteCount, ...)` so many entities render in one call.

## 2) How the frame works (high level)

1. `Init()`
   - GPU config + renderer setup:
     - `initGPUConfiguration(...)`
     - `initRenderer(...)`
     - `setupGPUConfigurationPlatformParameters(...)`
   - Create GPU resources (buffers, texture, pipeline).
   - Create ECS world and register components/systems/queries.
   - Create entities (sprites and avoiders).

2. `Update(deltaTime)`
   - Updates simulation:
     - `ecs_progress(gECSWorld, deltaTime * 3.0f);`
   - Builds render instance data into `gSpriteData[]` by iterating:
     - `gECSSpriteQuery`
     - `gECSAvoidQuery`
   - Updates `gDrawSpriteCount`.

3. `Draw()`
   - Acquire swapchain image.
   - Upload `gSpriteData` into `pSpriteVertexBuffers[gFrameIndex]`.
   - Record and submit GPU commands.
   - Issue a single instanced draw for all sprites.

4. `Exit()`
   - Destroy ECS queries/world.
   - Release renderer resources.
   - Call `exitGPUConfiguration()` (required to avoid config parsing leaks).

## 3) Build & run (Visual Studio)

1. Open the solution in Visual Studio.
2. Set startup project to `_VoECSExample`.
3. Build:
   - `Build > Build Solution`
4. Run:
   - `Debug > Start Debugging (F5)`

If assets aren’t found, ensure The Forge resource directories are configured correctly for your build output (see the commented-out `fsSetPathForResourceDir(...)` block in `Init()`).

## 4) Debugging tips

### Multithreading
- Toggle “Threading” in the UI to switch between single-thread and multi-thread ECS.
- If you suspect a race, force single-thread:
  - Set `static bool gMultiThread = false;`

### Validate entity counts
- Sprite entities: `gSpriteEntityCount`
- Avoid entities: `gAvoidEntityCount`
- Total capacity: `gMaxSpriteCount`
- Ensure `gDrawSpriteCount <= gMaxSpriteCount` (already asserted in `Draw()`).

### Leak checks
If you hit a memory leak assert on exit, ensure `Exit()` calls:
- `exitGPUConfiguration()` (GPU config parsing allocations)
- and that all GPU resources created in `Init()`/`Load()` are released in `Exit()`/`Unload()`.

## 5) Suggested exercises

1. Add a new component (e.g. `RotationComponent`) and update instance data to include it.
2. Add a new system that changes sprite scale over time.
3. Extend `AvoidanceSystem` to steer instead of flip velocity.
4. Increase `gSpriteEntityCount` and profile CPU vs GPU time (use the built-in profiler UI).

## 6) Key code pointers

- ECS setup: `EntityComponentSystem::Init()`
- Simulation: `MoveSystem`, `AvoidanceSystem`, `EntityComponentSystem::Update()`
- Rendering: `EntityComponentSystem::Draw()`
- Shutdown: `EntityComponentSystem::Exit()`