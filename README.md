# FableTextures 0.3.0-dev1 — source review

This branch contains the buildable source snapshot supplied for Nexus Mods security review of `FableTextures-0.3.0-dev1-win64.zip`, from [Fable Texture Kit, mod 608](https://www.nexusmods.com/fablethelostchapters/mods/608).

Start with [BUILD-INSTRUCTIONS.txt](BUILD-INSTRUCTIONS.txt). It provides prerequisites and complete commands for the 64-bit Windows application and the two 32-bit game-loader DLLs. All dependencies needed by these targets are included in this tree; a game installation is not required to compile them.

- Application: [tools/texture-export](tools/texture-export), with shared parser/writer code in [libs/forgecore](libs/forgecore).
- Runtime plugins and header generators: [tools/texture-runtime](tools/texture-runtime).
- Build results from a fresh extraction: [BUILD-VALIDATION.json](BUILD-VALIDATION.json).
- Release hashes, source hashes, and snapshot provenance: [provenance/RELEASE_AND_SOURCE.json](provenance/RELEASE_AND_SOURCE.json).
- Licensing: [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

The application exports textures from a user-selected Fable TLC installation and imports edited images using backups and transactional file updates. TextureSizeFix preserves HUD/menu display size for enlarged textures. Archive4GB corrects signed archive offset and stream handling for supported banks between 2 and 4 GiB. The plugins validate the supported host and patch sites, then modify their own game process. The application does not patch Fable.exe on disk.

## Provenance limits

All 360 files from the previously supplied source-review ZIP are preserved byte for byte. That ZIP's SHA-256 is `161974a23bb04408419960e9ada36d3c7ce4235f7a8bd293b7d482fee85236db`. This README and the publication verification record were added for the GitHub review branch.

All seven application source hashes recorded in the frozen release match this snapshot. Shared support-library sources reflect the working-tree snapshot used to prepare the review bundle, so the rebuilt application is **not byte-identical** to the frozen release. The executable code sections of both rebuilt runtime DLLs match their shipped versions. The original root CMake file is retained in `provenance/Original-CMakeLists.txt`; the review root builds the shared libraries and standalone application without unrelated editor targets.

This is a standalone source snapshot on a dedicated review branch of FableForge. It does not replace FableForge's main branch. It includes source and licenses, without compiled binaries, retail game assets, or saves.

To obtain this branch with Git:

```powershell
git clone --single-branch --branch review/fabletextures-0.3.0-dev1 https://github.com/BuffJesus/FableForge.git FableTextures-source-review
cd FableTextures-source-review
```

Then follow `BUILD-INSTRUCTIONS.txt`.
