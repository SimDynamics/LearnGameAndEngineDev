// OpenGL-only modular entry points for the SAME already-defined F12 engine.
// Each type has two REAL compatible operations; no artificial alternative or
// numerical implementation is introduced. The host selects the named original
// operation for every active uniform after each glUseProgram.
subroutine SmoD12 F12GlAddSubType(SmoD12 a,SmoD12 b);
subroutine(F12GlAddSubType) SmoD12 f12gl_add_impl(SmoD12 a,SmoD12 b){return smo_d12_add(a,b);}
subroutine(F12GlAddSubType) SmoD12 f12gl_sub_impl(SmoD12 a,SmoD12 b){return smo_d12_sub(a,b);}
subroutine uniform F12GlAddSubType f12gl_add_dispatch;
subroutine uniform F12GlAddSubType f12gl_sub_dispatch;

subroutine SmoD12 F12GlMulDivType(SmoD12 a,SmoD12 b);
subroutine(F12GlMulDivType) SmoD12 f12gl_mul_impl(SmoD12 a,SmoD12 b){return smo_d12_mul(a,b);}
subroutine(F12GlMulDivType) SmoD12 f12gl_div_impl(SmoD12 a,SmoD12 b){return smo_d12_div(a,b);}
subroutine uniform F12GlMulDivType f12gl_mul_dispatch;
subroutine uniform F12GlMulDivType f12gl_div_dispatch;

subroutine SmoD12 F12GlPowerType(SmoD12 a,SmoD12 exponent);
subroutine(F12GlPowerType) SmoD12 f12gl_pdir_impl(SmoD12 a,SmoD12 exponent){return smo_d12_pdir(a,exponent);}
subroutine(F12GlPowerType) SmoD12 f12gl_pinv_impl(SmoD12 a,SmoD12 exponent){return smo_d12_pinv(a,exponent);}
subroutine uniform F12GlPowerType f12gl_pdir_dispatch;
subroutine uniform F12GlPowerType f12gl_pinv_dispatch;

subroutine SmoD12 F12GlSqrtAbsType(SmoD12 a);
subroutine(F12GlSqrtAbsType) SmoD12 f12gl_sqrt_impl(SmoD12 a){return smo_d12_sqrt(a);}
subroutine(F12GlSqrtAbsType) SmoD12 f12gl_abs_impl(SmoD12 a){return smo_d12_abs(a);}
subroutine uniform F12GlSqrtAbsType f12gl_sqrt_dispatch;
subroutine uniform F12GlSqrtAbsType f12gl_abs_dispatch;

subroutine SmoD12 F12GlNormType(SmoD12 x,SmoD12 y,SmoD12 z);
subroutine(F12GlNormType) SmoD12 f12gl_norm3_impl(SmoD12 x,SmoD12 y,SmoD12 z){return smo_d12_norm3(x,y,z);}
subroutine(F12GlNormType) SmoD12 f12gl_magnitude2_impl(SmoD12 x,SmoD12 y,SmoD12 z){return smo_d12_magnitude2(x,y,z);}
subroutine uniform F12GlNormType f12gl_norm3_dispatch;
subroutine uniform F12GlNormType f12gl_magnitude2_dispatch;

subroutine SmoD12 F12GlDiscriminantClampType(SmoD12 a,SmoD12 b,SmoD12 c);
subroutine(F12GlDiscriminantClampType) SmoD12 f12gl_discriminant_impl(SmoD12 h,SmoD12 a,SmoD12 c){return smo_d12_discriminant(h,a,c);}
subroutine(F12GlDiscriminantClampType) SmoD12 f12gl_clamp_impl(SmoD12 a,SmoD12 low,SmoD12 high){return smo_d12_clamp(a,low,high);}
subroutine uniform F12GlDiscriminantClampType f12gl_discriminant_dispatch;
subroutine uniform F12GlDiscriminantClampType f12gl_clamp_dispatch;

// Aliases appear only AFTER every wrapper body. Consequently wrapper calls
// retain the original arithmetic functions, with no recursion or rewritten
// helper tree. Only the following shared scene uses these modular calls.
#define smo_d12_add f12gl_add_dispatch
#define smo_d12_sub f12gl_sub_dispatch
#define smo_d12_mul f12gl_mul_dispatch
#define smo_d12_div f12gl_div_dispatch
#define smo_d12_pdir f12gl_pdir_dispatch
#define smo_d12_pinv f12gl_pinv_dispatch
#define smo_d12_sqrt f12gl_sqrt_dispatch
#define smo_d12_abs f12gl_abs_dispatch
#define smo_d12_norm3 f12gl_norm3_dispatch
#define smo_d12_magnitude2 f12gl_magnitude2_dispatch
#define smo_d12_discriminant f12gl_discriminant_dispatch
#define smo_d12_clamp f12gl_clamp_dispatch
