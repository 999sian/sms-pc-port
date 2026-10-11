#pragma once
#include <string>

namespace gx { namespace android {
#ifdef __ANDROID__
void install(void* (*getProc)(const char*));
bool desktopEntry(const char* name);
std::string shaderSource(const std::string& source, unsigned type);
bool metricActive();
#else
inline bool desktopEntry(const char*) { return false; }
inline std::string shaderSource(const std::string& source, unsigned) { return source; }
#endif
}}
