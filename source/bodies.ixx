module;

#include <common.hxx>

export module bodies;

import common;

// Transcribed hook bodies and their recovered addresses.
//
// The shipped ASI jumps to the body's own code; the offline tools cannot, because
// the body sits in a 32-bit module and the dump runs in a tool of its own. So a
// branch that points at a registered body is rewritten, before the write set is
// compared, to the displacement the original ASI has at that site: the body's
// recovered VA. Without this the dump's bytes would depend on where the tool
// happened to place the body, and could never match a baseline.
export namespace Bodies
{
    struct Entry
    {
        uintptr_t address;   // where the body lives in this process
        uint32_t reference;  // the VA it has in the original ASI
    };

    inline std::vector<Entry>& Registry()
    {
        static std::vector<Entry> entries;
        return entries;
    }

    inline void Register(uintptr_t address, uint32_t reference)
    {
        Registry().push_back({ address, reference });
    }

    // The 4-byte slots an arm writes a module address into. The offline tools have
    // to record the original's placeholder for those words instead of a host
    // address, and only the arm knows which words they are: a scan for "looks like
    // an address in this process" cannot tell a published pointer from four bytes of
    // code that happen to form one.
    inline std::vector<uintptr_t>& PublishedRegistry()
    {
        static std::vector<uintptr_t> slots;
        return slots;
    }

    inline void RegisterPublished(uintptr_t slot)
    {
        PublishedRegistry().push_back(slot);
    }

    inline std::span<const uintptr_t> Published()
    {
        return PublishedRegistry();
    }

    // The 4-byte slots an arm writes a *game image* address into (an operand that
    // points at a float or a table inside the game executable). The baselines record
    // the reference dump's own placeholder for those words, so the dump has to
    // substitute it - and again only the arm knows which words they are.
    inline std::vector<uintptr_t>& PublishedImageRegistry()
    {
        static std::vector<uintptr_t> slots;
        return slots;
    }

    inline void RegisterPublishedImage(uintptr_t slot)
    {
        PublishedImageRegistry().push_back(slot);
    }

    inline std::span<const uintptr_t> PublishedImage()
    {
        return PublishedImageRegistry();
    }

    // The tables a transcribed body reads through an indexed absolute operand
    // (`push ds:off_100DE770[esi*4]`, `movss xmm0, ds:[esi*4+0x1015B5C0]`). The
    // original keeps such a table in its own .data, at a VA this module cannot
    // place its copy at, so the offline proof compared the displacement literally
    // and such a body could never pass. Registering the table names it: the call
    // here is mirrored by a `table` row in tests/bodies.tsv (the offline
    // projection - a 32-bit module's runtime vector is not readable from the file,
    // and the linker map is the only offline symbol-to-address bridge), and
    // tools/verify_bodies.py resolves the operand through that row, comparing
    // `table:<content hash>` instead of the address. The question it then answers is
    // the one that matters - "does the body read the same table?" - and not "does
    // the table sit at the same VA?".
    //
    // `bytes` is the table's length in the original ASI, and the hash covers
    // exactly those bytes. A table that is not registered keeps the literal
    // comparison, so nothing is relaxed by accident.
    struct Table
    {
        uintptr_t address;   // where the table lives in this process
        uint32_t reference;  // the VA it has in the original ASI
        size_t bytes;        // its length there
    };

    inline std::vector<Table>& TableRegistry()
    {
        static std::vector<Table> tables;
        return tables;
    }

    inline void RegisterTable(uintptr_t ourTable, uint32_t originalVa, size_t bytes)
    {
        TableRegistry().push_back({ ourTable, originalVa, bytes });
    }

    inline std::span<const Table> Tables()
    {
        return TableRegistry();
    }

    inline std::span<const Entry> All()
    {
        return Registry();
    }

    inline bool ReferenceFor(uintptr_t address, uint32_t& reference)
    {
        for (const auto& entry : Registry())
        {
            if (entry.address == address)
            {
                reference = entry.reference;
                return true;
            }
        }
        return false;
    }

    // The inverse lookup the modules used to copy: the host address of a
    // registered body reference, or 0 when it was not registered.
    inline uintptr_t AddressFor(uint32_t reference)
    {
        for (const auto& entry : Registry())
        {
            if (entry.reference == reference)
                return entry.address;
        }
        return 0;
    }

    // Rewrites every E8/E9 the pass wrote whose target is a registered body. A
    // branch the pass did not write is left alone (its byte still equals the
    // pristine one).
    // `hostBase` is where the image buffer sits in this process -- what the
    // injector wrote its displacement against. `imageBase` is the image's own
    // preferred base, which is the coordinate system a baseline is expressed in.
    // Matching the target needs the first; emitting the displacement needs the
    // second, and using one for both is off by hostBase - imageBase.
    // `pristine` is no longer consulted for the skip: a site that was already an
    // E9/E8 in the image (a hook installed over an existing branch, which is what
    // BlackscreenFix and CrashMsgFix do) writes the same opcode it found, so
    // "the byte changed" would miss exactly those sites. A branch whose target is
    // a registered body is ours whether or not the opcode byte differs.
    inline size_t Canonicalise(uint8_t* bytes, const uint8_t* pristine, size_t size,
                               uintptr_t hostBase, uintptr_t imageBase)
    {
        (void)pristine;
        size_t rewritten = 0;
        for (size_t offset = 0; offset + 5 <= size; ++offset)
        {
            const uint8_t opcode = bytes[offset];
            if (opcode != 0xE8 && opcode != 0xE9)
                continue;

            int32_t displacement{};
            std::memcpy(&displacement, bytes + offset + 1, sizeof(displacement));
            const auto site = static_cast<uintptr_t>(hostBase + offset);
            const auto target = static_cast<uintptr_t>(static_cast<intptr_t>(site + 5 + displacement));

            uint32_t reference{};
            if (!ReferenceFor(target, reference))
                continue;

            const auto imageSite = static_cast<uintptr_t>(imageBase + offset);
            const auto canonical = static_cast<int32_t>(static_cast<intptr_t>(reference) - static_cast<intptr_t>(imageSite + 5));
            std::memcpy(bytes + offset + 1, &canonical, sizeof(canonical));
            ++rewritten;
        }
        return rewritten;
    }
}
