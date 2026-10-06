// Entry point of the port. The original main() is compiled as gm_game_main (see shim/gmcompat.h) and
// reached through gm_main_entry in PLAYGAME.C, because its parameters are 16-bit ints there.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" int32_t gm_main_entry(int32_t argc, char **argv);
extern "C" volatile uint32_t gm_heartbeat;      // bumped by gm_pump(); the watchdog below watches it

#ifdef _WIN32
// Diagnostics: a crash reporter and a hang watchdog that print symbolised call stacks
// (they need the PDB next to the exe).
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static void print_stack(HANDLE thread, CONTEXT ctx)
{
    HANDLE proc = GetCurrentProcess();
    STACKFRAME64 sf = {};
    sf.AddrPC.Offset = ctx.Rip;    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 24 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &sf, &ctx, NULL,
                                           SymFunctionTableAccess64, SymGetModuleBase64, NULL); i++) {
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line = { sizeof(line) };
        DWORD ldisp = 0;
        const char *name = SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym) ? sym->Name : "?";
        if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ldisp, &line))
            fprintf(stderr, "  #%d %s  (%s:%lu)\n", i, name, line.FileName, line.LineNumber);
        else
            fprintf(stderr, "  #%d %s\n", i, name);
    }
    fflush(stderr);
}

static void init_symbols()
{
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    init_symbols();
    fprintf(stderr, "\n*** crash: exception %08lX at %p", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        fprintf(stderr, " (%s address %p)", ep->ExceptionRecord->ExceptionInformation[0] ? "write to" : "read of",
                (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    fprintf(stderr, "\n");
    print_stack(GetCurrentThread(), *ep->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;
}

// The C runtime calls this for invalid arguments (fopen(NULL), a closed FILE*, ...) instead of failing quietly.
static void invalid_parameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t)
{
    init_symbols();
    fprintf(stderr, "\n*** invalid parameter passed to a C runtime function; call stack:\n");
    CONTEXT ctx = {};
    RtlCaptureContext(&ctx);
    print_stack(GetCurrentThread(), ctx);
    ExitProcess(4);
}

// GM_EXITTRACE=1: print who called exit() (the stack is still intact inside an atexit handler).
static void trace_exit()
{
    init_symbols();
    fprintf(stderr, "\n*** exit() called from:\n");
    CONTEXT ctx = {};
    RtlCaptureContext(&ctx);
    print_stack(GetCurrentThread(), ctx);
}

static HANDLE main_thread;

static DWORD WINAPI watchdog(LPVOID)
{
    uint32_t last = gm_heartbeat;
    int stalled = 0;
    for (;;) {
        Sleep(1000);
        if (gm_heartbeat != last) { last = gm_heartbeat; stalled = 0; continue; }
        if (++stalled < 5) continue;
        SuspendThread(main_thread);
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_FULL;
        GetThreadContext(main_thread, &ctx);
        init_symbols();
        fprintf(stderr, "\n*** hang: the engine has not reached the platform layer for %d s; main thread is in:\n", stalled);
        print_stack(main_thread, ctx);
        ExitProcess(3);
    }
}
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_filter);
    _set_invalid_parameter_handler(invalid_parameter);
    if (getenv("GM_EXITTRACE")) atexit(trace_exit);
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    if (getenv("GM_WATCHDOG")) CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
#endif
    return gm_main_entry(argc, argv);
}
