// D12 software arithmetic: FOUR radix12^4 limbs, two fractional limbs.
// Requires uint4 (uvec4 in GLSL), word(buffer,offset), put(buffer,offset,value).
// No float, double, conversion to FP, transcendental or host arithmetic.
struct SmoD12 {uint4 digits;uint negative;uint diagnostic;};
struct SmoD12Result {SmoD12 primary;SmoD12 alternative;uint flags;};
struct SmoD12Wide {uint4 low;uint4 high;};
// HLSL globals are uniform/external by default, even with const: without
// static these values require a $Globals cbuffer instead of being literals.
// GLSL globals already give const the intended compile-time semantics.
#ifdef SMO_HLSL
static
#endif
const uint D12_R=20736u,D12_SCALE=429981696u,D12_FATAL=65535u,D12_INEXACT=65536u;
// Keep multiword arithmetic as real loops in DXBC instead of multiplying
// nested copies during O3 inlining. This changes no arithmetic/iteration order.
// GLSL retains its existing compiler decisions and receives no loop attribute.
#ifdef SMO_HLSL
#define D12_LOOP [loop]
#else
#define D12_LOOP
#endif
SmoD12 smo_d12_zero(){SmoD12 a;a.digits=uint4(0u,0u,0u,0u);a.negative=0u;a.diagnostic=0u;return a;}
SmoD12 smo_d12_failure(uint diagnostic){SmoD12 a=smo_d12_zero();a.diagnostic=diagnostic;return a;}
bool smo_d12_empty(SmoD12 a){return (a.digits.x|a.digits.y|a.digits.z|a.digits.w)==0u;}
bool smo_d12_valid(SmoD12 a){if((a.diagnostic&D12_FATAL)!=0u||a.negative>1u)return false;
    for(uint i=0u;i<4u;++i)if(a.digits[i]>=D12_R)return false;return !smo_d12_empty(a)||a.negative==0u;}
uint smo_d12_diagnostics(SmoD12 a){uint diagnostic=a.diagnostic;if(a.negative>1u||(smo_d12_empty(a)&&a.negative!=0u))diagnostic|=16u;
    for(uint i=0u;i<4u;++i)if(a.digits[i]>=D12_R)diagnostic|=16u;return diagnostic;}
SmoD12 smo_d12_normalize(SmoD12 a){if(smo_d12_empty(a))a.negative=0u;return a;}
// Explicit component access avoids DXBC's restriction on a dynamically indexed
// array member used as an l-value through an inout struct parameter. The eight
// mathematical limbs and their radix/order are unchanged.
uint smo_d12_wget(SmoD12Wide a,uint index){if(index==0u)return a.low.x;if(index==1u)return a.low.y;
    if(index==2u)return a.low.z;if(index==3u)return a.low.w;if(index==4u)return a.high.x;
    if(index==5u)return a.high.y;if(index==6u)return a.high.z;return a.high.w;}
void smo_d12_wset(inout SmoD12Wide a,uint index,uint value){if(index==0u)a.low.x=value;else if(index==1u)a.low.y=value;
    else if(index==2u)a.low.z=value;else if(index==3u)a.low.w=value;else if(index==4u)a.high.x=value;
    else if(index==5u)a.high.y=value;else if(index==6u)a.high.z=value;else a.high.w=value;}
SmoD12Wide smo_d12_wide_zero(){SmoD12Wide a;a.low=uint4(0u,0u,0u,0u);a.high=uint4(0u,0u,0u,0u);return a;}
SmoD12Wide smo_d12_wide(SmoD12 a){SmoD12Wide r=smo_d12_wide_zero();r.low=a.digits;return r;}
int smo_d12_wcompare(SmoD12Wide a,SmoD12Wide b){D12_LOOP for(uint step=0u;step<8u;++step){uint index=7u-step;
    uint left=smo_d12_wget(a,index),right=smo_d12_wget(b,index);if(left<right)return -1;if(left>right)return 1;}return 0;}
SmoD12Wide smo_d12_wsubtract(SmoD12Wide a,SmoD12Wide b){uint borrow=0u;D12_LOOP for(uint index=0u;index<8u;++index){
    uint left=smo_d12_wget(a,index),amount=smo_d12_wget(b,index)+borrow;
    if(left<amount){smo_d12_wset(a,index,left+D12_R-amount);borrow=1u;}else{smo_d12_wset(a,index,left-amount);borrow=0u;}}return a;}
SmoD12Wide smo_d12_wadd(SmoD12Wide a,SmoD12Wide b,out uint overflow){uint carry=0u;
    D12_LOOP for(uint index=0u;index<8u;++index){uint sum=smo_d12_wget(a,index)+smo_d12_wget(b,index)+carry;
        smo_d12_wset(a,index,sum%D12_R);carry=sum/D12_R;}overflow=carry;return a;}
SmoD12Wide smo_d12_wscale(SmoD12Wide a,uint factor){uint carry=0u;D12_LOOP for(uint index=0u;index<8u;++index){
    uint product=smo_d12_wget(a,index)*factor+carry;smo_d12_wset(a,index,product%D12_R);carry=product/D12_R;}return a;}
SmoD12Wide smo_d12_wsmall(SmoD12Wide a,uint value){uint carry=value;D12_LOOP for(uint index=0u;index<8u&&carry!=0u;++index){
    uint sum=smo_d12_wget(a,index)+carry;smo_d12_wset(a,index,sum%D12_R);carry=sum/D12_R;}return a;}
SmoD12 smo_d12_from_wide(SmoD12Wide a,uint negative,uint diagnostic){if((a.high.x|a.high.y|a.high.z|a.high.w)!=0u)return smo_d12_failure(diagnostic|1u);
    SmoD12 r=smo_d12_zero();r.digits=a.low;r.negative=negative;r.diagnostic=diagnostic;return smo_d12_normalize(r);}
SmoD12 smo_d12_increment(SmoD12 a){uint carry=1u;for(uint i=0u;i<4u&&carry!=0u;++i){uint sum=a.digits[i]+carry;a.digits[i]=sum%D12_R;carry=sum/D12_R;}
    if(carry!=0u)return smo_d12_failure(a.diagnostic|1u);return a;}
SmoD12 smo_d12_uint(uint value){if(value>=D12_SCALE)return smo_d12_failure(1u);SmoD12 r=smo_d12_zero();r.digits.z=value%D12_R;r.digits.w=value/D12_R;return r;}
bool smo_d12_packed_valid(uint a){for(uint i=0u;i<8u;++i)if(((a>>(4u*i))&15u)>11u)return false;return true;}
uint smo_d12_unpack(uint a){uint r=0u;for(uint i=0u;i<8u;++i)r=r*12u+((a>>(4u*(7u-i)))&15u);return r;}
SmoD12 smo_d12_literal(uint whole,uint fraction,uint count,uint negative){
    if(count>8u||negative>1u||!smo_d12_packed_valid(whole)||!smo_d12_packed_valid(fraction))return smo_d12_failure(16u);
    if(count<8u){if((fraction>>(4u*count))!=0u)return smo_d12_failure(16u);}
    SmoD12 r=smo_d12_uint(smo_d12_unpack(whole));uint raw=smo_d12_unpack(fraction);
    for(uint i=count;i<8u;++i)raw*=12u;r.digits.x=raw%D12_R;r.digits.y=raw/D12_R;r.negative=negative;return smo_d12_normalize(r);
}
SmoD12 smo_d12_abs(SmoD12 a){uint diagnostic=smo_d12_diagnostics(a);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);a.negative=0u;return a;}
SmoD12 smo_d12_negate(SmoD12 a){uint diagnostic=smo_d12_diagnostics(a);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    if(!smo_d12_empty(a))a.negative=1u-a.negative;return a;}
int smo_d12_compare(SmoD12 a,SmoD12 b){if(!smo_d12_valid(a)||!smo_d12_valid(b))return 2;
    if(a.negative!=b.negative)return a.negative!=0u?-1:1;int order=smo_d12_wcompare(smo_d12_wide(a),smo_d12_wide(b));return a.negative!=0u?-order:order;}
SmoD12 smo_d12_add(SmoD12 a,SmoD12 b){uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    if(a.negative==b.negative){SmoD12 r=smo_d12_zero();r.negative=a.negative;r.diagnostic=diagnostic;uint carry=0u;
        for(uint i=0u;i<4u;++i){uint sum=a.digits[i]+b.digits[i]+carry;r.digits[i]=sum%D12_R;carry=sum/D12_R;}
        if(carry!=0u)return smo_d12_failure(diagnostic|1u);return smo_d12_normalize(r);}
    int order=smo_d12_wcompare(smo_d12_wide(a),smo_d12_wide(b));SmoD12Wide large=smo_d12_wide(a),small=smo_d12_wide(b);uint negative=a.negative;
    if(order<0){large=smo_d12_wide(b);small=smo_d12_wide(a);negative=b.negative;}
    return smo_d12_from_wide(smo_d12_wsubtract(large,small),negative,diagnostic);
}
SmoD12 smo_d12_sub(SmoD12 a,SmoD12 b){return smo_d12_add(a,smo_d12_negate(b));}
SmoD12Wide smo_d12_product_wide(SmoD12 a,SmoD12 b){SmoD12Wide product=smo_d12_wide_zero();
    D12_LOOP for(uint left_index=0u;left_index<4u;++left_index){uint carry=0u;D12_LOOP for(uint right_index=0u;right_index<4u;++right_index){
        uint at=left_index+right_index,product_value=a.digits[left_index]*b.digits[right_index]+smo_d12_wget(product,at)+carry;
        smo_d12_wset(product,at,product_value%D12_R);carry=product_value/D12_R;}
        D12_LOOP for(uint carry_index=left_index+4u;carry_index<8u&&carry!=0u;++carry_index){uint carry_value=smo_d12_wget(product,carry_index)+carry;
            smo_d12_wset(product,carry_index,carry_value%D12_R);carry=carry_value/D12_R;}}return product;
}
SmoD12 smo_d12_quantize_product(SmoD12Wide product,uint negative,uint diagnostic){
    if(product.high.z!=0u||product.high.w!=0u)return smo_d12_failure(diagnostic|1u);
    SmoD12 r=smo_d12_zero();r.negative=negative;r.diagnostic=diagnostic;r.digits=uint4(product.low.z,product.low.w,product.high.x,product.high.y);
    uint remainder=product.low.x+product.low.y*D12_R;if(remainder!=0u)r.diagnostic|=D12_INEXACT;
    uint twice=remainder*2u;if(twice>D12_SCALE||(twice==D12_SCALE&&(r.digits.x&1u)!=0u))r=smo_d12_increment(r);
    bool nonzero=(product.low.x|product.low.y|product.low.z|product.low.w|product.high.x|product.high.y|product.high.z|product.high.w)!=0u;
    if((r.diagnostic&D12_FATAL)==0u&&smo_d12_empty(r)&&nonzero)return smo_d12_failure(r.diagnostic|2u);
    return smo_d12_normalize(r);
}
SmoD12 smo_d12_magnitude_product(SmoD12 a,SmoD12 b,uint negative){return smo_d12_quantize_product(smo_d12_product_wide(a,b),negative,a.diagnostic|b.diagnostic);}
SmoD12 smo_d12_discriminant(SmoD12 h,SmoD12 a,SmoD12 c){uint diagnostic=smo_d12_diagnostics(h)|smo_d12_diagnostics(a)|smo_d12_diagnostics(c);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    if(a.negative!=0u||smo_d12_empty(a))return smo_d12_failure(diagnostic|4u);
    SmoD12Wide squared=smo_d12_product_wide(h,h),weighted=smo_d12_product_wide(a,c),magnitude=smo_d12_wide_zero();uint negative=0u;
    if(c.negative!=0u){uint overflow=0u;magnitude=smo_d12_wadd(squared,weighted,overflow);if(overflow!=0u)return smo_d12_failure(diagnostic|1u);}
    else if(smo_d12_wcompare(squared,weighted)>=0)magnitude=smo_d12_wsubtract(squared,weighted);
    else{magnitude=smo_d12_wsubtract(weighted,squared);negative=1u;}
    // Both products stay at scale12^16; only their signed net is rounded.
    // An exact cancellation is0; a nonzero net below half a quantum remains
    // a visible UNDERFLOW, never silently accepted as an exact zero.
    return smo_d12_quantize_product(magnitude,negative,diagnostic);
}
SmoD12 smo_d12_magnitude_divide(SmoD12 a,SmoD12 b,uint negative){uint diagnostic=a.diagnostic|b.diagnostic;
    SmoD12Wide dividend=smo_d12_wide_zero(),divisor=smo_d12_wide(b),remainder=smo_d12_wide_zero(),quotient=smo_d12_wide_zero();
    dividend.low=uint4(0u,0u,a.digits.x,a.digits.y);dividend.high=uint4(a.digits.z,a.digits.w,0u,0u);
    D12_LOOP for(uint step=0u;step<8u;++step){uint at=7u-step;
        D12_LOOP for(uint shift_index=7u;shift_index>0u;--shift_index)smo_d12_wset(remainder,shift_index,smo_d12_wget(remainder,shift_index-1u));
        smo_d12_wset(remainder,0u,smo_d12_wget(dividend,at));
        uint low=0u,high=D12_R-1u,best=0u;
        D12_LOOP for(uint search=0u;search<15u&&low<=high;++search){uint middle=(low+high)/2u;SmoD12Wide trial=smo_d12_wscale(divisor,middle);
            if(smo_d12_wcompare(trial,remainder)<=0){best=middle;low=middle+1u;}else high=middle-1u;}
        smo_d12_wset(quotient,at,best);remainder=smo_d12_wsubtract(remainder,smo_d12_wscale(divisor,best));
    }
    SmoD12 r=smo_d12_from_wide(quotient,negative,diagnostic);if((r.diagnostic&D12_FATAL)!=0u)return r;
    SmoD12Wide zero=smo_d12_wide_zero();if(smo_d12_wcompare(remainder,zero)!=0)r.diagnostic|=D12_INEXACT;
    int rounding=smo_d12_wcompare(smo_d12_wscale(remainder,2u),divisor);
    if(rounding>0||(rounding==0&&(r.digits.x&1u)!=0u))r=smo_d12_increment(r);
    if((r.diagnostic&D12_FATAL)==0u&&smo_d12_empty(r)&&!smo_d12_empty(a))return smo_d12_failure(r.diagnostic|2u);
    return smo_d12_normalize(r);
}
uint smo_d12_wdigit(SmoD12Wide a,uint at){uint divisor=1u;for(uint digit_index=0u;digit_index<at%4u;++digit_index)divisor*=12u;return (smo_d12_wget(a,at/4u)/divisor)%12u;}
SmoD12 smo_d12_sqrt_wide(SmoD12Wide radicand,uint diagnostic,uint pair_count){SmoD12Wide remainder=smo_d12_wide_zero(),root=smo_d12_wide_zero();
    // Literal call-site bounds: scalar sqrt consumes twelve pairs as before;
    // the extended vector norm needs all sixteen pairs of its eight limbs.
    D12_LOOP for(uint step=0u;step<pair_count;++step){uint pair_at=(pair_count-1u-step)*2u;
        remainder=smo_d12_wsmall(smo_d12_wscale(remainder,144u),smo_d12_wdigit(radicand,pair_at+1u)*12u+smo_d12_wdigit(radicand,pair_at));
        uint chosen=0u;SmoD12Wide selected=smo_d12_wide_zero();
        D12_LOOP for(uint attempt=0u;attempt<12u;++attempt){uint digit=11u-attempt;
            SmoD12Wide trial=smo_d12_wscale(smo_d12_wsmall(smo_d12_wscale(root,24u),digit),digit);
            if(smo_d12_wcompare(trial,remainder)<=0){chosen=digit;selected=trial;break;}}
        remainder=smo_d12_wsubtract(remainder,selected);root=smo_d12_wsmall(smo_d12_wscale(root,12u),chosen);
    }
    SmoD12 r=smo_d12_from_wide(root,0u,diagnostic);SmoD12Wide zero=smo_d12_wide_zero();if(smo_d12_wcompare(remainder,zero)!=0)r.diagnostic|=D12_INEXACT;
    // For integer N, sqrt(N) rounds upward iff N-floor(sqrt(N))^2>floor(sqrt(N)).
    if(smo_d12_wcompare(remainder,root)>0)r=smo_d12_increment(r);return r;
}
SmoD12 smo_d12_sqrt(SmoD12 a){uint diagnostic=smo_d12_diagnostics(a);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    if(a.negative!=0u)return smo_d12_failure(diagnostic|4u);
    SmoD12Wide radicand=smo_d12_wide_zero();radicand.low=uint4(0u,0u,a.digits.x,a.digits.y);radicand.high=uint4(a.digits.z,a.digits.w,0u,0u);
    return smo_d12_sqrt_wide(radicand,diagnostic,12u);
}
SmoD12Wide smo_d12_squares_wide(SmoD12 x,SmoD12 y,SmoD12 z,out uint diagnostic){diagnostic=smo_d12_diagnostics(x)|smo_d12_diagnostics(y)|smo_d12_diagnostics(z);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_wide_zero();
    uint first_overflow=0u;SmoD12Wide sum=smo_d12_wadd(smo_d12_product_wide(x,x),smo_d12_product_wide(y,y),first_overflow);
    if(first_overflow!=0u){diagnostic|=1u;return smo_d12_wide_zero();}
    uint second_overflow=0u;sum=smo_d12_wadd(sum,smo_d12_product_wide(z,z),second_overflow);
    if(second_overflow!=0u){diagnostic|=1u;return smo_d12_wide_zero();}return sum;
}
SmoD12 smo_d12_magnitude2(SmoD12 x,SmoD12 y,SmoD12 z){uint diagnostic=0u;SmoD12Wide sum=smo_d12_squares_wide(x,y,z,diagnostic);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);return smo_d12_quantize_product(sum,0u,diagnostic);
}
SmoD12 smo_d12_norm3(SmoD12 x,SmoD12 y,SmoD12 z){uint diagnostic=0u;SmoD12Wide sum=smo_d12_squares_wide(x,y,z,diagnostic);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);return smo_d12_sqrt_wide(sum,diagnostic,16u);
}
SmoD12 smo_d12_power_positive(SmoD12 base,uint count){SmoD12 r=smo_d12_uint(1u);r.diagnostic=base.diagnostic;
    // The fixed-radix definition explicitly uses this left-fold tree. Changing
    // its association would change quantization and must be another experiment.
    D12_LOOP for(uint i=0u;i<count;++i){r=smo_d12_magnitude_product(r,base,0u);if(!smo_d12_valid(r))return r;}return r;}
SmoD12 smo_d12_power_primary(SmoD12 a,SmoD12 exponent,uint profile,bool inverted){
    if((profile&~63u)!=0u||(profile&12u)==12u)return smo_d12_failure(64u);
    uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(exponent);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    a.diagnostic=diagnostic;exponent.diagnostic=diagnostic;
    if(a.negative!=0u||smo_d12_empty(a))return smo_d12_failure(diagnostic|4u);
    if(exponent.digits.x!=0u||exponent.digits.y!=0u||exponent.digits.w!=0u||exponent.digits.z>64u)return smo_d12_failure(diagnostic|128u);
    uint count=exponent.digits.z;
    if(count==0u){if((profile&16u)!=0u)return a;SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;return zero;}
    if(count==1u&&(profile&32u)!=0u){SmoD12 one=smo_d12_uint(1u);one.negative=exponent.negative;one.diagnostic=diagnostic;return one;}
    uint degree=count;if(inverted)degree=count>=2u?count-2u:1u;
    SmoD12 magnitude=smo_d12_power_positive(a,degree);if(!smo_d12_valid(magnitude))return magnitude;
    if(inverted&&count>2u)magnitude=smo_d12_magnitude_divide(smo_d12_uint(1u),magnitude,0u);
    if(!smo_d12_empty(magnitude))magnitude.negative=exponent.negative;return magnitude;
}
SmoD12Result smo_d12_evaluate(uint operation,SmoD12 a,SmoD12 b,uint profile){SmoD12Result r;r.primary=smo_d12_failure(32u);r.alternative=smo_d12_failure(4u);r.flags=profile!=0u?128u:0u;
    if((profile&~63u)!=0u||(profile&12u)==12u){r.primary=smo_d12_failure(64u);return r;}
    uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);if((diagnostic&D12_FATAL)!=0u){r.primary=smo_d12_failure(diagnostic);return r;}
    a.diagnostic=diagnostic;b.diagnostic=diagnostic;
    // Conformance may specialize one operation at shader compilation, while
    // retaining every input/profile case in a separate dispatch. A normal
    // scene build defines no filter and keeps the complete rule evaluator.
    switch(operation){
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 0
    case 0u:{r.primary=smo_d12_add(a,b);break;}
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 1
    case 1u:{r.primary=smo_d12_sub(a,b);break;}
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 2
    case 2u:{if(smo_d12_empty(a)||smo_d12_empty(b)){SmoD12 other=a;if(smo_d12_empty(a))other=b;SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;
            if((profile&2u)!=0u){r.primary=other;r.alternative=zero;}else{r.primary=zero;r.alternative=other;}r.flags|=2u;
        }else{uint negative=(profile&1u)!=0u?(a.negative^b.negative):(a.negative|b.negative);r.primary=smo_d12_magnitude_product(a,b,negative);}break;}
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 3
    case 3u:{if(smo_d12_empty(b)){uint policy=profile&12u;SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;
            if(policy==8u){r.primary=smo_d12_failure(diagnostic|8u);r.flags|=4u;return r;}
            if(policy==4u){r.primary=zero;r.alternative=a;}else{r.primary=a;r.alternative=zero;}r.flags|=2u;
        }else if(smo_d12_empty(a)){r.primary=smo_d12_zero();r.primary.diagnostic=diagnostic;}
        else{uint negative=(profile&1u)!=0u?(a.negative^b.negative):(a.negative|b.negative);r.primary=smo_d12_magnitude_divide(a,b,negative);}break;}
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 4 || D12_ONLY_OPERATION == 5
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 4
    case 4u:
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 5
    case 5u:
#endif
    {
#if defined(D12_ONLY_OPERATION)
        r.primary=smo_d12_power_primary(a,b,profile,D12_ONLY_OPERATION==5);
#else
        r.primary=smo_d12_power_primary(a,b,profile,operation==5u);
#endif
        if(!smo_d12_valid(r.primary))return r;
        if(b.digits.z==0u){SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;
            if((profile&16u)!=0u)r.alternative=zero;else r.alternative=a;r.flags|=2u;}
        break;
    }
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 9
    case 9u:{r.primary=smo_d12_sqrt(a);break;}
#endif
    default:break;
    }
    if(smo_d12_valid(r.primary))r.flags|=1u;return r;
}
// Statically selected scene operations call only their own primitives. Routing
// each nested vector product through the general rule dispatcher makes shader
// inlining expand unrelated division, powers and square-root implementations.
SmoD12 smo_d12_mul(SmoD12 a,SmoD12 b){uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    if(smo_d12_empty(a)||smo_d12_empty(b)){SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;return zero;}
    a.diagnostic=diagnostic;b.diagnostic=diagnostic;return smo_d12_magnitude_product(a,b,a.negative|b.negative);}
SmoD12 smo_d12_div(SmoD12 a,SmoD12 b){uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    a.diagnostic=diagnostic;b.diagnostic=diagnostic;if(smo_d12_empty(b))return a;
    if(smo_d12_empty(a)){SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;return zero;}
    return smo_d12_magnitude_divide(a,b,a.negative|b.negative);}
SmoD12 smo_d12_pdir(SmoD12 a,SmoD12 exponent){return smo_d12_power_primary(a,exponent,0u,false);}
SmoD12 smo_d12_pinv(SmoD12 a,SmoD12 exponent){return smo_d12_power_primary(a,exponent,0u,true);}
SmoD12 smo_d12_min(SmoD12 a,SmoD12 b){int order=smo_d12_compare(a,b);if(order==2)return smo_d12_failure(smo_d12_diagnostics(a)|smo_d12_diagnostics(b));
    SmoD12 r=a;if(order>0)r=b;r.diagnostic|=a.diagnostic|b.diagnostic;return r;}
SmoD12 smo_d12_max(SmoD12 a,SmoD12 b){int order=smo_d12_compare(a,b);if(order==2)return smo_d12_failure(smo_d12_diagnostics(a)|smo_d12_diagnostics(b));
    SmoD12 r=a;if(order<0)r=b;r.diagnostic|=a.diagnostic|b.diagnostic;return r;}
SmoD12 smo_d12_clamp(SmoD12 a,SmoD12 low,SmoD12 high){int order=smo_d12_compare(low,high);if(order==2||order>0)return smo_d12_failure(smo_d12_diagnostics(a)|smo_d12_diagnostics(low)|smo_d12_diagnostics(high)|4u);
    return smo_d12_min(smo_d12_max(a,low),high);}
// Final display boundary only. Keep the original channel's ties-to-even rule
// without adding an INEXACT warning for conversion to an 8-bit display value.
uint smo_d12_round_uint(SmoD12 value,inout uint diagnostic){diagnostic|=smo_d12_diagnostics(value);if(!smo_d12_valid(value))return 0u;
    if(value.negative!=0u||value.digits.w!=0u||value.digits.z>255u||(value.digits.z==255u&&(value.digits.x|value.digits.y)!=0u)){
        diagnostic|=4u;return 0u;}
    uint whole=value.digits.z;
    if(value.digits.y>10368u||(value.digits.y==10368u&&(value.digits.x!=0u||(whole&1u)!=0u)))++whole;
    return min(whole,255u);
}
SmoD12 smo_d12_load(uint storage,uint offset){SmoD12 a;a.digits=uint4(word(storage,offset),word(storage,offset+1u),word(storage,offset+2u),word(storage,offset+3u));
    a.negative=word(storage,offset+4u);a.diagnostic=word(storage,offset+5u);return a;}
void smo_d12_store(uint storage,uint offset,SmoD12 a){for(uint i=0u;i<4u;++i)put(storage,offset+i,a.digits[i]);put(storage,offset+4u,a.negative);put(storage,offset+5u,a.diagnostic);}
