Fable Textures 0.3.0 development build

Windows 10/11, 64-bit. Unzip the whole folder and run FableTextures.exe.
Keep the runtime folder next to the exe.
No Python, .NET, EgoCore or additional runtime installation is required.
No game files are included.

EXPORT TAB
1. Check the detected Fable: The Lost Chapters folder, or choose Browse.
2. Choose an export location.
3. Choose Fast, Balanced (recommended), or Smallest files. All are lossless.
4. Select Export textures. Cancel safely at any time.

Each export creates a new "Fable textures" folder. Existing folders are never
replaced; subsequent exports use numbered folders. Your source files are read
only. The completed folder appears only after the export finishes.

OUTPUT
images/ contains editable PNGs organized by source, subject and texture role.
Character groups include Apprentices and Bandits. Developer-path hints group
world scenery, foliage, furniture, effects and editor interface textures.
originals/ preserves native payloads, headers and stored mip levels.
Front end/ contains a separate project from frontend.big.
texture-project.json identifies the ordinary main/GUI texture replacements.
Its texture fields also describe entry types, native pixel formats, allocation,
frame dimensions/counts, mip levels, transparency and flags for preserved and
skipped entries. Raw headers remain authoritative; these fields aid inspection.
collection-index.json records other source paths, IDs, hashes, frame layouts,
glyph-page mappings and the appropriate import policy.
export-report.json lists counts, coverage, conversion issues and timing.
SIZE-PLAN.txt compares baseline, 2x and 4x native mip-data costs by subject/role.
texture-size-plan.json includes per-texture dimensions and mip estimates.
Each archive has its own plan; Front end/ contains the frontend.big plan.
Plans retain DXT compression but exclude bank LZO, metadata and alignment.
Review the edited images in the Import tab for the exact rebuilt archive size.

COVERAGE
Main and GUI textures, front-end textures, loose images under data/, static font
atlases, PC streaming glyph pages, and recognized DXT1 terrain patch textures.
Animated textures are exported as ordered frame strips; supported volumes as ordered slice strips.
Signed bump channels use the reversible mapping documented in their manifest.
PNG images show the highest-resolution mip; native originals retain all mips.

MODDING
Keep names, folder structure and manifests when editing ordinary textures for
Forge review and sparse mod-pack creation. Original bank IDs remain authoritative.
DDS replacements can sit beside the corresponding PNG using the same filename
stem. Keep the PNG unchanged or remove it when using the DDS replacement.

Current Forge routes Front end/ projects to frontend.big automatically.
Sprite atlases retain their complete cell grid. Animation strips stack full
stored frames vertically, including padding; keep their order and frame count.
Resize these images proportionally. Animation strips use PNG rather than DDS.
Export a fresh project to use these workflows with older collection exports.
Volume strips retain depth and one raw mip; resize XY proportionally, preserving
slice order/count. Signed vector maps use R=U+128, G=V+128, B=128, alpha=255.
The development Forge importer validates this encoding and retains source mip
counts where dimensions permit. DXT5 sky slots preserve their format/alpha class.
Font, terrain and unsupported volume/animation layouts need specialized writers.
The Import tab brings edited main and front-end textures back into the game.

Some game records declare frames that are not stored in that entry. Their stored
surface is exported and the discrepancy is recorded; missing frames are not
invented. Non-PC font data is preserved raw, without implying PC runtime usage.

COMMAND LINE (optional)
FableTextures.exe --export --install "C:\Path\To\Fable" --output "D:\New export" --compression balanced
Compression choices: fast, balanced, smallest. Output must be a new folder.
The graphical interface appears when --export is omitted.

IMPORT TAB
Puts textures you edited in a "Fable textures" export (made on the
Export tab) back into your game: the main textures (textures.big) and the menus
(frontend.big). Bigger images are fine. Menus and the HUD keep their normal
on-screen size through TextureSizeFix, a small mod DLL the importer installs.

1. Export on the Export tab, then edit PNGs in place. Keep the file
   names and folders. Delete any images you don't care about; a missing image
   keeps the game's original texture.
2. Open the Import tab. Check the game folder and choose your
   "Fable textures" folder (filled in for you right after an export).
3. Review changes: shows how many textures you edited. Nothing is written yet.
4. Import into game (with Fable closed).
5. Start Fable through your usual mod launcher (FSE / EgoCore).

WHAT CHANGES IN YOUR GAME FOLDER
data\graphics\pc\textures.big and frontend.big are rebuilt with your images.
The first time, the originals are kept as textures.big.forge-orig and
frontend.big.forge-orig. To undo everything, delete the rebuilt file and
rename the .forge-orig copy back.
Mods\TextureSizeFix\TextureSizeFix.dll and TextureSizes.dat are installed and
enabled in Mods.ini (Mods.ini is backed up once as Mods.ini.forge-orig).
If textures.big reaches 2 GiB, the importer also installs and enables
Mods\Archive4GB\Archive4GB.dll in the same transaction. The audited Steam TLC
executable and an existing Mods.ini are required for that larger-bank import.
Import as often as you like: every import rebuilds from the kept originals
plus all edited PNGs in your folder, and refreshes TextureSizes.dat so menus
and the HUD stay correct.
To restore textures through the app, put the original exported PNGs back in your
export folder, then review and import again. Review lists the archives to restore.
If an image or installed archive changes after review, review again. A failed
image review preserves your previous import. If Windows blocks installation,
the importer restores the previous files or reports where recovery files remain.

LIMITS
- Textures larger than 512 pixels per side need the EgoCore 2K texture DLL
  (Mods\Fable2KTextureLimit, enabled in Mods.ini). Up to 2048 is supported.
  The importer warns when you need it and it isn't set up.
- textures.big may exceed 2 GiB with the included Archive4GB DLL, but its complete
  rebuilt file must stay below 4 GiB. Other archives retain the 2 GiB limit here.
  SIZE-PLAN.txt in your export shows what 2x/4x costs per group.
- Archive4GB corrects archive seeking, stream limits and packed offsets. It does
  not increase the game's RAM allowance or texture dimension limit. Large-address-
  aware (LAA) executable changes are separate; this program does not patch Fable.exe.
- Fonts, terrain patches and loose images (images\Fonts, Terrain, Loose) are
  not imported by this test build.
- TextureSizeFix only works with the Steam Fable: The Lost Chapters Fable.exe.
  On any other build it refuses to load and changes nothing (see
  Mods\TextureSizeFix\TextureSizeFix.log).

COMMAND LINE (optional)
FableTextures.exe --review --install "C:\Path\To\Fable" --textures "D:\Fable textures"
FableTextures.exe --import --install "C:\Path\To\Fable" --textures "D:\Fable textures"
