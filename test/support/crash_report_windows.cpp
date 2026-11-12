/**
 * \file
 * Test support, Windows only: when the test binary crashes, print where.
 *
 * A debugger changes the timing enough to hide a crash that depends on it, so
 * the binary reports its own: the exception, the faulting thread, and the
 * stack as offsets into the image (RVAs). addr2line wants addresses at the
 * image's preferred base, as `objdump -p` prints it (ImageBase), so add it:
 *
 *     addr2line -f -C -i -e redis_unit_test.exe <ImageBase + each RVA>
 *
 * Nothing here runs unless the process is already dying, or uses a handle
 * that is not open (see below).
 */
#ifdef _WIN32

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

/**
 * Where \p address is in the image, as an offset from where it was loaded (an
 * RVA); 0 if outside it. Not relative to the in-memory ImageBase: the loader
 * rewrites that to the load address when it relocates the image.
 */
std::uint64_t in_image(const void *address) {
	const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
	const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	const auto *nt =
		reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
	const auto at = reinterpret_cast<std::uintptr_t>(address);
	if (at < base || at >= base + nt->OptionalHeader.SizeOfImage) return 0;
	return at - base;
}

LONG WINAPI report(EXCEPTION_POINTERS *info) {
	const EXCEPTION_RECORD *record = info->ExceptionRecord;
	std::fprintf(stderr,
				 "\n*** crash: exception 0x%08lx on thread %lu",
				 record->ExceptionCode,
				 GetCurrentThreadId());
	if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
		record->NumberParameters >= 2) {
		const auto kind = record->ExceptionInformation[0];
		std::fprintf(stderr,
					 ", %s of 0x%llx",
					 kind == 0 ? "read" : kind == 1 ? "write" : "execute",
					 static_cast<unsigned long long>(
						 record->ExceptionInformation[1]));
	}
	std::fprintf(stderr,
				 "\n*** faulting instruction: 0x%llx\n",
				 static_cast<unsigned long long>(
					 in_image(record->ExceptionAddress)));

	// The filter runs on the faulting thread, so this walk passes through the
	// exception dispatcher into the frames that crashed.
	void *frames[96];
	const USHORT count = RtlCaptureStackBackTrace(0, 96, frames, nullptr);
	std::fprintf(stderr, "*** stack:");
	for (USHORT i = 0; i < count; ++i) {
		if (const auto at = in_image(frames[i]))
			std::fprintf(stderr, " 0x%llx", static_cast<unsigned long long>(at));
	}
	std::fprintf(stderr, "\n");
	std::fflush(stderr);
	return EXCEPTION_CONTINUE_SEARCH;
}

// Installed before main(), for the whole run.
const bool installed = [] {
	SetUnhandledExceptionFilter(report);
	// Using or closing a handle that is not open (a double close, say) raises
	// STATUS_INVALID_HANDLE at once instead of failing quietly, or, worse,
	// later closing an unrelated handle that reused the value.
	PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY strict{};
	strict.RaiseExceptionOnInvalidHandleReference = 1;
	strict.HandleExceptionsPermanentlyEnabled     = 1;
	SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &strict,
							   sizeof(strict));
	return true;
}();

} // namespace

#endif
