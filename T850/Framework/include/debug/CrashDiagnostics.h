#pragma once

namespace t850 {

// In Windows Debug builds, suppress the CRT Abort/Retry/Ignore dialog and
// select Retry programmatically so an attached debugger receives the assert
// breakpoint immediately. No-op on other platforms and Release builds.
void InstallUnattendedCrtReportHook();

} // namespace t850
