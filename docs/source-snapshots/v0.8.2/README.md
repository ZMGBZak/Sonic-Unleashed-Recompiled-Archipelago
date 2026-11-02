# Sonic Unleashed Recompiled × Archipelago — V0.8.2 C++ source snapshot

Snapshot date: 2026-11-02. Branch: `archipelago-prototype`.

These are **complete copies of four developer-supplied source files**, saved separately so that the existing C++ build tree is not overwritten with an incomplete update:

- `UnleashedRecomp/archipelago_client.cpp`
- `UnleashedRecomp/archipelago_client.h`
- `UnleashedRecomp/gpu/video.cpp`
- `UnleashedRecomp/gpu/ap_multiplayer_overlay.h`

The video source displays game-client/APWorld version **0.8.2** and integrates the multiplayer item feed, progression-item chime and corrected medal counter. The APWorld's source is separately tracked under `archipelago/sonic_unleashed/`.

## Important: not yet a complete buildable source-tree sync

The working Windows build also depends on locally modified or generated support files that are **not** in this snapshot and are missing or older in this GitHub branch. Known missing dependencies include:

- `UnleashedRecomp/gpu/ap_roulette_embedded_assets.h` (generated artwork/SFX; rights and provenance need review before distribution)
- `UnleashedRecomp/patches/ap_ring_roulette.h`
- `UnleashedRecomp/os/ap_update_helper.h`

Other local source differences may remain, including patch implementations and resource files. **Do not copy these snapshots wholesale into the active C++ paths** and assume the project builds. First reconcile the developer's latest local source tree against this branch, check all dependencies and licenses, build on Windows, and test with a new multiplayer room.

The existing active C++ files were deliberately left untouched to avoid breaking the repository. This commit is **not** an update to the game binary, a tagged release, or an update manifest change.

The repository's GNU GPLv3 applies. Distributors of compiled binaries must comply with the obligations for the corresponding complete source code; this snapshot alone is **not sufficient** as the complete corresponding source for the newly compiled executable.
