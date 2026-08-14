//! the solar brace mirror of core/math/math_funcs, shared scalar math stuff

#[inline]
pub const fn lerp(from: f32, to: f32, weight: f32) -> f32 {
    from + (to - from) * weight
}

