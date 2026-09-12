/*
 * Win98 compatibility for the static MinGW-w64 C++ runtime.
 *
 * GCC 13's win32 thread support puts all of its __gthr_win32_* entry points
 * in one libgcc object.  That object also references GetThreadId, which is
 * not exported by Windows 9x.  The SDL 1.2 build only needs the critical
 * section primitives used by libstdc++'s allocator/exception support, so
 * provide those four entry points locally.  This keeps gthr-win32.o out of
 * the link and therefore removes the XP-only import without changing the
 * application's single-threaded behavior.
 */

#include <windows.h>

void __gthr_win32_mutex_init_function(void *mutex)
{
    InitializeCriticalSection((LPCRITICAL_SECTION)mutex);
}

void __gthr_win32_mutex_destroy(void *mutex)
{
    DeleteCriticalSection((LPCRITICAL_SECTION)mutex);
}

int __gthr_win32_mutex_lock(void *mutex)
{
    EnterCriticalSection((LPCRITICAL_SECTION)mutex);
    return 0;
}

int __gthr_win32_mutex_unlock(void *mutex)
{
    LeaveCriticalSection((LPCRITICAL_SECTION)mutex);
    return 0;
}
