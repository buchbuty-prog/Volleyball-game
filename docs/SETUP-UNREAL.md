# Setting up Unreal Engine for Volleyball Career 26

The game project is in `game/`. It already contains the volleyball rules, so the
editor gives you Blueprint nodes for lineups, rotation, the libero, Fit % and
scoring. These steps take you from nothing installed to the project open in
the editor, then to the game running on an Android phone.

Plan for about 150 GB of free disk space and a few hours of downloads.

## 1. Install the tools (Windows)

1. **Epic Games Launcher**: download it from <https://www.unrealengine.com/download>, sign in or create a free Epic account.
2. **Unreal Engine**: in the launcher open *Unreal Engine → Library*, click **+** next to *Engine Versions*, pick **5.8** (the version this project is set to) and click *Install*.
   Before it starts, open *Options* and tick **Android** under *Target Platforms*.
3. **Visual Studio 2022 Community** (free): <https://visualstudio.microsoft.com/>. In the installer, tick the **Game development with C++** workload, and in its details tick the *Unreal Engine installer* and the newest *Windows SDK*.

On a Mac, use **Xcode** from the App Store instead of Visual Studio. Everything else is the same.

## 2. Open the project

1. Clone the repository (or download it as a ZIP from GitHub and unzip it):
   ```
   git clone -b claude/volleyball-career-26-design-hj0lvy https://github.com/buchbuty-prog/Volleyball-game.git
   ```
2. In `Volleyball-game/game/`, right-click **VolleyballCareer26.uproject** and choose **Generate Visual Studio project files**. If it asks which engine version to use, pick the one you installed.
3. Double-click **VolleyballCareer26.uproject**. When it says modules are missing or out of date and asks to rebuild them, click **Yes**. The first build takes several minutes.

If the rebuild fails, open `VolleyballCareer26.sln` in Visual Studio, build the *Development Editor* configuration, and copy the first error from the *Error List* to Claude.

## 3. Check that the rules are there

In the editor, create any Blueprint actor, add the **VC26 Team** and **VC26 Match** components, and look under the *VC26* category in the node menu:
*Validate Lineup*, *Start Set*, *Rally End*, *Get Role Fit*, *Award Point*, and the *On Side Out*, *On Libero Swap*, *On Match Point* events.

## 4. Play

Run `game/Play.bat`, or press **Play** in the editor. The arena is built at runtime on the engine's empty map, so there are no levels to open. Run `Play.bat -VC26Autoplay` to watch the CPU play both sides.

## 5. Android (when you're ready to run on a phone)

1. Install **Android Studio** from <https://developer.android.com/studio>. Use the version Epic lists for your engine on the page below. Open it once, then close it.
2. Run the engine's setup script. For a launcher install it's
   `C:\Program Files\Epic Games\UE_5.x\Engine\Extras\Android\SetupAndroid.bat`. It downloads the SDK, NDK and build tools versions that engine needs.
3. Restart the editor. In *Project Settings → Platforms → Android*, click **Configure Now** and accept the SDK license.
4. Turn on *Developer options* and *USB debugging* on your phone, connect it by USB, and accept the prompt on the phone.
5. Run `game/BuildAndroid.bat`. It builds the game and installs it on the phone. The first run takes a long time.

If the build says Android isn't an available platform, open *Unreal Engine → Library* in the launcher, click the arrow next to 5.8, choose *Options*, tick **Android** and click *Apply*.

On the phone, the left half of the screen is a stick that appears under your thumb, and the right half is the action button. Tap the difficulty label in the top left to change it. To try this layout on PC, run `Play.bat -VC26Touch`, where the mouse acts as one finger.

Exact SDK, NDK and Android Studio versions change with each engine release. Epic keeps them on [Android Development Requirements](https://dev.epicgames.com/documentation/en-us/unreal-engine/android-development-requirements-for-unreal-engine).

## How the code is laid out

| Path | What it is |
|---|---|
| `core/` | The volleyball rules in plain C++. Shared with the unit tests and the future ranked server |
| `game/Plugins/VC26Core/` | Compiles `core/` into Unreal and adds the Blueprint nodes |
| `game/Source/VolleyballCareer26/` | The game module: draws `RallySim`, reads input and shows the HUD. Rules and gameplay belong in `core/` |
| `VC26CourtActor` | An earlier court demo with rotation labels and a libero display. It isn't spawned, but you can place it in a level to try it |
| `game/Config/` | Mobile renderer, Android package and landscape settings |
