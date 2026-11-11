// ForgedLCE data mods: a folder in the mods folder with a forgedlce.json
// that declares blocks, items, recipes and smelting. No code, nothing to
// compile, so they can be made and installed on any platform ForgedLCE runs
// on with a mods folder (Windows64, and consoles through the injected plugin).
//
// Format (docs/DATA_MODS.md has the full reference):
// {
//   "modId": "rubymod", "name": "Ruby Mod", "version": "1.0",
//   "items":  [ { "name": "ruby", "displayName": "Ruby", "texture": "emerald" } ],
//   "blocks": [ { "name": "ruby_block", "displayName": "Block of Ruby", "material": "metal",
//                 "sound": "metal", "hardness": 5, "resistance": 10, "light": 0,
//                 "texture": "blockEmerald", "drops": "self" } ],
//   "recipes": [
//     { "type": "shaped", "result": "ruby_block", "pattern": ["RRR","RRR","RRR"], "key": { "R": "ruby" } },
//     { "type": "shapeless", "result": "ruby", "count": 9, "ingredients": ["ruby_block"] } ],
//   "smelting": [ { "input": "ruby_block", "result": 264, "xp": 1.0 } ]
// }
// Item references are vanilla numeric ids (264) or registry names: "ruby"
// means this mod's ruby, "othermod:thing" another mod's.
#include "stdafx.h"
#include "FLCE_Internal.h"
#include "FLCE_Json.h"

#include <string.h>

namespace ForgedLCE {
namespace DataMods {

using Json::Value;

struct DataMod {
    Json::Document doc;
    std::wstring   modId, name, version, file;
};

namespace {

void Error(const DataMod *m, const Value *at, const std::wstring &message)
{
    Log(FLCE_LOG_ERROR, m->modId.c_str(),
        m->file + L" line " + IntToString(at ? at->line : 0) + L": " + message);
}

void Warn(const DataMod *m, const Value *at, const std::wstring &message)
{
    Log(FLCE_LOG_WARN, m->modId.c_str(),
        m->file + L" line " + IntToString(at ? at->line : 0) + L": " + message);
}

// Warns about keys not in `allowed` (NULL-terminated), to catch typos.
void CheckKeys(const DataMod *m, const Value *obj, const wchar_t *const *allowed, const wchar_t *what)
{
    for (size_t i = 0; i < obj->keys.size(); ++i) {
        bool known = false;
        for (const wchar_t *const *a = allowed; *a; ++a) {
            if (obj->keys[i] == *a) { known = true; break; }
        }
        if (!known) Warn(m, obj->items[i], L"unknown key \"" + obj->keys[i] + L"\" in " + what + L" (ignored)");
    }
}

const wchar_t *Str(const Value *obj, const wchar_t *key, const wchar_t *fallback)
{
    const Value *v = obj->get(key);
    return v != NULL && v->isString() ? v->string.c_str() : fallback;
}

double Num(const Value *obj, const wchar_t *key, double fallback)
{
    const Value *v = obj->get(key);
    return v != NULL && v->isNumber() ? v->number : fallback;
}

bool Bool(const Value *obj, const wchar_t *key)
{
    const Value *v = obj->get(key);
    return v != NULL && v->type == Value::BOOL && v->boolean;
}

std::wstring FullName(const DataMod *m, const std::wstring &shortOrFull)
{
    return shortOrFull.find(L':') == std::wstring::npos ? m->modId + L":" + shortOrFull : shortOrFull;
}

// A vanilla numeric id or a registry name. Returns -1 (and logs) if unknown.
int Resolve(const DataMod *m, const Value *ref, const wchar_t *what)
{
    if (ref == NULL) {
        Error(m, ref, std::wstring(what) + L" is missing");
        return -1;
    }
    if (ref->isNumber()) {
        int id = (int)ref->number;
        if (id > 0 && Backend::IdExists(id)) return id;
        Error(m, ref, std::wstring(what) + L": no block or item has id " + IntToString(id));
        return -1;
    }
    if (ref->isString()) {
        std::wstring name = FullName(m, ref->string);
        int id = Registry::FindId(name.c_str());
        if (id > 0) return id;
        Error(m, ref, std::wstring(what) + L": nothing is registered as \"" + name + L"\"");
        return -1;
    }
    Error(m, ref, std::wstring(what) + L" must be a number (vanilla id) or a name");
    return -1;
}

int Lookup(const wchar_t *const *names, const wchar_t *value, int fallback)
{
    for (int i = 0; names[i]; ++i) {
        if (wcscmp(names[i], value) == 0) return i;
    }
    return fallback;
}

const wchar_t *const MATERIALS[] = { L"stone", L"wood", L"dirt", L"grass", L"sand", L"metal", L"glass",
                                     L"cloth", L"plant", L"leaves", L"ice", L"clay", NULL };
const wchar_t *const SOUNDS[] = { L"stone", L"wood", L"gravel", L"grass", L"metal", L"glass", L"cloth",
                                  L"sand", L"snow", NULL };

void RegisterItems(DataMod *m, const Value *list)
{
    static const wchar_t *const keys[] = { L"name", L"displayName", L"texture", L"maxStackSize",
                                           L"maxDamage", L"handEquipped", NULL };
    for (size_t i = 0; i < list->items.size(); ++i) {
        const Value *it = list->items[i];
        if (!it->isObject()) { Error(m, it, L"each item must be an object { ... }"); continue; }
        CheckKeys(m, it, keys, L"an item");
        const wchar_t *shortName = Str(it, L"name", NULL);
        if (shortName == NULL) { Error(m, it, L"item has no \"name\""); continue; }

        std::wstring name = FullName(m, shortName);
        FLCE_ItemDesc d;
        memset(&d, 0, sizeof(d));
        d.structSize   = sizeof(d);
        d.registryName = name.c_str();
        d.displayName  = Str(it, L"displayName", shortName);
        d.textureName  = Str(it, L"texture", L"stick");
        d.maxStackSize = (int)Num(it, L"maxStackSize", 0);
        d.maxDamage    = (int)Num(it, L"maxDamage", 0);
        d.handEquipped = Bool(it, L"handEquipped") ? 1 : 0;
        Registry::RegisterItem(&d); // logs its own errors
    }
}

void RegisterBlocks(DataMod *m, const Value *list)
{
    static const wchar_t *const keys[] = { L"name", L"displayName", L"material", L"sound", L"hardness",
                                           L"resistance", L"light", L"texture", L"drops", L"dropCount", NULL };
    for (size_t i = 0; i < list->items.size(); ++i) {
        const Value *b = list->items[i];
        if (!b->isObject()) { Error(m, b, L"each block must be an object { ... }"); continue; }
        CheckKeys(m, b, keys, L"a block");
        const wchar_t *shortName = Str(b, L"name", NULL);
        if (shortName == NULL) { Error(m, b, L"block has no \"name\""); continue; }

        std::wstring name = FullName(m, shortName);
        FLCE_BlockDesc d;
        memset(&d, 0, sizeof(d));
        d.structSize   = sizeof(d);
        d.registryName = name.c_str();
        d.displayName  = Str(b, L"displayName", shortName);
        d.material     = Lookup(MATERIALS, Str(b, L"material", L"stone"), -1);
        d.sound        = Lookup(SOUNDS, Str(b, L"sound", L"stone"), -1);
        if (d.material < 0) { Warn(m, b->get(L"material"), L"unknown material, using stone"); d.material = FLCE_MATERIAL_STONE; }
        if (d.sound < 0)    { Warn(m, b->get(L"sound"), L"unknown sound, using stone"); d.sound = FLCE_SOUND_STONE; }
        d.hardness      = (float)Num(b, L"hardness", 1.5);
        d.resistance    = (float)Num(b, L"resistance", 10.0);
        double light    = Num(b, L"light", 0);   // 0-15, like the game's light levels
        d.lightEmission = (float)(light < 0 ? 0 : light > 15 ? 1.0 : light / 15.0);
        d.textureName   = Str(b, L"texture", L"stone");
        d.dropCount     = (int)Num(b, L"dropCount", 0);

        const Value *drops = b->get(L"drops");
        if (drops != NULL && !(drops->isString() && drops->string == L"self")) {
            int dropId = Resolve(m, drops, L"drops");
            if (dropId < 0) continue;
            d.dropId = dropId;
        }
        Registry::RegisterBlock(&d);
    }
}

// One "key" entry of a shaped recipe: a reference, or { "item": ref, "data": n }.
bool ResolveKey(DataMod *m, const Value *v, FLCE_RecipeKey &key)
{
    const Value *ref = v;
    key.data = -1;
    if (v->isObject()) {
        ref = v->get(L"item");
        key.data = (int)Num(v, L"data", -1);
    }
    key.id = Resolve(m, ref, L"recipe key");
    return key.id > 0;
}

void AddShaped(DataMod *m, const Value *r, int result, int count, int data)
{
    const Value *pattern = r->get(L"pattern");
    const Value *keys = r->get(L"key");
    if (pattern == NULL || !pattern->isArray() || pattern->items.empty() || pattern->items.size() > 3) {
        Error(m, r, L"shaped recipe needs \"pattern\": 1-3 strings like \"XXX\"");
        return;
    }
    if (keys == NULL || !keys->isObject()) {
        Error(m, r, L"shaped recipe needs \"key\": { \"X\": item, ... }");
        return;
    }
    const wchar_t *rows[3];
    for (size_t i = 0; i < pattern->items.size(); ++i) {
        if (!pattern->items[i]->isString()) { Error(m, pattern, L"pattern rows must be strings"); return; }
        rows[i] = pattern->items[i]->string.c_str();
    }
    std::vector<FLCE_RecipeKey> resolved;
    for (size_t i = 0; i < keys->keys.size(); ++i) {
        if (keys->keys[i].size() != 1) { Error(m, keys->items[i], L"key symbols must be one character"); return; }
        FLCE_RecipeKey k;
        k.symbol = keys->keys[i][0];
        if (!ResolveKey(m, keys->items[i], k)) return;
        resolved.push_back(k);
    }
    Registry::AddShapedRecipe(result, count, data, rows, (int)pattern->items.size(),
                              resolved.empty() ? NULL : &resolved[0], (int)resolved.size());
}

void AddShapeless(DataMod *m, const Value *r, int result, int count, int data)
{
    const Value *list = r->get(L"ingredients");
    if (list == NULL || !list->isArray() || list->items.empty()) {
        Error(m, r, L"shapeless recipe needs \"ingredients\": [ item, ... ]");
        return;
    }
    std::vector<int> ids;
    for (size_t i = 0; i < list->items.size(); ++i) {
        int id = Resolve(m, list->items[i], L"ingredient");
        if (id < 0) return;
        ids.push_back(id);
    }
    Registry::AddShapelessRecipe(result, count, data, &ids[0], (int)ids.size());
}

void AddRecipes(DataMod *m, const Value *list)
{
    static const wchar_t *const keys[] = { L"type", L"result", L"count", L"data", L"pattern", L"key",
                                           L"ingredients", NULL };
    for (size_t i = 0; i < list->items.size(); ++i) {
        const Value *r = list->items[i];
        if (!r->isObject()) { Error(m, r, L"each recipe must be an object { ... }"); continue; }
        CheckKeys(m, r, keys, L"a recipe");
        int result = Resolve(m, r->get(L"result"), L"result");
        if (result < 0) continue;
        int count = (int)Num(r, L"count", 1);
        int data = (int)Num(r, L"data", 0);
        std::wstring type = Str(r, L"type", L"");
        if (type == L"shaped")         AddShaped(m, r, result, count, data);
        else if (type == L"shapeless") AddShapeless(m, r, result, count, data);
        else Error(m, r, L"recipe \"type\" must be \"shaped\" or \"shapeless\"");
    }
}

void AddSmelting(DataMod *m, const Value *list)
{
    static const wchar_t *const keys[] = { L"input", L"result", L"count", L"data", L"xp", NULL };
    for (size_t i = 0; i < list->items.size(); ++i) {
        const Value *s = list->items[i];
        if (!s->isObject()) { Error(m, s, L"each smelting entry must be an object { ... }"); continue; }
        CheckKeys(m, s, keys, L"a smelting entry");
        int input = Resolve(m, s->get(L"input"), L"input");
        int result = Resolve(m, s->get(L"result"), L"result");
        if (input < 0 || result < 0) continue;
        Registry::AddSmelting(input, result, (int)Num(s, L"count", 1), (int)Num(s, L"data", 0),
                              (float)Num(s, L"xp", 0));
    }
}

const Value *ListOrNull(DataMod *m, const wchar_t *key)
{
    const Value *v = m->doc.root->get(key);
    if (v == NULL) return NULL;
    if (!v->isArray()) { Error(m, v, std::wstring(L"\"") + key + L"\" must be a list [ ... ]"); return NULL; }
    return v;
}

} // namespace

DataMod *Load(const std::wstring &dir, const std::wstring &folderName)
{
    std::string text;
    if (!Platform::ReadFile(dir + L"forgedlce.json", text)) return NULL; // not a mod folder

    DataMod *m = new DataMod();
    m->file = folderName + Platform::PathSeparator() + L"forgedlce.json";
    std::wstring error;
    if (!m->doc.parse(text, error) || !m->doc.root->isObject()) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", m->file + L": " + (error.empty() ? std::wstring(L"must be an object { ... }") : error));
        delete m;
        return NULL;
    }
    const Value *root = m->doc.root;

    // A forgedlce.json with "source" describes a built-in code mod for
    // scripts/apply.ps1, not a data mod.
    if (root->get(L"source") != NULL) {
        delete m;
        return NULL;
    }
    const wchar_t *modId = Str(root, L"modId", NULL);
    if (!IsValidModId(modId)) {
        Log(FLCE_LOG_ERROR, L"ForgedLCE", m->file + L": \"modId\" must be non-empty lowercase a-z, 0-9, _");
        delete m;
        return NULL;
    }
    m->modId   = modId;
    m->name    = Str(root, L"name", modId);
    m->version = Str(root, L"version", L"");

    static const wchar_t *const keys[] = { L"modId", L"name", L"version", L"description", L"authors",
                                           L"items", L"blocks", L"recipes", L"smelting", NULL };
    CheckKeys(m, root, keys, L"the mod");
    return m;
}

const wchar_t *ModId(const DataMod *m)   { return m->modId.c_str(); }
const wchar_t *Name(const DataMod *m)    { return m->name.c_str(); }
const wchar_t *Version(const DataMod *m) { return m->version.c_str(); }

void PreInit(DataMod *m)
{
    // Items first, so this mod's blocks can drop this mod's items.
    const Value *items = ListOrNull(m, L"items");
    if (items != NULL) RegisterItems(m, items);
    const Value *blocks = ListOrNull(m, L"blocks");
    if (blocks != NULL) RegisterBlocks(m, blocks);
}

void Init(DataMod *m)
{
    const Value *recipes = ListOrNull(m, L"recipes");
    if (recipes != NULL) AddRecipes(m, recipes);
    const Value *smelting = ListOrNull(m, L"smelting");
    if (smelting != NULL) AddSmelting(m, smelting);
}

} // namespace DataMods
} // namespace ForgedLCE
