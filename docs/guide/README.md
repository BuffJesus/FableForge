# FableForge user guide

Short pages, one task each. Start with **[your first level](../FIRST_LEVEL.md)**
if you have not used FableForge before: it covers setup, moving and placing
objects, sculpting and writing a level into the game.

| I want to... | Page |
| --- | --- |
| Make a level, move and place objects, sculpt, write to the game | [Your first level](../FIRST_LEVEL.md) |
| Paint ground textures, walkable areas, environments and sounds | [Paint the ground](paint-the-ground.md) |
| Make a filler map meet the maps around it | [Fit to neighbours](fit-to-neighbours.md) |
| Combine mods, order them and resolve conflicts | [Mod packs and load order](mod-packs.md) |
| Change how a character's mouth moves on a line | [Dialogue lip sync](dialogue-lip-sync.md) |
| Install Aeon Edition with Controller Support | [Aeon/controller walkthrough](../walkthrough/aeon-controller/index.html) |

Before writing anything, read the [engine rules](../ENGINE_RULES.md). They cover
what the game itself refuses, for example new creatures not appearing in an
existing save.

## Safety in one paragraph

Close Fable before writing; FableForge refuses while the game runs. Every game
file FableForge changes is backed up once, and **Setup > Restore the retail
files** puts them back. Mod deploys keep their own originals; **Mods > Undeploy**
restores them. When *Writes go into* (bottom of the Edit panel) names a mod pack,
your edits go into that pack instead, and nothing in the game changes until
**Mods > Build and deploy**.

The screenshots are taken by `tests/ui/guide_shots.txt` on a retail install and
write only to a scratch folder:
`build\FableForge.exe --auto tests\ui\guide_shots.txt --size 1600x900`
(clear `build/guide_install` first; the Mods capture needs the local mod corpus).
