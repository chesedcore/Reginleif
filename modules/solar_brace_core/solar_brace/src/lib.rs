pub mod core;
use std::{ffi::c_float, os::raw::c_char};
use crate::core::math::color::Color;

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

#[unsafe(no_mangle)]
pub extern "C" fn color_lerp(from: Color, to: Color, weight: c_float) -> Color {
    from.lerp(&to, weight)
}
