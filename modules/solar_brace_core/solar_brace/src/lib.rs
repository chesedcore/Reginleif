mod core;

use std::os::raw::c_char;

#[unsafe(no_mangle)]
pub extern "C" fn solar_brace_init() -> i32 {
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn solar_brace_shutdown() {
    //cleanup goes here
}

#[unsafe(no_mangle)]
pub extern "C" fn solar_brace_version() -> *const c_char {
    b"Solar Brace, version 0.1.0\0".as_ptr() as *const c_char
}