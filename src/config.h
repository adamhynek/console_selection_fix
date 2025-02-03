#pragma once

#include <unordered_set>

#include "skse64/NiNodes.h"
#include "skse64/GameData.h"


namespace Config {
    struct Options {
        float upOffset = -0.21f;
        float rightOffset = -0.29;
        float upMultiplier = 1.2f;
        float rightMultiplier = 0.88f;
    };
    extern Options options; // global object containing options

    // Fills Options struct from INI file
    bool ReadConfigOptions();
    bool ReloadIfModified();

    const std::string & GetConfigPath();

    std::string GetConfigOption(const char * section, const char * key);

    bool GetConfigOptionDouble(const char *section, const char *key, double *out);
    bool GetConfigOptionFloat(const char *section, const char *key, float *out);
    bool GetConfigOptionInt(const char *section, const char *key, int *out);
    bool GetConfigOptionUInt64Hex(const char *section, const char *key, UInt64 *out);
    bool GetConfigOptionBool(const char *section, const char *key, bool *out);
}
