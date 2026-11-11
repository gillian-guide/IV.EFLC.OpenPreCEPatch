module;

#include <common.hxx>

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
export namespace Loader
{
    // The build the loader classified at startup. An unsupported executable is
    // `unknown` (id 0), which no key's mask contains.
    inline Build& Current()
    {
        static Build build = Build::unknown;
        return build;
    }

    // Publishes hand a site's original operands to a body before the site is
    // written; installs write the site; posts run after the last install.
    inline FusionFix::Event<>& PublishEvent() { static FusionFix::Event<> event; return event; }
    inline FusionFix::Event<>& InstallEvent() { static FusionFix::Event<> event; return event; }
    inline FusionFix::Event<>& PostEvent() { static FusionFix::Event<> event; return event; }

    // An arm runs only when its key is on and the current build is in the key's
    // mask.
    inline bool Enabled(std::string_view key)
    {
        const auto* option = Catalogue::Find(key);
        if (option == nullptr || !Catalogue::AppliesTo(*option, Current()))
            return false;
        return Catalogue::Read(*option) == 1;
    }

    inline void Run()
    {
        PublishEvent().executeAll();
        InstallEvent().executeAll();
        PostEvent().executeAll();
    }
}
