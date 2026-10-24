// NATIVE BINARY FP32 CONTROL, NOT THE SOFTWARE RADIX12 ENGINE.
// Reuses the scene API/control flow and source magnitudes for a representation
// comparison. digits.x holds the SIGNED IEEE float bits, digits.yzw are zero;
// no radix20736 arithmetic or hidden fixed-point quantization occurs here.
// The real D12 module must never load this representation as its own numbers.
// Requires REAL/BITS, uint4 and raw word/put from the graphics API prefix.
struct SmoD12 {uint4 digits;uint negative;uint diagnostic;};
SmoD12 smo_d12_zero(){SmoD12 r;r.digits=uint4(0u,0u,0u,0u);r.negative=0u;r.diagnostic=0u;return r;}
SmoD12 smo_d12_failure(uint diagnostic){SmoD12 r=smo_d12_zero();r.diagnostic=diagnostic;return r;}
bool d12_control_nonfinite(uint bits){return (bits&0x7f800000u)==0x7f800000u;}
bool d12_control_subnormal(uint bits){return (bits&0x7f800000u)==0u&&(bits&0x007fffffu)!=0u;}
uint d12_control_diagnostics(SmoD12 value){uint diagnostic=value.diagnostic;
    if((value.digits.y|value.digits.z|value.digits.w)!=0u||d12_control_nonfinite(value.digits.x)||d12_control_subnormal(value.digits.x))diagnostic|=16u;
    uint magnitude=value.digits.x&0x7fffffffu,negative=value.digits.x>>31u;
    if(value.negative>1u||value.negative!=negative||(magnitude==0u&&negative!=0u))diagnostic|=16u;return diagnostic;}
bool smo_d12_valid(SmoD12 value){return (d12_control_diagnostics(value)&65535u)==0u;}
SmoD12 d12_control_number(float value,uint diagnostic,bool nonzero){uint bits=BITS(value);
    if(d12_control_nonfinite(bits))return smo_d12_failure(diagnostic|1u);
    if(d12_control_subnormal(bits)||(nonzero&&value==0.0))return smo_d12_failure(diagnostic|2u);
    SmoD12 r=smo_d12_zero();r.digits.x=value==0.0?0u:bits;r.negative=r.digits.x>>31u;r.diagnostic=diagnostic;return r;}
SmoD12 smo_d12_uint(uint value){return d12_control_number(float(value),0u,false);}
bool d12_control_packed_valid(uint value){for(uint index=0u;index<8u;++index)if(((value>>(4u*index))&15u)>11u)return false;return true;}
uint d12_control_unpack(uint value){uint result=0u;for(uint index=0u;index<8u;++index)result=result*12u+((value>>(4u*(7u-index)))&15u);return result;}
SmoD12 smo_d12_literal(uint whole,uint fraction,uint count,uint negative){if(count>8u||negative>1u||!d12_control_packed_valid(whole)||!d12_control_packed_valid(fraction))return smo_d12_failure(16u);
    if(count<8u){if((fraction>>(4u*count))!=0u)return smo_d12_failure(16u);}
    uint denominator=1u;for(uint index=0u;index<count;++index)denominator*=12u;
    precise float fractional=float(d12_control_unpack(fraction))/float(denominator);
    precise float value=float(d12_control_unpack(whole))+fractional;if(negative!=0u)value=-value;
    return d12_control_number(value,0u,false);
}
SmoD12 smo_d12_abs(SmoD12 value){uint diagnostic=d12_control_diagnostics(value);if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);
    value.digits.x&=0x7fffffffu;value.negative=0u;return value;}
SmoD12 smo_d12_negate(SmoD12 value){uint diagnostic=d12_control_diagnostics(value);if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);
    if((value.digits.x&0x7fffffffu)!=0u){value.digits.x^=0x80000000u;value.negative^=1u;}return value;}
int smo_d12_compare(SmoD12 a,SmoD12 b){if(!smo_d12_valid(a)||!smo_d12_valid(b))return 2;
    float left=REAL(a.digits.x),right=REAL(b.digits.x);if(left<right)return -1;if(left>right)return 1;return 0;}
SmoD12 smo_d12_add(SmoD12 a,SmoD12 b){uint diagnostic=d12_control_diagnostics(a)|d12_control_diagnostics(b);if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);
    float left=REAL(a.digits.x),right=REAL(b.digits.x);precise float value=left+right;return d12_control_number(value,diagnostic,left!=-right);}
SmoD12 smo_d12_sub(SmoD12 a,SmoD12 b){uint diagnostic=d12_control_diagnostics(a)|d12_control_diagnostics(b);if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);
    float left=REAL(a.digits.x),right=REAL(b.digits.x);precise float value=left-right;return d12_control_number(value,diagnostic,left!=right);}
SmoD12 smo_d12_mul(SmoD12 a,SmoD12 b){uint diagnostic=d12_control_diagnostics(a)|d12_control_diagnostics(b);if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);
    float left=REAL(a.digits.x),right=REAL(b.digits.x);if(left==0.0||right==0.0){SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;return zero;}
    precise float magnitude=abs(left)*abs(right);bool negative=left<0.0||right<0.0;
    return d12_control_number(negative?-magnitude:magnitude,diagnostic,true);
}
SmoD12 smo_d12_div(SmoD12 a,SmoD12 b){uint diagnostic=d12_control_diagnostics(a)|d12_control_diagnostics(b);if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);
    float left=REAL(a.digits.x),right=REAL(b.digits.x);if(right==0.0){a.diagnostic=diagnostic;return a;}
    if(left==0.0){SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;return zero;}
    precise float magnitude=abs(left)/abs(right);bool negative=left<0.0||right<0.0;
    return d12_control_number(negative?-magnitude:magnitude,diagnostic,true);
}
SmoD12 d12_control_power(SmoD12 a,SmoD12 exponent,bool inverted){uint diagnostic=d12_control_diagnostics(a)|d12_control_diagnostics(exponent);
    if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);float base=REAL(a.digits.x),n=REAL(exponent.digits.x);
    if(!(base>0.0))return smo_d12_failure(diagnostic|4u);if(trunc(n)!=n||abs(n)>64.0)return smo_d12_failure(diagnostic|128u);
    uint count=uint(abs(n));if(count==0u){SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;return zero;}
    uint degree=count;if(inverted)degree=count>=2u?count-2u:1u;precise float magnitude=1.0;
    // Match the D12 scene's explicit left-fold power tree, using native FP32.
    for(uint index=0u;index<degree;++index){magnitude=magnitude*base;SmoD12 tested=d12_control_number(magnitude,diagnostic,true);if(!smo_d12_valid(tested))return tested;}
    if(inverted&&count>2u)magnitude=1.0/magnitude;
    return d12_control_number(n<0.0?-magnitude:magnitude,diagnostic,true);
}
SmoD12 smo_d12_pdir(SmoD12 a,SmoD12 exponent){return d12_control_power(a,exponent,false);}
SmoD12 smo_d12_pinv(SmoD12 a,SmoD12 exponent){return d12_control_power(a,exponent,true);}
SmoD12 smo_d12_sqrt(SmoD12 value){uint diagnostic=d12_control_diagnostics(value);if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);
    float magnitude=REAL(value.digits.x);if(magnitude<0.0)return smo_d12_failure(diagnostic|4u);precise float result=sqrt(magnitude);return d12_control_number(result,diagnostic,magnitude!=0.0);}
SmoD12 smo_d12_discriminant(SmoD12 h,SmoD12 a,SmoD12 c){uint diagnostic=d12_control_diagnostics(h)|d12_control_diagnostics(a)|d12_control_diagnostics(c);
    if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);float projection=REAL(h.digits.x),coefficient=REAL(a.digits.x),constant_term=REAL(c.digits.x);
    if(!(coefficient>0.0))return smo_d12_failure(diagnostic|4u);
    // Same intended geometric expression, with this control's own native FP32
    // rounding. It is not quantized to radix12 and is not claimed bit-identical.
    precise float squared=abs(projection)*abs(projection);precise float weighted=coefficient*constant_term;precise float value=squared-weighted;
    return d12_control_number(value,diagnostic,squared!=weighted);
}
SmoD12 smo_d12_magnitude2(SmoD12 x,SmoD12 y,SmoD12 z){uint diagnostic=d12_control_diagnostics(x)|d12_control_diagnostics(y)|d12_control_diagnostics(z);
    if((diagnostic&65535u)!=0u)return smo_d12_failure(diagnostic);float a=abs(REAL(x.digits.x)),b=abs(REAL(y.digits.x)),c=abs(REAL(z.digits.x));
    precise float squared_x=a*a;precise float squared_y=b*b;precise float squared_z=c*c;precise float xy=squared_x+squared_y;precise float total=xy+squared_z;
    return d12_control_number(total,diagnostic,a!=0.0||b!=0.0||c!=0.0);
}
SmoD12 smo_d12_norm3(SmoD12 x,SmoD12 y,SmoD12 z){SmoD12 squared=smo_d12_magnitude2(x,y,z);return smo_d12_sqrt(squared);}
SmoD12 smo_d12_min(SmoD12 a,SmoD12 b){int order=smo_d12_compare(a,b);if(order==2)return smo_d12_failure(d12_control_diagnostics(a)|d12_control_diagnostics(b));
    SmoD12 result=a;if(order>0)result=b;result.diagnostic|=a.diagnostic|b.diagnostic;return result;}
SmoD12 smo_d12_max(SmoD12 a,SmoD12 b){int order=smo_d12_compare(a,b);if(order==2)return smo_d12_failure(d12_control_diagnostics(a)|d12_control_diagnostics(b));
    SmoD12 result=a;if(order<0)result=b;result.diagnostic|=a.diagnostic|b.diagnostic;return result;}
SmoD12 smo_d12_clamp(SmoD12 value,SmoD12 low,SmoD12 high){int order=smo_d12_compare(low,high);
    if(order==2||order>0)return smo_d12_failure(d12_control_diagnostics(value)|d12_control_diagnostics(low)|d12_control_diagnostics(high)|4u);
    return smo_d12_min(smo_d12_max(value,low),high);}
uint smo_d12_round_uint(SmoD12 value,inout uint diagnostic){diagnostic|=d12_control_diagnostics(value);if(!smo_d12_valid(value))return 0u;
    float number=REAL(value.digits.x);if(number<0.0||number>255.0){diagnostic|=4u;return 0u;}
    uint whole=uint(floor(number));precise float fraction=number-float(whole);
    if(fraction>0.5||(fraction==0.5&&(whole&1u)!=0u))++whole;return min(whole,255u);
}
SmoD12 smo_d12_load(uint storage,uint offset){SmoD12 value;value.digits=uint4(word(storage,offset),word(storage,offset+1u),word(storage,offset+2u),word(storage,offset+3u));
    value.negative=word(storage,offset+4u);value.diagnostic=word(storage,offset+5u);return value;}
void smo_d12_store(uint storage,uint offset,SmoD12 value){put(storage,offset,value.digits.x);put(storage,offset+1u,value.digits.y);put(storage,offset+2u,value.digits.z);
    put(storage,offset+3u,value.digits.w);put(storage,offset+4u,value.negative);put(storage,offset+5u,value.diagnostic);}
