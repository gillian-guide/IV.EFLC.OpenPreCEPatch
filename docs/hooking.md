# Hook bodies

The original mod installs jumps/calls from the game into its own module. The recovered module must present the same code at the same entry points. There are two kinds of body:

- **ordinary C++ functions** with an explicit calling convention. Most helpers and several small stubs are written this way.
- **naked shims** for sites whose register/stack contract cannot be expressed in C++ (a tail jump to a handler-published resume, a callee-popped frame, a register the following instruction consumes). These keep the original instruction stream, with the original VA in the comment.

## Installs

The install blocks are applied through `injector` and `Hooking.Patterns`: `injector::WriteMemoryRaw` for byte writes, `injector::MakeCALL`/`MakeJMP` for branches, and `hook::make_range_pattern` (wrapped as `Pattern(image, ...)`) for
the signatures. This is the same shape as the original patch blocks.

## Rules

1. **Resumes are tail-jumped, never called.** `return resume();` compiles to `jmp`; a `call; ret` leaves a return address that a `ret N` resume eats.
2. **Keep the register/flag/stack contract.** The game's surrounding code may depend on EAX/EDX/ECX/EFLAGS or on a callee-cleaned stack.
3. **Publish before install.** The original handler reads a site's original displacement for the resume before overwriting it. Slot publishes therefore run before the signature installs.
4. **No new guards.** A body that the original dereferences without a check keeps that dereference. The preserved faults are listed in `docs/origins.md`.
5. **No invented entry point.** If a body cannot be recovered, leaving it out of `kAbiBodies` makes the destination a clean skip; do not guess at its code.

## A naked body

```cpp
// 0x100243A0 MouseFix body B (installed at game 0xA82265): save xmm0-7,
// call the advance helper, restore, replay the replaced instructions and
// tail-jump to the published resume.
[[maybe_unused]] __declspec(naked) void abi_mouse_advance() {
  __asm {
    pushad
    movss dword ptr [zpatch_mf_xmm_save], xmm0
    ...
    call abi_mf_advance_step
    ...
    popad
    jmp dword ptr [zpatch_mf_resume_b]
    __emit 0CCh
  }
}
```

The `pushad`/`popad` pair is the original's own; the body deliberately runs the helper with the caller's registers saved. Removing it would change which flags/registers the resumed code sees.

## A compiled helper

```cpp
// 0x100064B0 DrivebyFOVFix resolver trampoline.
extern "C" __declspec(noinline) std::uint32_t __cdecl
zpatch_driveby_resolve_seh(std::uint32_t hash, std::uint32_t edx_arg) {
  return zpatch::detail::driveby_resolve_seh(&zpatch_resolver_call, hash, edx_arg);
}
```

`driveby_resolve_seh` is a catch-all SEH frame. On an exception it returns 0; the caller then reads `[eax+0x0E]` and faults, exactly like the original. Do not add a null check here.

## Script-call dispatch

The two script-call layers differ:

- `sub_100038E0` receives the hash in ECX/ESI **and** the call block in EDX. The resolver must preserve both.
- `sub_10003980` is the unary path; EDX is scratch and may be clobbered.

The bodies live in `dllmain.cpp` as `zpatch_script_call_by_hash` and the exact transcriptions around them.

## LoadDLCs

The post-helper returns the object, ignores `GetModuleFileNameA`'s result, uses the 0x104-byte `operator new` buffer and `PathRemoveFileSpecA`, leaves `EDX = 0x40` on the all-absent path, and reproduces the original's register/apply callee cleanup. It is transcribed, not "cleaned up".

## One-time pattern scans

Speedup/GFWL bodies scan once, cache in the original globals, and call `cache - 51` unconditionally on a miss. That fault is intended. The guard is an explicit tri-state (`0` fresh / `-1` scanning / `1` done), because the original's CRT TLS epoch is loader state that cannot be reproduced. The tri- state is not a null check; it only prevents a recursive scan.

## Recovery

The naked bodies were compared with the shipped 7.65 module instruction-for-instruction during recovery, including registers, flags, memory writes, calls, fault location and resume. The compiled helpers were checked behaviourally. Those checks are not part of the public build.
