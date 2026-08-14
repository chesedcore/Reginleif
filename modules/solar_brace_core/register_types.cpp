#include "register_types.h"
#include "solar_brace_wrapper.h"
#include "core/variant/variant.h"

void initialize_solar_brace_core_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    solar_brace_init();
    const char* version = solar_brace_version();
    print_line(String(version));
}

void uninitialize_solar_brace_core_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    solar_brace_shutdown();
}