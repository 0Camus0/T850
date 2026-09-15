#pragma once
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS
namespace t850 {
BaseDriver* CreateWindowsGraphicsDriver(GraphicsApi::E api);
}
#endif