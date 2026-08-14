#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

int32_t solar_brace_init();
void solar_brace_shutdown();
const char* solar_brace_version();

#ifdef __cplusplus
}
#endif