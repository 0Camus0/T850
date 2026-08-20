#include <pch.h>

#include <debug/CrashDiagnostics.h>

#if defined(OS_WINDOWS) && defined(_DEBUG) && defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace t850 {

#if defined(OS_WINDOWS) && defined(_DEBUG) && defined(_MSC_VER)
namespace {

int __cdecl UnattendedCrtReportHook(int reportType, char* message, int* returnValue) {
  if (reportType != _CRT_ASSERT) return FALSE;
  if (message) OutputDebugStringA(message);
  if (returnValue) *returnValue = 1; // Retry: invoke _CrtDbgBreak without showing the dialog.
  return TRUE;
}

} // namespace
#endif

void InstallUnattendedCrtReportHook() {
#if defined(OS_WINDOWS) && defined(_DEBUG) && defined(_MSC_VER)
  _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, UnattendedCrtReportHook);
#endif
}

} // namespace t850
