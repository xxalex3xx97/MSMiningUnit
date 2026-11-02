// FILE: mods/MiningBenchMod/mb_docks.hpp
// WHAT: finds docks and items in the world (by in-game name or numeric id) and writes docks.txt/items.txt for the Lua Mining window.
// WHY separate: dock and item lookup is shared by the orders, test-tab and dock-job code.
// DEPENDS ON: mb_common (widen, log_line, get_dll_dir, write_atomic, u8, g_sig_dock_file, g_items_file_count, g_lists_logged), mb_mscore (ms_load, g_ms), mb_ue (obj_id, obj_display, read_text_prop).
// USED BY: mb_rocks, mb_orders, mb_ships, mb_testcmd, dllmain.cpp.
// GAME FACTS:
//   VERIFIED (logs): a dock's in-game name is its DisplayName TextProperty ("text prop read ok" is logged by dock_name).
//   VERIFIED (logs): an Item's numeric id is an int32 property named ID.
//   VERIFIED (brief Update 9): game-made docks have an empty Name property, so DisplayName is the name the player sees.
//   ASSUMPTION: an Item's display name is DisplayName, with GetDisplayName as fallback (obj_display in mb_ue).
#pragma once

namespace
{
    // ---------------- item lookup ----------------

    // WHAT: resolves one Item from a resource string: exact object name or display name (case-insensitive), or a numeric ID.
    // WHY: the INI accepts names or ids (brief Update 5). Zero or several matches return nullptr with the reason in `why`, so the caller reports it instead of guessing an item.
    UObject* find_item(const std::string& res, std::wstring& why)
    {
        if (!ms_load(why)) return nullptr;
        wchar_t w[256]{};
        UObject* o = static_cast<UObject*>(g_ms.find_item(res.c_str(), w, 256));
        if (!o) why = w;
        return o;
    }

    // ---------------- dock names ----------------

    // WHAT: the in-game name of a dock: its DisplayName text property (empty when there is none).
    // WHY: game docks have an empty Name property, so DisplayName is the name the player sees (brief Update 9/10). Logs "text prop read ok" once, as proof the reflection read works in game.
    std::wstring dock_name(UObject* o)
    {
        static bool ok_logged = false;
        std::wstring r = read_text_prop(o, STR("DisplayName"));
        if (!r.empty() && !ok_logged)
        {
            ok_logged = true;
            log_line(STR("text prop read ok: dock name '") + r + STR("'"));
        }
        return r;
    }

    // WHAT: returns the id of the one dock whose in-game name matches. Pass 0 = exact match (case-insensitive), pass 1 = substring. Returns -1 when none, -2 when several match (`why` says which).
    // WHY: lets the INI and the Lua window pick a dock by the name the player sees instead of a raw id (brief Update 10). Exact is tried first, so a full name wins over a longer name that merely contains it.
    int find_dock_by_name(const std::string& want_s, std::wstring& why)
    {
        if (!ms_load(why)) return -1;
        wchar_t w[256]{};
        const int id = g_ms.find_dock(want_s.c_str(), w, 256);
        if (id <= 0) why = w;
        return id;
    }

    // ---------------- lists for the Lua window ----------------

    // WHAT: writes docks.txt (id|in-game name|object name|class|display name) and items.txt (object name|display name) next to the dll. docks.txt is rewritten when its content changes (always on the first call); items.txt when the item count changes.
    // WHY: the Lua Mining window reads these files, so it can list docks and items with their names without doing its own reflection (brief Update 9). Errors are logged and never thrown into the engine tick.
    void write_lists()
    {
        try
        {
            auto dir = get_dll_dir();
            static ULONGLONG last_walk = 0;   // dock walk (full object scan) at most every 20 s; renames show within that
            if (last_walk != 0 && GetTickCount64() - last_walk < 20000) return;
            last_walk = GetTickCount64();
            std::vector<UObject*> v;
            UObjectGlobals::FindAllOf(STR("Dock"), v);
            std::string dk;
            int dock_rows = 0;
            for (UObject* o : v)
            {
                if (!o) continue;
                int id = obj_id(o);
                auto* cls = o->GetClassPrivate();
                dk += std::to_string(id) + "|" + u8(dock_name(o)) + "|" + u8(o->GetName()) + "|" +
                      u8(cls ? std::wstring(cls->GetName()) : std::wstring()) + "|" + u8(obj_display(o)) + "\n";
                ++dock_rows;
            }
            bool wrote = false;
            if (dk != g_sig_dock_file)
            {
                write_atomic(dir / "docks.txt", dk);
                g_sig_dock_file = dk;
                wrote = true;
            }

            // items never change in a session: one FindAllOf walk for items.txt, not one per scan
            static bool items_done = false;
            v.clear();
            if (!items_done) UObjectGlobals::FindAllOf(STR("Item"), v);
            int item_rows = items_done ? g_items_file_count : static_cast<int>(v.size());
            if (!items_done && item_rows > 0) items_done = true;
            if (item_rows != g_items_file_count)
            {
                std::string itxt;
                for (UObject* o : v)
                {
                    if (!o) continue;
                    itxt += u8(o->GetName()) + "|" + u8(obj_display(o)) + "\n";
                }
                write_atomic(dir / "items.txt", itxt);
                g_items_file_count = item_rows;
                wrote = true;
            }

            if (wrote && !g_lists_logged)
            {
                g_lists_logged = true;
                log_line(STR("lists written: docks=") + std::to_wstring(dock_rows) + STR(" items=") + std::to_wstring(item_rows) + STR(" (docks.txt, items.txt)"));
            }
        }
        catch (const std::exception& e) { log_line(STR("lists write failed: ") + widen(e.what())); }
        catch (...) { log_line(STR("lists write failed (unknown)")); }
    }
}
