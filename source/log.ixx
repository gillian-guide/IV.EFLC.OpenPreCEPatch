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

#include <filesystem>

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
// The owned trace log. Opening is lazy and retried on the next event when the
// first open failed, so a log file created later still receives writes (the
// original per-site std::fopen did the same). The release ASI never compiles
// this block.
class TraceLog final
{
public:
	std::ofstream* Stream() noexcept
	{
		if (stream_.is_open())
			return &stream_;
		stream_.clear();
		stream_.open(path_, std::ios::app);
		return stream_.is_open() ? &stream_ : nullptr;
	}

private:
	std::filesystem::path path_ = "ZolikaPatch.trace.log";
	std::ofstream stream_;
};

TraceLog& TraceFile() noexcept
{
	static TraceLog log;
	return log;
}

// The 8-digit upper-case hex the old fprintf format printed. One value per
// stream expression; the buffer is trace-only scratch.
const char* Hex(std::uint32_t value) noexcept
{
	static char text[9];
	std::snprintf(text, sizeof(text), "%08lX", static_cast<unsigned long>(value));
	return text;
}

const char* HeapState() noexcept
{
	return HeapValidate(GetProcessHeap(), 0, nullptr) ? "ok" : "BAD";
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
	std::ofstream* const log = TraceFile().Stream();
	if (log == nullptr)
		return EXCEPTION_CONTINUE_SEARCH;

	*log << "fault " << seen << " code=" << Hex(record->ExceptionCode)
	     << " eip=" << Hex(context->Eip) << " esp=" << Hex(context->Esp)
	     << " eax=" << Hex(context->Eax) << " ecx=" << Hex(context->Ecx)
	     << " edx=" << Hex(context->Edx) << " ebx=" << Hex(context->Ebx)
	     << " esi=" << Hex(context->Esi) << " edi=" << Hex(context->Edi)
	     << " ebp=" << Hex(context->Ebp) << "\n";
	*log << "fault addr=" << Hex(static_cast<std::uint32_t>(record->ExceptionInformation[1]))
	     << " thread=" << static_cast<unsigned long>(GetCurrentThreadId()) << "\n";

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
		*log << "frame " << i << " " << Hex(static_cast<std::uint32_t>(value)) << " "
		     << base_name << "+"
		     << Hex(static_cast<std::uint32_t>(
		                value - reinterpret_cast<std::uintptr_t>(mbi.AllocationBase)))
		     << "\n";
	}
	log->flush();
	return EXCEPTION_CONTINUE_SEARCH;
}

// Debug builds only: name the loader stage, so a fault before the install pass
// can be located from ZolikaPatch.trace.log.
void TraceBoot(const char* stage)
{
	std::ofstream* const log = TraceFile().Stream();
	if (log == nullptr)
		return;
	*log << "stage " << stage << "\n";
	// A corrupted heap only faults at the next allocation; validating here names
	// the stage that corrupted it, not the allocation that noticed.
	*log << "heap " << HeapState() << " stage:" << stage << "\n";
	log->flush();
}

void TraceBootValue(const char* label, std::uintptr_t value)
{
	std::ofstream* const log = TraceFile().Stream();
	if (log == nullptr)
		return;
	*log << "stage " << label << " " << Hex(static_cast<std::uint32_t>(value)) << "\n";
	log->flush();
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

#if defined(ZP_TRACE_INSTALLS)
// Debug builds only: name the arm that is about to run, so a boot fault can
// be narrowed down from the log.
void Trace(const char* name)
{
	std::ofstream* const log = TraceFile().Stream();
	if (log == nullptr)
		return;
	*log << "arm " << name << "\n";
	*log << "heap " << HeapState() << " arm:" << name << "\n";
#if defined(ZP_TRACE_WRITES)
	injector::g_traceArm = name;
#endif
	log->flush();
}
#else
void Trace(const char*) {}
#endif
