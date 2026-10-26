# VOLLEYBALL CAREER 26
### Technical & Game Design Document — Mobile 3rd-Person Volleyball Sim
**Platform:** Android (Unity 6 LTS / Unreal Engine 5.4+) | **Target:** Mid-range to flagship devices, 60 FPS

---

## 0. EXECUTIVE SUMMARY & ENGINE RECOMMENDATION

| Criteria | Unity 6 LTS | Unreal Engine 5.4+ |
|---|---|---|
| Android APK/AAB size efficiency | Better (smaller baseline) | Larger baseline, needs aggressive stripping |
| Mobile rendering pipeline | URP (Universal Render Pipeline) — mature mobile path | Mobile Forward Renderer — improving but heavier |
| Animation tooling for motion-captured sports moves | Timeline + Animation Rigging package | Control Rig + Motion Matching (native, excellent for this genre) |
| Team familiarity / hiring pool for mobile sports titles | Large | Smaller for mobile specifically |
| **Recommendation** | Use for a leaner F2P live-service launch | Use if Motion Matching–driven locomotion (EA Sports FC-style fluidity) is the top priority and mid-range performance can be tuned via Nanite/Lumen scalability groups |

**Recommended stack:** Unreal Engine 5.4 with **Motion Matching** for footwork/rotation blending (this is what makes EA Sports FC's movement feel "smart"), Mobile Forward Renderer, no Lumen/Nanite (baked lighting + LODs instead for mid-range Android), and a custom **Rotation State Machine** (detailed in Section 1.7) driving the free-position feature.

> The rules logic (Section 1.7) lives in an engine-agnostic C++17 core (`core/`) so it can be compiled into a UE5 module, a Unity native plugin, or the headless ranked server unchanged.

---

## 1. CORE GAMEPLAY MECHANICS (3RD-PERSON)

### 1.1 Camera System
- Default: over-the-shoulder, player-locked, 65° FOV, dynamic FOV pull (up to 75°) during fast rallies.
- Spike cam: auto zoom-in + slow-mo (0.4x for 300ms) on jump apex during power spikes.
- Dig/block cam: low-angle cinematic cut triggered on rally-point saves (match-point situations, streak digs).
- Free-look: right-stick/swipe toggle for pre-serve positioning reads.

### 1.2 Serving
- Dual-gauge input: **Gauge A (Power)** — hold-and-release timing bar; **Gauge B (Placement)** — directional stick/drag defining target zone (6 court zones + seam targeting).
- Serve types: float, jump-float, topspin jump serve, short/deep variation — each with distinct gauge sensitivity curves and risk/reward (ace probability vs. error probability).

### 1.3 Passing & Digging
- Contextual "shadow" system: a ground-projected ball-shadow reticle; player must be inside the shadow radius at contact.
- Timing window: Perfect / Good / Late-Shank, modified by player attributes (Reaction, Positioning) and stamina.
- Dive roll, pancake, and chest-pass animations auto-selected by ball trajectory + player speed vector.

### 1.4 Setting
- 3rd-person target reticle with a live trajectory arc preview (Bezier curve rendered client-side, recalculated every frame from current arm/wrist bone transform).
- Play-call wheel (radial menu, swipe-up gesture): Quick 1, Pipe, High 4, Slide, Dump — each mapped to a pre-baked footwork/approach animation blend-tree.

### 1.5 Attacking
- Jump-timing gauge + aim vector (right stick or swipe-drag at apex).
- Shot types: power spike, line, cross, wipe (off-block), tip, roll shot — selectable via directional input at the timing-gauge peak.

### 1.6 Blocking
- Multi-player commit system: Solo / Double / Triple, called via quick-tap prompts on teammate icons or auto-AI assist (configurable).
- Timing determines outcome band: roof block / touch (recoverable) / penetration fault (net-touch risk).

### 1.7 Free Positioning & Perfect Rotation
This is the system that lets a player select **any court position** (not just their trained specialty) while the game still enforces legal FIVB rotation rules automatically.

**Design goals:**
1. Player picks *any* of the 6 rotational slots (P1–P6) pre-match, regardless of listed role.
2. The engine auto-manages legal rotation order for the other 5 AI/CPU or online teammates.
3. Switching to an "off-role" position (e.g., a Libero playing P2 as a hitter) carries a **realism cost** — reduced attribute effectiveness for actions outside the trained role — but is never blocked.

**Systems required:**
- **Rotation Manager (server-authoritative in online modes, deterministic FSM offline):** tracks P1–P6 slot occupancy per rally, auto-rotates clockwise on side-out per official volleyball rules, and validates overlap/foot-fault legality each serve.
- **Role-Flex Attribute Modifier:** each player has a base role (Setter, OH, MB, Opposite, Libero) with a Role-Fit multiplier (0.6–1.0) applied to context actions when playing a duty other than their native role. Displayed to the player as a transparent HUD "Fit %" badge so it never feels like a hidden penalty.
  - *Clarification:* P1–P6 are rotational slots that every player cycles through during a set, so Role-Fit cannot be keyed on the current slot. Instead, the slot a player **starts** in fixes their **duty** for the set (in a 5-1, counting P1→P6 from the setter: S, OH, MB, OPP, OH, MB), and Role-Fit = `fit(native role, duty)`.
- **Libero Rule Enforcement:** two modes, chosen per match.
  - **Simulation Mode** (strict FIVB): the libero may only replace back-row players, may not serve, is automatically swapped out when their slot rotates to the front row, and re-enters automatically for configured targets (default: both middle blockers). Illegal placements are **disabled** in the lineup screen rather than penalized.
  - **House Rules / Arcade Mode:** the player may put a libero in any slot, including front row. The libero then stays in the rotation like a regular player (serving and front-row play included) and auto-swaps are switched off; Role-Fit carries the realism cost.
- **UI:** pre-match drag-and-drop court diagram (6 slots), color-coded by legality (green = compliant, amber = arcade-only, red = illegal even in arcade mode, e.g. an empty slot or the same player in two slots / 7 players on court).

#### 1.7.1 Reference implementation (`core/`)
| GDD concept | Code |
|---|---|
| Green / amber / red lineup diagram | `validateLineup()` → `LineupReport::slots` (`Legality::Legal / ArcadeOnly / Illegal`) |
| Simulation vs. Arcade toggle | `RuleMode`; `RotationManager::create()` refuses lineups the mode doesn't allow |
| Clockwise rotation on side-out | `RotationManager::onRallyEnd(wonByUs)` (called once per dead ball) |
| Libero automation | Same call; returns a `LiberoSwap` event for animation/VO hooks. At most one libero replacement per dead ball, satisfying the "completed rally between replacements" rule |
| Overlap/positional fault at serve | `checkOverlap()` — FIVB 7.4.3 adjacent-pair rules, serving P1 exempt |
| Role-Fit badge | `roleFit()`, `roleFitPercent()`, `RotationManager::roleFitOf()` |
| 5-1 / 6-2 / 4-2 duties | `systemDuty()`, `LineupSpec::system` |
| Set/match scoring, match-point cam and VO triggers | `MatchScore` (`MatchRules::indoor()` / `beach()`), `PointResult` events |
| Playable rules demo | `docs/demo/rotation-lab.html` |

---

## 2. CAREER MODE ENGINE ("VOLLEYBALL CAREER 26")

### 2.A Player Career (Be a Pro)
- **Create-A-Player:** face morphing (blendshape rig, ~60 sliders), gear/tattoo/shoe/pad customization, role selection including Libero.
- **Progression:** Skill Trees per archetype (Power Spiker, Defensive Anchor, Precision Setter, Rally Terminator-style block specialist). Skill Points earned via training minigames, Match Rating (1.0–10.0, EA FC–style post-match grading algorithm weighting efficiency %, errors, and clutch-point conversion).
- **Career path:** Amateur/university circuit → national club league → scouted to FIVB Nations League / World Championship national team, with a scouting-report overlay showing scout attendance at matches (tension mechanic, à la Career Mode scouting in modern sports titles).
- **Chemistry/Energy:** fatigue curve per set, injury-risk modeling tied to overtraining, team chemistry meter affecting set-accuracy-to-hitter and dig-coverage AI behavior.

### 2.B Manager Career (Club Dynasty)
- Full roster/contract/transfer-window management with a scouting network (regional scouts, report-confidence ratings).
- Youth academy pipeline generating trainable prospects with growth curves.
- Tactical board: rotation systems (5-1, 4-2, 6-2), defensive schemes (read vs. commit block, perimeter vs. rotational D), substitution strategy presets (tactical libero/server swap triggers by set-score threshold).
- Club infrastructure: finances, facility upgrades, medical staff tier (injury-recovery speed), sponsorship deals.

---

## 3. CONTROLS & UI/UX (ANDROID)

| Scheme | Input Mapping |
|---|---|
| Touch Gestures | Swipe-up = spike, tap = set, drag-and-release = serve direction/power |
| Virtual Gamepad | Dual on-screen sticks + contextual A/B/X/Y-equivalent buttons for Pass/Set/Spike/Block |
| Physical Controller | Full Bluetooth HID mapping — Xbox, DualSense, Razer Kishi, generic HID fallback profile |

**HUD:** minimalist — stamina bar, timing-gauge ring, play-call radial overlay, Role-Fit badge (from Section 1.7), rotation-order mini-diagram in the corner during serve-receive.

---

## 4. VISUALS, SOUND & REALISM

- **Audio:** layered crowd chant system (intensity scales with rally length/score tension), surface-accurate footstep/contact audio (hardwood vs. sand), multi-language commentary via a modular VO barks system keyed to match events (ace, block, rally-save, match point).
- **Animation:** motion-captured base library; Motion Matching (UE5) or Animation Rigging + custom blend trees (Unity) for momentum-based footwork, setter hand-set variation, slide-attack approach, net-collision physics.
- **Mobile graphics target:** 60 FPS on mid-range Android (e.g., Snapdragon 7-series class). Baked lighting, cascaded shadow maps scaled by device tier, dynamic lighting reserved for high-tier device profile, minimal post-processing (bloom/AA only) on low-tier.

---

## 5. GAME MODES & LIVE SERVICES

- **Modes:** Be a Pro Player Career, Manager Club Dynasty, Online 1v1/6v6 Co-op Ranked Seasons, Beach Volleyball 2v2 Tournaments, Quick Match, Skill Challenges, **Free Position Exhibition** (showcase for Section 1.7).
- **Live Services:** weekly real-world-inspired challenge scenarios, seasonal battle pass (kits, courts, celebration animations), roster/attribute updates synced to real competition calendars.

---

## 6. BACKEND & LIVE-OPS ARCHITECTURE (Technical Addendum)

- **Netcode:** authoritative server for ranked online modes (rollback/prediction for latency-sensitive spike timing); Unreal's Iris replication or Unity Netcode for GameObjects, depending on final engine choice.
- **Rotation legality validation:** server-side re-check of the Section 1.7 Rotation Manager state on every serve to prevent client-side manipulation in ranked play. The core is deterministic (no RNG, no engine state), so the server runs the same code as the client. Ranked play is Simulation Mode only.
- **Live-ops backend:** headless service for battle pass progression, roster/attribute patches, and challenge rotation — decoupled from the client build so weekly content doesn't require app-store resubmission for data-only updates. The Role-Fit table is a data-only patch candidate.
- **Telemetry:** match-rating algorithm inputs, Role-Fit usage analytics (to balance the free-position penalty curve post-launch), and device-tier performance telemetry for scalability tuning.

---

## 7. OPEN DESIGN QUESTIONS FOR NEXT PASS
1. Should Free Position mode support **saved custom lineups** per opponent, or reset each match?
2. Monetization boundary: is the battle pass cosmetic-only, or does it touch Role-Fit training speed (pay-to-progress risk)?
3. Cross-play scope: Android-only ranked pools, or shared MMR with a future iOS build?
4. Rule-set selector: FIVB forbids the libero from serving, but NCAA and some national leagues allow it (and allow two liberos per match). Should Simulation Mode offer a rule-set choice (FIVB / NCAA / domestic league), with each Career Mode league defaulting to its real-world rules?
5. Role-Fit growth: can a player's fit for an off-role duty improve with training/match time in Career Mode (role retraining), and if so, does it decay when unused?
6. Beach 2v2 has no rotations or libero: does Free Position have any beach equivalent (e.g., left/right side preference with a Fit penalty), or is it indoor-only?
