# Making an address map for a game version

The console plugins need to know where certain functions, variables and struct fields are in **one specific build** of the game. This page explains how to find them, once per game version. The result is a text file that everyone with that version can use.

You need:
- a disassembler/decompiler that can load the game's executable (for example Ghidra with an Xbox 360 XEX loader, or with a PS Vita ELF/SELF loader, using an executable dumped from your own copy)
- the LCE source, as a map of what each function looks like. Function names in this page refer to it.

Start from [inject/addresses/TEMPLATE.txt](../inject/addresses/TEMPLATE.txt). It lists every key with a short description; copy it to e.g. `xbox360-TU19.txt` and uncomment lines as you fill them in.

## Value format

- Xbox 360: absolute addresses, e.g. `fn.Tile_ctor = 0x8215A2C8`. The game image has a fixed base.
- PS Vita: `segment:offset` relative to the game module, e.g. `fn.Tile_ctor = 0:0x0015A2C9` (code is in segment 0, data in segment 1). An **odd** code offset means Thumb code; that's normal on the Vita.
- Sizes and offsets are plain numbers.

## Start here: one call sequence gives you most of it

`MinecraftWorld_RunStaticCtors()` (`Minecraft.World/Minecraft.World.cpp`) calls the static constructors in a fixed order: `Packet`, `MaterialColor`, `Material`, `Tile`, the tool items, `BlockReplacements`, `Biome`, `Item`, `FurnaceRecipes`, `Recipes`, stats, `Skeleton`, `PigZombie`, `TileEntity`, `EntityIO`, **`MobCategory`**, `Item::staticInit`, `LevelChunk`, ... To find it, follow `Minecraft::main()` from the platform's entry point (`*_Minecraft.cpp`). It's the first call.

| Key | How to find it |
|---|---|
| `hook.MobCategory_staticCtor` | The call right after `EntityIO::staticCtor` in that sequence. Its body does `new MobCategory(70, ...)`, `(10, ...)`, `(5, ...)`. |
| `data.LateCheck` | The first static variable `MobCategory::staticCtor` stores to (`MobCategory::monster`). |
| `check.address`, `check.value` | Use the start of `MobCategory::staticCtor`: its address, and the 32-bit instruction word there as the CPU reads it. |
| `data.Material_*` | `Material::staticCtor` creates the materials one after another (air, grass, dirt, wood, stone, metal, heavyMetal, ...; match each `new Material(...)` line in `Material.cpp` to its store). The stores give the variables' addresses. |
| `data.Tile_SOUND_*` | The `Tile::SOUND_*` statics (`Tile.h`) are created early in the `Tile` static setup. Match each `new SoundType(...)` in `Tile.cpp` to its store, by its name and volume/pitch arguments. |
| `data.Tile_tiles` | `Tile::staticCtor` allocates `new Tile *[4096]` and stores it here. |
| `fn.operator_new`, `fn.Tile_ctor`, `size.Tile` | In `Tile::staticCtor`, `Tile::lapisBlock = new Tile(22, Material::stone)`: `operator new` is called with `sizeof(Tile)`, then `Tile::Tile(22, stone, true)`. |
| `fn.Tile_setDestroyTime`, `fn.Tile_setExplodeable`, `fn.Tile_setSoundType`, `fn.Tile_setLightEmission`, `fn.Tile_setDescriptionId` | The calls chained on that new tile in the source line (`->setDestroyTime(3)->setExplodeable(5)->setSoundType(...)`). Glowstone's line has `setLightEmission(1.0f)`. These are virtual: use the **base** `Tile::` versions. |
| `fn.Tile_setTextureName`, `fn.wstring_ctor`, `fn.wstring_dtor` | Each `->setTextureName(L"...")` builds a temporary `std::wstring`: the call before is the wstring constructor (taking the literal), the call after is its destructor. |
| `fn.TileItem_ctor`, `size.TileItem`, `fn.Tile_init`, `data.Item_items` | The loop at the end of `Tile::staticCtor`: `if (Item::items[i] == NULL) { new TileItem(i - 256); tiles[i]->init(); }`. |
| `fn.Item_ctor`, `size.Item`, `fn.Item_setTextureName` | In `Item::staticCtor`, `Item::ironIngot = (new Item(9))->setTextureName(L"ingotIron")`. The constructor also logs `"CONFLICT @ %d"`, an easy string to search for. |
| `fn.Item_setMaxStackSize`, `fn.Item_setMaxDamage`, `fn.Item_handEquipped`, `fn.Item_setDescriptionId` | Chained calls in `Item::staticCtor` (tools use `setMaxDamage` and `handEquipped`). |
| `fn.FurnaceRecipes_addFurnaceRecipy`, `data.FurnaceRecipes_instance` | The `FurnaceRecipes` constructor's first call: `addFurnaceRecipy(15 /*iron ore*/, new ItemInstance(Item::ironIngot), 0.7f)`. `staticCtor` stores the instance. |
| `fn.ItemInstance_ctor`, `size.ItemInstance` | `ItemInstance(int id, int count, int damage)`: called with three integers all over recipe code. |
| `data.Recipes_instance`, `offset.Recipes_recipies` | `Recipes::staticCtor` stores `new Recipes()`. `Recipes::_init` stores `new RecipyList()` into the member. |
| `fn.ShapedRecipy_ctor`, `size.ShapedRecipy`, `fn.RecipyPtrVector_push_back` | The end of `Recipes::addShapedRecipy` (a varargs function): `recipies->push_back(new ShapedRecipy(width, height, ids, result, group))`. |
| `fn.ShapelessRecipy_ctor`, `size.ShapelessRecipy`, `fn.ItemInstancePtrVector_push_back`, `size.vector` | `Recipes::addShapelessRecipy`: `new vector<ItemInstance *>()` gives `sizeof(vector)`, `ingredients->push_back(...)`, then `new ShapelessRecipy(...)`. |
| `fn.Recipes_buildRecipeIngredientsArray` | The last call in the `Recipes` constructor. |

## Hooks for names and events

| Key | How to find it |
|---|---|
| `hook.GetString` | Xbox 360: `CConsoleMinecraftApp::GetString(int)` in `Xbox/Xbox_App.cpp`, a tiny function that forwards to the XUI string table's `Lookup`. Other platforms: `StringTable::getString(int)` (`StringTable.cpp`). Development builds contain a `__debugbreak()` there. Content-package builds don't. |
| `hook.Minecraft_tick` | `Minecraft::tick(bool, bool)`, called from `Minecraft::run_middle` once per local player. |
| `hook.MinecraftServer_tick` | `MinecraftServer::tick()`, called from `MinecraftServer::run`'s loop. |
| `hook.ServerPlayerGameMode_destroyBlock`, `offset.ServerPlayerGameMode_level`, `offset.ServerPlayerGameMode_player` | `ServerPlayerGameMode::destroyBlock(int, int, int)`. It loads `level` and calls `getTile` / `getData` on it, then uses `player`. Those loads give the two member offsets. |
| `fn.Level_getTile`, `fn.Level_getData` | The first two calls in `destroyBlock`. They're virtual: use the base `Level::` versions. |
| `fn.Level_setTileAndData`, `offset.Level_isClientSide` | `Level::setTileAndData`, and any `level->isClientSide` check (for example at the top of `WorkbenchTile::use`). |
| `hook.Level_addEntity` | `Level::addEntity(shared_ptr<Entity>)`. |
| `offset.Entity_x` | `Entity::setPos(double, double, double)` stores x, y and z one after another. |

## Calling-convention flags

These depend on the compiler the game was built with. Check them in the disassembly; don't guess.

| Key | How to tell |
|---|---|
| `abi.sharedPtrArgByPointer` | Look at a call to `Level::addEntity`. If the caller builds a `shared_ptr` on its stack and passes that address, use `1`. If it passes the two pointer fields in registers, use `0`; the entity-join event then stays off. |
| `abi.calleeDestroysArgs` | `1` if `Level::addEntity` itself releases its `shared_ptr` argument before returning (MSVC style). `0` if the caller does it afterwards (GCC/Clang style). If `1`, also fill in `fn.EntitySharedPtr_dtor`. |
| `abi.wcharSize` | Find a wide string literal such as `L"stone"`. `73 00 74 00` ("s", "t") means `2`; `73 00 00 00` means `4`. Leave it out if it matches the plugin; the Xbox 360 plugin uses 2, the VitaSDK plugin 4. |

## Checking your map

- The plugin refuses a map with a missing required key, an address that resolves to 0, or a version check that doesn't match. It logs why and leaves the game alone.
- Start with only the required keys plus `hook.GetString`, and watch `forgedlce.log`. Then add the event hooks one at a time.
- Please share finished maps (name the file after the edition and title update) so nobody has to repeat the work.
