use super::math_funcs::lerp as scalar_lerp;

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Color {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}



impl Color {

    #[inline]
    pub const fn new(r: f32, g: f32, b: f32, a: f32) -> Self {
        Self { r, g, b, a }
    }

    #[inline]
    pub const fn rgb(r: f32, g: f32, b: f32) -> Self {
        Self { r, g, b, a: 1.0 }
    }

    #[inline]
    pub const fn get_luminance(&self) -> f32 {
        (0.2126 * self.r) + (0.7152 * self.g) + (0.0722 * self.b) 
    }

    #[inline]
    pub const fn lerp(&self, to: &Color, weight: f32) -> Self {
        Self::new(
            scalar_lerp(self.r, to.r, weight), 
            scalar_lerp(self.g, to.g, weight),
            scalar_lerp(self.b, to.b, weight), 
            scalar_lerp(self.a, to.a, weight),
        )
    }

    #[inline]
    pub const fn darkened(&self, amount: f32) -> Self {
        Self::new(
            self.r * (1.0 - amount), 
            self.g * (1.0 - amount), 
            self.b * (1.0 - amount),
            self.a
        )
    }

    #[inline]
    pub const fn lightened(&self, amount: f32) -> Self {
        Self::new(
            self.r + (1.0 - self.r) * amount, 
            self.g + (1.0 - self.g) * amount, 
            self.b + (1.0 - self.b) * amount,
            self.a
        )
    }

    #[inline]
    pub const fn blend(&self, over: &Color) -> Self {
        let source_alpha = 1.0 - over.a;
        let res_alpha = self.a * source_alpha + over.a;

        if res_alpha == 0.0 {
            Self::new(0.0, 0.0, 0.0, 0.0)
        } else {
            Self::new(
                (self.r * self.a * source_alpha + over.r * over.a) / res_alpha,
                (self.g * self.a * source_alpha + over.g * over.a) / res_alpha,
                (self.b * self.a * source_alpha + over.b * over.a) / res_alpha,
                res_alpha
            )
        }
    }

    #[inline]
    pub const fn clamp(&self, min: &Color, max: &Color) -> Self {
        Self::new(
            self.r.clamp(min.r, max.r),
            self.g.clamp(min.g, max.g),
            self.b.clamp(min.b, max.b),
            self.a.clamp(min.a, max.a),
        )
    }

    #[inline]
    pub const fn clamp_default(&self) -> Self {
        self.clamp(
            &Color::new(0.0, 0.0, 0.0, 0.0), 
            &Color::new(1.0, 1.0, 1.0, 1.0)
        )
    }    
}