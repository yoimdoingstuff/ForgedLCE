# Data mods

A data mod is a folder with a `forgedlce.json` in it. It adds blocks, items, recipes and smelting without any code, so making and installing one needs no compiler.

Put the folder in the mods folder:

| Where ForgedLCE runs | Mods folder |
|---|---|
| Windows64 (built from source) | `mods\` next to the game executable |
| Xbox 360 plugin | `ForgedLCE\mods\` on the hard drive or USB |
| PS Vita plugin | `ux0:data/ForgedLCE/mods/` |

(Consoles running a source build have no mods folder; use built-in code mods there.)

## Example

```json
{
  "modId": "rubymod",
  "name": "Ruby Mod",
  "version": "1.0",

  "items": [
    { "name": "ruby", "displayName": "Ruby", "texture": "emerald" }
  ],

  "blocks": [
    { "name": "ruby_block", "displayName": "Block of Ruby",
      "material": "metal", "sound": "metal", "hardness": 5, "resistance": 10,
      "light": 0, "texture": "blockEmerald" },
    { "name": "ruby_ore", "displayName": "Ruby Ore", "hardness": 3,
      "drops": "ruby", "dropCount": 1, "texture": "oreEmerald" }
  ],

  "recipes": [
    { "type": "shaped", "result": "ruby_block",
      "pattern": ["RRR", "RRR", "RRR"], "key": { "R": "ruby" } },
    { "type": "shapeless", "result": "ruby", "count": 9, "ingredients": ["ruby_block"] }
  ],

  "smelting": [
    { "input": "ruby_ore", "result": "ruby", "xp": 1.0 }
  ]
}
```

## Referring to items and blocks

Wherever a recipe, drop or smelting entry needs an item or block:

- a **number** is a vanilla id: `264` is diamond, `280` stick, `388` emerald, `1` stone
- a **name without a colon** is one of this mod's: `"ruby"` means `rubymod:ruby`
- a **name with a colon** is from any mod: `"othermod:thing"`

In a shaped recipe's `key`, an entry can also be `{ "item": "ruby", "data": 2 }` to match only one data value.

## Reference

**Mod**

| Key | Required | Meaning |
|---|---|---|
| `modId` | yes | Lowercase `a-z`, `0-9`, `_`. Must be unique. |
| `name`, `version`, `description`, `authors` | no | Shown in the log |
| `items`, `blocks`, `recipes`, `smelting` | no | Lists of the entries below |

**Items**

| Key | Default | Meaning |
|---|---|---|
| `name` | required | Registry name (becomes `modid:name`) |
| `displayName` | the name | Shown in game (any language; save the file as UTF-8) |
| `texture` | `stick` | A vanilla item icon name (custom textures aren't supported yet) |
| `maxStackSize` | 64 | |
| `maxDamage` | 0 | Durability; 0 = not damageable |
| `handEquipped` | false | Held like a tool |

**Blocks**

| Key | Default | Meaning |
|---|---|---|
| `name` | required | Registry name |
| `displayName` | the name | |
| `material` | `stone` | `stone wood dirt grass sand metal glass cloth plant leaves ice clay` |
| `sound` | `stone` | `stone wood gravel grass metal glass cloth sand snow` |
| `hardness` | 1.5 | Stone is 1.5, obsidian 50 |
| `resistance` | 10 | Explosion resistance |
| `light` | 0 | Light level it gives off, 0-15 |
| `texture` | `stone` | A vanilla block icon name |
| `drops` | `"self"` | What breaking it drops (not supported by the console plugin yet) |
| `dropCount` | 1 | |

**Recipes**

| Key | Meaning |
|---|---|
| `type` | `"shaped"` or `"shapeless"` |
| `result`, `count`, `data` | What it makes (count default 1) |
| `pattern` | Shaped: 1-3 strings of equal length (1-3 characters), spaces are empty slots |
| `key` | Shaped: `{ "X": item, ... }` for each symbol in the pattern |
| `ingredients` | Shapeless: a list of 1-9 items |

**Smelting**: `input`, `result`, `count` (default 1), `data`, `xp`.

## Mistakes

Problems go to `forgedlce.log` with the file and line, and the rest of the mod still loads:

```
[ERROR] [ForgedLCE] brokenmod\forgedlce.json: line 3: expected ',' or '}' (is a comma missing?)
[WARN]  [typomod] typomod\forgedlce.json line 1: unknown key "hardnes" in a block (ignored)
[ERROR] [rubymod] rubymod\forgedlce.json line 13: ingredient: nothing is registered as "rubymod:nope"
```

A `forgedlce.json` that has a `"source"` key is a built-in **code** mod manifest for `scripts/apply.ps1`, and is skipped as a data mod.
