#include <pch.h>
#include <utils/DeviceInfo.h>

#include <algorithm>
#include <cctype>
#include <string>

#if defined(OS_WINDOWS)
#include <windows.h>
#elif defined(OS_LINUX)
#include <fstream>
#endif

namespace t850 {
  namespace {
#if defined(OS_LINUX) || defined(OS_WINDOWS)
    // Only the two desktop platforms probe anything; Android and Emscripten return the default.
    // The helper block is compiled out there so a warnings-as-errors toolchain cannot complain
    // about unused static functions.
    std::string ToLowerAscii(std::string value) {
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
      });
      return value;
    }

    // DMI and BIOS strings are space-padded on some boards, which would both break an exact
    // compare and pollute the log line.
    std::string TrimAsciiWhitespace(std::string value) {
      const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
      value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
      value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
      return value;
    }

    bool ContainsNoCase(const std::string& haystack, const char* needle) {
      if (!needle || !*needle) return false;
      return ToLowerAscii(haystack).find(ToLowerAscii(needle)) != std::string::npos;
    }
#endif

#if defined(OS_LINUX)
    std::string ReadFirstLine(const char* path) {
      std::ifstream file(path);
      std::string value;
      std::getline(file, value);
      return TrimAsciiWhitespace(value);
    }
#endif

#if defined(OS_WINDOWS)
    // `buffer` is fixed-size on purpose: BIOS identity strings are tens of bytes, and a long value
    // returns ERROR_MORE_DATA, which fails closed to "unknown machine" rather than producing a
    // truncated vendor name that could match the wrong device.
    std::string ReadRegistryString(HKEY root, const char* subKey, const char* valueName) {
      HKEY key = nullptr;
      if (RegOpenKeyExA(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
      }
      char buffer[512] = {};
      DWORD type = 0;
      DWORD size = sizeof(buffer);
      const LSTATUS status = RegQueryValueExA(key, valueName, nullptr, &type,
                                              reinterpret_cast<LPBYTE>(buffer), &size);
      RegCloseKey(key);
      if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return {};
      }
      return TrimAsciiWhitespace(std::string(buffer));
    }
#endif

#if defined(OS_LINUX) || defined(OS_WINDOWS)
    // A Steam Deck reports Valve as the vendor with a `Jupiter` (LCD) or `Galileo` (OLED)
    // product/board. Both fields are checked because the OLED sets product and board to the same
    // value while third-party skins and some BIOS revisions only populate one of them. The vendor
    // test is a substring on purpose (a "Valve Corporation" spelling still matches) and cannot
    // produce a false positive on its own, because the Jupiter/Galileo condition must hold too.
    bool LooksLikeSteamDeck(const std::string& vendor, const std::string& product,
                            const std::string& board, const std::string& probe) {
      const bool valve = ContainsNoCase(vendor, "valve");
      const bool deckModel = ContainsNoCase(product, "jupiter") || ContainsNoCase(board, "jupiter") ||
                             ContainsNoCase(product, "galileo") || ContainsNoCase(board, "galileo");
      if (valve && deckModel) return true;
      // Fallback for a machine that names itself without matching the DMI vendor, which is what
      // the pre-shared probes relied on.
      return ContainsNoCase(probe, "steam deck");
    }

    const char* kHandheldReasonSteamDeck = "Steam Deck";
#endif

    DeviceInfo ProbeDeviceInfo() {
      DeviceInfo info;
#if defined(OS_LINUX)
      info.systemVendor = ReadFirstLine("/sys/devices/virtual/dmi/id/sys_vendor");
      info.systemProduct = ReadFirstLine("/sys/devices/virtual/dmi/id/product_name");
      info.systemBoard = ReadFirstLine("/sys/devices/virtual/dmi/id/board_name");
      const std::string probe = info.systemProduct + " " + info.systemBoard + " " + info.systemVendor;
      if (LooksLikeSteamDeck(info.systemVendor, info.systemProduct, info.systemBoard, probe)) {
        info.steamDeck = true;
        info.handheldReason = std::string(kHandheldReasonSteamDeck) + " (" + probe + ")";
      }
#elif defined(OS_WINDOWS)
      const std::string manufacturer = ReadRegistryString(
        HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemManufacturer");
      const std::string product = ReadRegistryString(
        HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemProductName");
      const std::string baseBoard = ReadRegistryString(
        HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BaseBoardProduct");
      info.systemVendor = manufacturer;
      info.systemProduct = product;
      info.systemBoard = baseBoard;
      const std::string probe = manufacturer + " " + product + " " + baseBoard;

      if (LooksLikeSteamDeck(manufacturer, product, baseBoard, probe)) {
        info.steamDeck = true;
        info.handheldReason = std::string(kHandheldReasonSteamDeck) + " (" + product + ")";
      } else if (ContainsNoCase(probe, "rog ally") || ContainsNoCase(probe, "rc71") ||
                 ContainsNoCase(probe, "rc72")) {
        info.handheldReason = "ASUS ROG Ally/Ally X (" + product + " " + baseBoard + ")";
      } else if (ContainsNoCase(probe, "legion go")) {
        info.handheldReason = "Lenovo Legion Go (" + product + ")";
      } else if (ContainsNoCase(probe, "msi claw")) {
        info.handheldReason = "MSI Claw (" + product + ")";
      } else if (ContainsNoCase(probe, "ayaneo")) {
        info.handheldReason = "AYANEO (" + product + ")";
      } else if (ContainsNoCase(probe, "onexplayer")) {
        info.handheldReason = "ONEXPLAYER (" + product + ")";
      } else if (ContainsNoCase(probe, "gpd win") || ContainsNoCase(probe, "gpd handheld")) {
        info.handheldReason = "GPD handheld (" + product + ")";
      } else if (ContainsNoCase(probe, "aokzoe")) {
        info.handheldReason = "AOKZOE (" + product + ")";
      }
#endif
      // Android and Emscripten run on phones and browsers, neither of which is a Steam Deck, and
      // neither exposes a probe worth doing.
      return info;
    }
  }

  const DeviceInfo& GetDeviceInfo() {
    static const DeviceInfo info = ProbeDeviceInfo();
    return info;
  }
}
