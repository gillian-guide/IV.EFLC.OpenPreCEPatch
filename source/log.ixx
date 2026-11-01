// zp.log - the opt-in diagnostics.
//
// `ZP_TRACE_INSTALLS` (a CMake option, default OFF) turns on the boot-stage
// trace, the heap check after each stage and the first-fault handler; the
// release ASI is silent and writes no log. The stage names are the loader
// pipeline's, so a fault before the install pass can be located from
// ZolikaPatch.trace.log.

module;

#include "common.hxx"
#include "common.h"

export module zp.log;

import zp.common;

export
{
void TraceBoot(const char* stage);

void TraceBootValue(const char* label, std::uintptr_t value);

// Installs the fault tracer; a release build installs nothing.
void InstallFaultTracer();

// The option pass's arm tracer (ZP_TRACE_INSTALLS): a release ASI is silent.
void Trace(const char* name);
}  // export

#if defined(ZP_TRACE_INSTALLS)
namespace
{
// The one trace-log handle: opened on first use. A failed open is retried by
// the next event (the original per-site code did the same), so the helper only
// returns the current handle and every caller still treats null as silent.
std::FILE* TraceLog() noexcept
{
	static std::FILE* log = nullptr;
	if (log == nullptr)
		log = std::fopen("ZolikaPatch.trace.log", "a");
	return log;
}
}  // namespace

// Debug builds only: log the first fault with its registers and the raw stack, so
// the module and call site that caused it can be identified from the log.
LONG WINAPI TraceFault(EXCEPTION_POINTERS* info)
{
	const auto* record = info->ExceptionRecord;
	const auto* context = info->ContextRecord;
	static int seen = 0;
	if (++seen > 8)
		return EXCEPTION_CONTINUE_SEARCH;
	std::FILE* const log = TraceLog();
	if (log == nullptr)
		return EXCEPTION_CONTINUE_SEARCH;

	std::fprintf(log,
	             "fault %d code=%08lX eip=%08lX esp=%08lX eax=%08lX ecx=%08lX edx=%08lX "
	             "ebx=%08lX esi=%08lX edi=%08lX ebp=%08lX\n",
	             seen, static_cast<unsigned long>(record->ExceptionCode),
	             static_cast<unsigned long>(context->Eip),
	             static_cast<unsigned long>(context->Esp),
	             static_cast<unsigned long>(context->Eax),
	             static_cast<unsigned long>(context->Ecx),
	             static_cast<unsigned long>(context->Edx),
	             static_cast<unsigned long>(context->Ebx),
	             static_cast<unsigned long>(context->Esi),
	             static_cast<unsigned long>(context->Edi),
	             static_cast<unsigned long>(context->Ebp));
	std::fprintf(log, "fault addr=%08lX thread=%lu\n",
	             static_cast<unsigned long>(record->ExceptionInformation[1]),
	             static_cast<unsigned long>(GetCurrentThreadId()));

	// Every stack dword that lies inside a mapped image is a candidate return
	// address; resolve it to module+offset so the call chain is readable.
	const auto* stack = reinterpret_cast<const std::uint32_t*>(context->Esp);
	for (int i = 0; i < 256; ++i)
	{
		const std::uintptr_t value = stack[i];
		if (value < zp::kMinPlausiblePointer)
			continue;
		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(reinterpret_cast<const void*>(value), &mbi, sizeof(mbi)) == 0)
			continue;
		if (mbi.Type != MEM_IMAGE)
			continue;
		char module[MAX_PATH] = {};
		if (GetModuleFileNameA(static_cast<HMODULE>(mbi.AllocationBase), module, MAX_PATH) == 0)
			continue;
		const char* base_name = module;
		for (const char* c = module; *c; ++c)
			if (*c == '\\' || *c == '/')
				base_name = c + 1;
		std::fprintf(log, "frame %02d %08lX %s+%08lX\n", i,
		             static_cast<unsigned long>(value), base_name,
		             static_cast<unsigned long>(value - reinterpret_cast<std::uintptr_t>(mbi.AllocationBase)));
	}
	std::fflush(log);
	return EXCEPTION_CONTINUE_SEARCH;
}

// Debug builds only: name the loader stage, so a fault before the install pass
// can be located from ZolikaPatch.trace.log.
void TraceBoot(const char* stage)
{
	std::FILE* const log = TraceLog();
	if (log == nullptr)
		return;
	std::fprintf(log, "stage %s\n", stage);
	// A corrupted heap only faults at the next allocation; validating here names
	// the stage that corrupted it, not the allocation that noticed.
	std::fprintf(log, "heap %s stage:%s\n",
	             HeapValidate(GetProcessHeap(), 0, nullptr) ? "ok" : "BAD", stage);
	std::fflush(log);
}

void TraceBootValue(const char* label, std::uintptr_t value)
{
	std::FILE* const log = TraceLog();
	if (log == nullptr)
		return;
	std::fprintf(log, "stage %s %08lX\n", label, static_cast<unsigned long>(value));
	std::fflush(log);
}
#else
void TraceBoot(const char*) {}
void TraceBootValue(const char*, std::uintptr_t) {}
#endif

void InstallFaultTracer()
{
#if defined(ZP_TRACE_INSTALLS)
	AddVectoredExceptionHandler(1, TraceFault);
#endif
}

// The arm tracer: one pass over every arm, in the order PatchGame runs them.
#if defined(ZP_TRACE_WRITES)
namespace injector
{
const char* g_traceArm = "?";
}
#endif

#if defined(ZP_TRACE_INSTALLS)
// Debug builds only: name the arm that is about to run, so a boot fault can
// be narrowed down from the log.
void Trace(const char* name)
{
	std::FILE* const log = TraceLog();
	if (log == nullptr)
		return;
	std::fprintf(log, "arm %s\n", name);
	std::fprintf(log, "heap %s arm:%s\n",
	             HeapValidate(GetProcessHeap(), 0, nullptr) ? "ok" : "BAD", name);
#if defined(ZP_TRACE_WRITES)
	injector::g_traceArm = name;
#endif
	std::fflush(log);
}
#else
void Trace(const char*) {}
#endif
