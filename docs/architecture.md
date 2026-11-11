# Architecture

This tree is one 32-bit ASI built from C++23 module units, in the shape GTAIV.EFLC.FusionFix uses: `source/common.hxx` is the shared include header, `source/dllmain.cpp` is the only plain translation unit, `source/*.ixx` are the module units, and the two vendored libraries are pinned submodules under `external/` (`external/injector/` and `external/hooking/`). `CMakeLists.txt` builds the ASI from there and adds `external/injector/include` and `external/hooking` to the include path. `source/dllmain.cpp` is the startup driver; the install arms live in the feature modules that own the keys.

## Module layout

| module | responsibility |
| --- | --- |
| `source/common.ixx` | `FusionFix::Event<Args...>`, the trace `Log`, the `Game` install context, and the module/path helpers |
| `source/patch.ixx` | image-ranged `find_pattern`, recovered-signature normalisation, the write primitives that need more than a direct `injector` call (`raw_mem`, `IATHook`) |
| `source/game.ixx` | build classification and the image scan range |
| `source/config.ixx` | the feather-ini-parser 1.41 grammar, the two per-model light tables, and the `std::stoi` read boundary |
| `source/options.ixx` | the generated catalogue of 99 keys (`source/options.inc`) |
| `source/loader.ixx` | the publish, install and post tiers, and the option gate |
| `source/bytes.ixx` | the opcode bytes the arms write, as named `std::byte` constants |
| `source/bodies.ixx` | the body, published-slot and registered-table registries |
| `source/shared.ixx` | the recovered value resolver and model lookup, transcribed |
| `source/native.ixx` | the native-script call layer and the loader preamble's salt rule |
| `source/stability.ixx` | 18 crash-fix keys |
| `source/vehicles.ixx` | 21 vehicle, steer and shadow keys |
| `source/useless_checks.ixx` | `RemoveUselessChecks`: the recovered per-build RVA tables and single writes |
| `source/graphics.ixx` | 16 rendering, light and shadow keys |
| `source/launcher.ixx` | the 25-row launcher stage, and the DFA and ROM stages |
| `source/loading.ixx` | 18 startup, loading, save and file-compatibility keys |
| `source/audio.ixx` | 13 keys the original groups with the radio and the world: audio, input, pointer pools, MP sync and the episodic weapons |
| `source/misc.ixx` | 13 misc keys: the bundled `MiscFixes` table, the mod-check stubs, the flags, the menu gate and the process gate |
| `source/menu.ixx` | the IVMenuAPI integration behind `AddOptionsInMenu` |
| `source/ooc.ixx` | the `OutOfCommissionFix` payload: the decoder, the archive builder and the mount |
| `source/dllmain.cpp` | the plain TU: `DllMain`, `InitializeASI` and the startup order |
| `source/common.hxx` | the include header every unit starts from |
| `external/injector/`, `external/hooking/` | the pinned memory-write library and signature scanner |

The catalogue's `module` field names one of six feature modules - `stability`, `vehicles`, `graphics`, `loading`, `audio` and `misc` - which own 18, 21, 16, 18, 13 and 13 keys: the 99-key catalogue. A feature module owns its patterns, its published slots, its config reads, its arms and its bodies, and the catalogue itself is generated data. The other units are infrastructure, or the implementation behind a single key: `useless_checks` carries `RemoveUselessChecks` (a `misc` key), `menu` carries `AddOptionsInMenu` (`misc`), `launcher` backs the `SkipLauncher` and `LaunchWithPlayGTAIV` stages, and `ooc` carries `OutOfCommissionFix` (an `audio` key).

## Startup

Everything happens synchronously on `DLL_PROCESS_ATTACH`. The original worked that way because the DRM has already rewritten the game's code by the time a deferred thread would run, so the signatures have to be matched now. `DllMain` calls `InitializeASI`, whose `std::call_once` body runs the startup path:

```
Game::Initialise            module, executable, image range, file version
Config::Load                ZolikaPatch.ini next to the ASI
Classify(FileVersion)       four fields -> compact product version -> Build
FusionFix::onInitEvent
Shared::Register            the recovered resolver and model lookup
Native::Register            the native-call layer and the salt rule
RegisterStability ... RegisterOoc   one call per module, in this order
Loader::Run                 PublishEvent, InstallEvent, PostEvent
```

The driver opens the trace log only in a `ZP_TRACE` build, and `DLL_PROCESS_DETACH` drains `onShutdownEvent` and closes it. Nothing is deferred, and nothing depends on `InitializeASI` being called by the loader: Ultimate ASI Loader calls it, every other loader gets the `DllMain` call.

## Event tiers

The ordering contract is FusionFix's `Event<Args...>` and three tiers in `source/loader.ixx`:

| tier | when it runs | what it does |
| --- | --- | --- |
| `PublishEvent` | before any install | reads a site's original operands and stores the resume addresses a body will tail-jump to |
| `InstallEvent` | after every publish | scans, registers the body, and writes the site through `injector` |
| `PostEvent` | after the last install | work that must follow the whole write set |

The publish tier exists because a site write destroys the signature a later publish would scan for, and because the original reads a site's original displacement before overwriting it. Running the publish phase first is what makes a body's resume point at the game's own continuation rather than at the shim.

An arm is gated by `Loader::Enabled(key)`: the key must be in the catalogue, the current `Build` must be in the key's mask (an `unknown` build is 0, which no mask contains), and the stored value must be exactly `1`. `Loader::EnabledNonZero` exists for the one key whose original consumer stores the parse's answer rather than comparing it with 1 (`AddOptionsInMenu`). A missing signature is the arm's own clean skip, as it is in the original.

## The shape of an arm

A feature module reads top to bottom: header, published state with the original's addresses, plain C++ helpers, the `Publish<Key>` functions, the `Install<Key>` functions, `Register<Key>`, and a marked tail holding the transcribed bodies. One arm is one short pair of functions plus its registration:

```cpp
void PublishFreezeGunFix()
{
    if (!Loader::Enabled("FreezeGunFix"))
        return;

    auto pattern = find_pattern("2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF");
    if (pattern.empty())
        return;

    zpatch_freeze_gun_resume = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) - 12);
}

void InstallFreezeGunFix()
{
    if (!Loader::Enabled("FreezeGunFix"))
        return;

    auto pattern = find_pattern("2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF");
    if (pattern.empty())
        return;

    Bodies::Register(reinterpret_cast<uintptr_t>(&FreezeGunShim), 0x10036EB0u);
    injector::MakeJMP(pattern.get_first(-21), reinterpret_cast<void*>(&FreezeGunShim), true);
}
```

`RegisterStability` subscribes those functions to the two tiers in the order the module needs. The order inside a registration is behavioural and does not change: arms that touch the same site depend on it. Because the driver calls each module's registration explicitly, the linker cannot drop an arm, and the whole schedule is readable in one place.

## Readable first, assembly in a marked tail

Every module follows the same rule: the readable code comes first, and every naked body and every `__asm` block sits in a tail section under a comment that names its recovered VA. That split is not cosmetic. MSVC rejects `__declspec(naked)` on a declaration (C2488), and an external-linkage naked function written to a module interface is copied into the BMI, so every importer fails. A body used above its definition therefore gets a plain `static` prototype in the readable section, and only the definition in the tail keeps the attribute. A body that must be reachable from another module is exported by address, or kept in the unit whose resolver it calls. `source/shared.ixx` is the documented case: the `PlayerAsPedComponentsFix` bodies live there because they `call` the resolver by name.

## Build classification

`Classify` concatenates the executable's four file-version fields and maps the result through the original's switch. The enum value is the original's build id, and the catalogue's masks index it (bit *i* is build *i*):

| product version | compact | build id |
| --- | ---: | ---: |
| 1.0.0.0 | 1000 | 1 |
| 1.0.4.0 | 1040 | 5 |
| 1.0.0.4 | 1004 | 6 |
| 1.0.6.0 | 1060 | 7 |
| 1.0.7.0 | 1070 | 8 |
| 1.0.8.0 | 1080 | 9 |
| 1.1.0.0 | 1100 | 10 |
| 1.1.1.0 | 1110 | 11 |
| 1.1.2.0 | 1120 | 12 |
| 1.1.3.0 | 1130 | 13 |
| 1.0.6.1 | 1061 | 15 |
| 1.2.0.43 | 12043 | 18 |

An unsupported executable classifies as `unknown` (0) and installs nothing.

## Diagnostics

The shipped ASI is silent: the original writes no log at all. Diagnostics live behind the `Trace` configuration, which defines `ZP_TRACE` and opens `ZolikaPatch.trace.log` next to the ASI. The trace records the module and executable paths, the classified build, the INI path and whether it was read, the enabled key set, and one line per decision the arms make. A `Release` build compiles the same calls to nothing and opens no file.

## Where the evidence is produced

The write set is measured by applying the arms to a mapped copy of a game image and diffing the bytes (`tools/parity_report.py`, `tools/dump_matrix.py`). The bodies are measured instruction for instruction against the original ASI (`tools/verify_bodies.py`). The pieces whose instruction stream cannot be compared are catalogued in [rebuilt.md](rebuilt.md), and the preserved faults are catalogued in [behavior.md](behavior.md). [hooking.md](hooking.md) describes the transcription and proof rules, and [testing.md](testing.md) lists every test and measurement tool.
