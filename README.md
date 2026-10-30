# Volleyball Career 26

Mobile 3rd-person volleyball sim for Android. The full design is in [docs/GDD.md](docs/GDD.md).

## Repository layout

| Path | Contents |
|---|---|
| `docs/GDD.md` | Technical & game design document |
| `core/` | Engine-agnostic C++17 gameplay rules: Rotation Manager, libero automation, overlap checks, Role-Fit for 5-1 / 6-2 / 4-2 (GDD 1.7), set and match scoring for indoor and beach |
| `tests/` | Unit tests for `core/` (no third-party dependencies), run by CI on every push |
| `game/` | Unreal Engine 5 project. The `VC26Core` plugin brings `core/` in with Blueprint nodes. Setup: [docs/SETUP-UNREAL.md](docs/SETUP-UNREAL.md) |
| `docs/demo/rotation-lab.html` | Interactive browser demo of the free-position rules. Open the file in any browser |

`core/` has no engine dependencies and is deterministic, so the same code can be compiled into a UE5 module, a Unity native plugin, or the authoritative ranked server.

## Build & test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Usage sketch

```cpp
#include "vc26/RotationManager.h"
using namespace vc26;

LineupSpec spec;
spec.starters = { /* P1..P6 players from the drag-and-drop screen */ };
spec.libero = Player{7, Role::Libero};
spec.liberoStartSlot = Slot::P6;
spec.liberoTargets = {3, 6};            // follow both middle blockers

LineupReport report;                    // drives the green/amber/red diagram
auto rm = RotationManager::create(spec, RuleMode::Simulation, /*servingFirst=*/true, &report);

RallyResult r = rm->onRallyEnd(/*wonByUs=*/false);  // once per dead ball
if (r.liberoSwap) { /* trigger substitution animation / VO */ }
float fit = rm->roleFitOf(playerId).value_or(1.0f); // HUD "Fit %" badge
```
