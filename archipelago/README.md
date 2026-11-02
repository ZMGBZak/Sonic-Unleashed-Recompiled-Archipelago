# Sonic Unleashed Recompiled — Archipelago V0.8.2 (group-test candidate)

This directory tracks the **Python source** of the experimental V0.8.2 Archipelago world.

The package is `sonic_unleashed/` (six Python files). To build a local `.apworld` archive, ZIP the `sonic_unleashed` folder **with that folder at the ZIP root**, then rename `.zip` to `.apworld`. Install it through your Archipelago custom-worlds mechanism and create a **new** room.

Features in this source snapshot include the three Goals, DLC and CD/Art Book toggles, logic/progression rules, removal of Burn Trap from newly generated rooms, and the medal-goal target of 338 unique locations. Some aspects of the logic and in-game completion still require real multiplayer tests.

**Compatibility:** Pair this APWorld with the matching V0.8.2 game executable. The live C++ source tree in this GitHub branch is not yet identical to the developer's locally compiled executable. See `docs/source-snapshots/v0.8.2/README.md`.

This is a test candidate, **not** a verified stable release. Distribution must honor the repository's GPLv3 license and avoid bundling proprietary game assets.
