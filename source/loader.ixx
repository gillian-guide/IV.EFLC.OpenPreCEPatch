module;

#include <common.hxx>
#include <algorithm>

export module loader;

import common;
import config;
import game;
import options;

// The load pipeline. The original reads every option once, in its own order, and
// installs each arm as it goes; the two orderings that are load-bearing are
// pinned by the dump's ordering check:
//
//   * every publish runs before any install -- a site write destroys the
//     signature a later publish scans for, and the original reads a site's
//     original bytes for the resume before it overwrites them;
//   * RemoveUselessChecks runs before MiscFixes and RestoreDeathMusic, because
//     its tables destroy both signatures (the original applies it once, at the
//     same point, and the two installs are suppressed there).
//
// Arms do not subscribe to the tiers as they are registered. Each one is queued
// with the driver position it had before the recategorisation (`entry`) and its
// position inside that entry (`index`), and the first Run() subscribes the two
// tiers sorted by that pair. The pair is therefore the arm's ordering identity:
// moving an arm from one module to another does not change when it runs, which
// is what lets the section split be a pure move.
export namespace Loader
{
    // The build the loader classified at startup. An unsupported executable is
    // `unknown` (id 0), which no key's mask contains.
    inline Build& Current()
    {
        static Build build = Build::unknown;
        return build;
    }

    // The driver positions, in the order the startup driver called the original
    // module `Register*` functions. The names are historical: they stay after a
    // module is merged into a section module, because an arm keeps the entry it
    // was registered from when it moves.
    enum class Entry : int
    {
        Stability = 0,
        Vehicles,
        UselessChecks,
        Graphics,
        Launcher,
        Loading,
        Audio,
        Misc,
        Menu,
        Ooc,
    };

    // Publishes hand a site's original operands to a body before the site is
    // written; installs write the site; posts run after the last install.
    inline FusionFix::Event<>& PublishEvent() { static FusionFix::Event<> event; return event; }
    inline FusionFix::Event<>& InstallEvent() { static FusionFix::Event<> event; return event; }
    inline FusionFix::Event<>& PostEvent() { static FusionFix::Event<> event; return event; }

    // One queued arm: the original driver position (`entry`), the position inside
    // that entry (`index`) and the function the tier runs.
    struct Arm
    {
        int entry;
        int index;
        void (*run)();
    };

    inline std::vector<Arm>& PublishArms()
    {
        static std::vector<Arm> arms;
        return arms;
    }

    inline std::vector<Arm>& InstallArms()
    {
        static std::vector<Arm> arms;
        return arms;
    }

    // An arm is queued from its module's `Register*` function with the position
    // the original driver gave it: the entry it was registered from and its index
    // inside that entry.
    inline void AddPublish(Entry entry, int index, void (*run)())
    {
        PublishArms().push_back({ static_cast<int>(entry), index, run });
    }

    inline void AddInstall(Entry entry, int index, void (*run)())
    {
        InstallArms().push_back({ static_cast<int>(entry), index, run });
    }

    // An arm runs only when its key is on and the current build is in the key's
    // mask. The gate is the original's `if (std::stoi(ini.get(key, default)))`,
    // including the throw a non-numeric value raises.
    inline bool Enabled(std::string_view key)
    {
        if (!Catalogue::AppliesTo(key, Current()))
            return false;
        return Catalogue::Read(key) == 1;
    }

    // One key's stored flag is the parse's answer, not its equality with 1.
    // `AddOptionsInMenu` is stored as `setnz` (`0x1005332E test ecx,ecx` /
    // `0x10053337 setnz byte_10158038`) over the inlined `std::stoi` of the value
    // (`push 0Ah ; Radix` / `call ds:strtol` / `cmp esi,[ebp-0C8h]` /
    // `jnz` -> `_Xinvalid_argument`, i.e. base 10 and the same throw), so `2`
    // enables it as surely as `1` does. The mask and the absent-key default still
    // come from the catalogue, and the read is the same `Catalogue::Read` every
    // other key gets; only the comparison differs, and only where an arm asks for
    // this one.
    inline bool EnabledNonZero(std::string_view key)
    {
        if (!Catalogue::AppliesTo(key, Current()))
            return false;
        return Catalogue::Read(key) != 0;
    }

    // Subscribe one tier's queued arms in (entry, index) order, then clear the
    // queue: the event owns its handlers now.
    inline void SubscribeArms(std::vector<Arm>& arms, FusionFix::Event<>& event)
    {
        std::stable_sort(arms.begin(), arms.end(), [](const Arm& left, const Arm& right)
        {
            return left.entry != right.entry ? left.entry < right.entry
                                             : left.index < right.index;
        });
        for (const auto& arm : arms)
            event += arm.run;
        arms.clear();
    }

    inline void Run()
    {
        // The first Run subscribes the queued arms; a later call only re-runs the
        // tiers it has already built.
        static bool subscribed = false;
        if (!subscribed)
        {
            SubscribeArms(PublishArms(), PublishEvent());
            SubscribeArms(InstallArms(), InstallEvent());
            subscribed = true;
        }

        PublishEvent().executeAll();
        InstallEvent().executeAll();
        PostEvent().executeAll();
    }
}
