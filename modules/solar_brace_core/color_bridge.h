#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

struct Colour { float r, g, b, a; };

Colour color_lerp(Colour from, Colour to, float weight);

#ifdef __cplusplus
}
#endif