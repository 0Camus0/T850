#ifndef T800_DEVICE_INFO_H
#define T800_DEVICE_INFO_H

#include <Config.h>

#include <string>

namespace t850 {
  // Handheld detection in one place, so the input layer and the water solver agree on what the
  // machine is. Before this the platforms carried their own probes and the Steam Deck OLED was
  // invisible to them: its DMI reads `sys_vendor=Valve`, `product_name=Galileo`,
  // `board_name=Galileo` (an LCD Deck reads `Jupiter`), and the Linux probe only looked for the
  // strings "steam deck" and "jupiter".
  struct DeviceInfo {
    // True only for a Steam Deck. The other handhelds report a reason but stay false, because
    // the pressure-iteration cap below is an owner decision for the Deck specifically.
    bool steamDeck = false;
    // Human-readable reason for the handheld log line; empty when the machine is not a handheld.
    std::string handheldReason;
    // Raw identity strings, kept for the log line and for support reports.
    std::string systemVendor;
    std::string systemProduct;
    std::string systemBoard;
  };

  // Probed once (function-local static) and cached for the process lifetime.
  const DeviceInfo& GetDeviceInfo();
}

#endif
