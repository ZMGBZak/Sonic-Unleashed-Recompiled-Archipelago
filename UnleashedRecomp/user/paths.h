#pragma once

#include <mod/mod_loader.h>

#define USER_DIRECTORY "UnleashedRecomp"

#ifndef GAME_INSTALL_DIRECTORY
#define GAME_INSTALL_DIRECTORY "."
#endif

extern std::filesystem::path g_executableRoot;

bool CheckPortable();
std::filesystem::path BuildUserPath();
const std::filesystem::path& GetUserPath();

inline std::filesystem::path GetGamePath()
{
#ifdef __APPLE__
    // On macOS, there is the expectation that the app may be installed to
    // /Applications/, and the bundle should not be modified. Thus we need
    // to install game files to the user directory instead of next to the app.
    return GetUserPath();
#else
    return GAME_INSTALL_DIRECTORY;
#endif
}

inline std::filesystem::path GetSavePath(bool checkForMods)
{
    return GetUserPath() / "save_archipelago";
}

// Returned file name may not necessarily be
// equal to SYS-DATA as mods can assign anything.
inline std::filesystem::path GetSaveFilePath(bool checkForMods)
{
    return GetSavePath(false) / "SYS-DATA";
}
