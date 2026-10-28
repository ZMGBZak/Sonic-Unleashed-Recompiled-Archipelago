# Release checklist

This checklist applies to every GitHub Release, not just files committed to the repository.

## Source repository

- [ ] Include only original code, documentation and resources with confirmed redistribution rights.
- [ ] Follow upstream source-code licenses and attribution requirements.
- [ ] Exclude all backups (`.bak`, `.old`, `backup/`, patch backups), debugging logs and private local data.
- [ ] Exclude all Sonic Unleashed game dumps and extracted proprietary game assets.
- [ ] Inspect `git status --short` and `git diff --cached --name-only` before pushing.
- [ ] Check for passwords, credentials, private paths, and personal information.
- [ ] Remember: `.gitignore` does not remove files already committed or in Git history.

## Client Release ZIP

- [ ] Build from a reviewed, reproducible source revision.
- [ ] Determine whether each executable and bundled library is legally redistributable; when in doubt, publish a patch or installer rather than a full game binary.
- [ ] Use an explicit allowlist of approved release files. Never zip the whole game folder.
- [ ] Do not include backup files, save files, the updater's temporary files, logs or developer tooling.
- [ ] Do not include proprietary game models, audio, textures, video, archives or extracted game data.
- [ ] Generate SHA-256 hashes and test installation with a clean separate copy.
- [ ] Verify the updater never deletes user saves or mod configuration and can recover on failure.

## APWorld Release

- [ ] Review the `.apworld` as a ZIP archive for accidentally bundled files or game assets.
- [ ] Version the APWorld independently from the client.
- [ ] Warn players when APWorld changes require regeneration of a multiworld.
- [ ] Publish a distinct APWorld asset and direct users to its GitHub Release.

## Update metadata

- Use GitHub Releases as the authoritative version source.
- Do not publish or auto-install development archives or random assets.
- Download only from the intended GitHub repository over HTTPS.
- Require exact filenames, sizes, and SHA-256 checksums in a signed or otherwise authenticated manifest before installation.
- Perform updates outside the running process, with atomic replacement and rollback.
