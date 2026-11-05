# Volleyball Career 26

Mobile 3rd-person volleyball sim for Android. The full design is in [docs/GDD.md](docs/GDD.md).

## Repository layout

| Path | Contents |
|---|---|
| `docs/GDD.md` | Technical & game design document |
| `core/` | Engine-agnostic C++17 gameplay: Rotation Manager, libero automation, overlap checks, Role-Fit for 5-1 / 6-2 / 4-2 (GDD 1.7), set and match scoring for indoor and beach, ball physics and the playable 6v6 rally (`RallySim`) |
| `tests/` | Unit tests for `core/` (no third-party dependencies), run by CI on every push |
| `game/` | Unreal Engine 5 project. The `VC26Core` plugin brings `core/` in with Blueprint nodes. Setup: [docs/SETUP-UNREAL.md](docs/SETUP-UNREAL.md) |
| `docs/demo/rotation-lab.html` | Interactive browser demo of the free-position rules. Open the file in any browser |

`core/` has no engine dependencies and is deterministic, so the same code can be compiled into a UE5 module, a Unity native plugin, or the authoritative ranked server.

## Play the prototype

Build the project once ([docs/SETUP-UNREAL.md](docs/SETUP-UNREAL.md)), then run `game/Play.bat`. Everyone is a placeholder shape for now.

| Input | Action |
|---|---|
| W A S D / arrows / left stick | Move to the ring when passing. Aim the serve, set and attack. Slide along the net when blocking |
| Space / gamepad A | Hold and release to serve. Press on the timing mark to play the ball or jump to block |
| 1 / 2 / 3 | Easy, Normal or Hard CPU |

You control whichever Home player the ball is going to, or your nearest front-row blocker when the opponent attacks. Sets and attacks position the player for you.

The main menu has **New Career**, **Continue Career** and **Quick Match**. In a career you pick an archetype, play an eight-team league and a final, spend skill points, and climb from the University Circuit to the World Stage. Press Esc to leave a match without it counting. Careers save to `game/Saved/VC26Career.sav`.

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
