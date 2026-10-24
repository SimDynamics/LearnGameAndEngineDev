// F12: twelve significant radix12 digits and a variable radix12 exponent.
// Binary uint instructions implement every quantity; there are NO FP types,
// loads, conversions or arithmetic. Three radix12^4 coefficient limbs use
// xyz; w is the VERSIONED exponent tag, not a fourth coefficient limb.
#define SMO_FLOAT12 1
struct SmoD12 {uint4 digits;uint negative;uint diagnostic;};
struct SmoD12Result {SmoD12 primary;SmoD12 alternative;uint flags;};
struct F12Wide {uint4 low;uint4 high;};
struct F12Number {uint4 coefficient;int exponent;uint negative;uint diagnostic;};
#ifdef SMO_HLSL
static
#endif
const uint F12_R=20736u,F12_MIN_HIGH=1728u,F12_TAG=0xF1200000u,F12_TAG_MASK=0xfffffe00u,
    F12_ZERO_TAG=0xF12000c0u,D12_FATAL=65535u,D12_INEXACT=65536u;
#ifdef SMO_HLSL
#define F12_LOOP [loop]
#else
#define F12_LOOP
#endif

uint f12_get(F12Wide a,uint i){uint result=a.high.w;if(i==0u)result=a.low.x;else if(i==1u)result=a.low.y;else if(i==2u)result=a.low.z;else if(i==3u)result=a.low.w;
    else if(i==4u)result=a.high.x;else if(i==5u)result=a.high.y;else if(i==6u)result=a.high.z;return result;}
void f12_set(inout F12Wide a,uint i,uint n){if(i==0u)a.low.x=n;else if(i==1u)a.low.y=n;else if(i==2u)a.low.z=n;else if(i==3u)a.low.w=n;
    else if(i==4u)a.high.x=n;else if(i==5u)a.high.y=n;else if(i==6u)a.high.z=n;else a.high.w=n;}
F12Wide f12_zero_wide(){F12Wide a;a.low=uint4(0u,0u,0u,0u);a.high=uint4(0u,0u,0u,0u);return a;}
bool f12_empty_wide(F12Wide a){return (a.low.x|a.low.y|a.low.z|a.low.w|a.high.x|a.high.y|a.high.z|a.high.w)==0u;}
F12Wide f12_small(uint n){F12Wide a=f12_zero_wide();a.low=uint4(n%F12_R,(n/F12_R)%F12_R,n/(F12_R*F12_R),0u);return a;}
F12Wide f12_coefficient(uint4 n){F12Wide a=f12_zero_wide();a.low=uint4(n.x,n.y,n.z,0u);return a;}
int f12_compare_wide(F12Wide a,F12Wide b){F12_LOOP for(uint step=0u;step<8u;++step){uint i=7u-step,l=f12_get(a,i),r=f12_get(b,i);if(l<r)return -1;if(l>r)return 1;}return 0;}
F12Wide f12_add_wide(F12Wide a,F12Wide b){uint carry=0u;F12_LOOP for(uint i=0u;i<8u;++i){uint n=f12_get(a,i)+f12_get(b,i)+carry;f12_set(a,i,n%F12_R);carry=n/F12_R;}return a;}
F12Wide f12_sub_wide(F12Wide a,F12Wide b){uint borrow=0u;F12_LOOP for(uint i=0u;i<8u;++i){uint l=f12_get(a,i),r=f12_get(b,i)+borrow;
    if(l<r){f12_set(a,i,l+F12_R-r);borrow=1u;}else{f12_set(a,i,l-r);borrow=0u;}}return a;}
F12Wide f12_scale(F12Wide a,uint factor){uint carry=0u;F12_LOOP for(uint i=0u;i<8u;++i){uint n=f12_get(a,i)*factor+carry;f12_set(a,i,n%F12_R);carry=n/F12_R;}return a;}
F12Wide f12_add_small(F12Wide a,uint n){uint carry=n;F12_LOOP for(uint i=0u;i<8u&&carry!=0u;++i){uint sum=f12_get(a,i)+carry;f12_set(a,i,sum%F12_R);carry=sum/F12_R;}return a;}
F12Wide f12_div_small(F12Wide a,uint divisor,out uint remainder){remainder=0u;F12_LOOP for(uint step=0u;step<8u;++step){uint i=7u-step,n=remainder*F12_R+f12_get(a,i);f12_set(a,i,n/divisor);remainder=n%divisor;}return a;}
uint f12_partial_power(uint digits){uint result=1728u;if(digits==0u)result=1u;else if(digits==1u)result=12u;else if(digits==2u)result=144u;return result;}
// Whole radix20736 words are pure vector permutations. The residual scale is
// a divisor of the radix, so each limb's incoming carry cannot overflow its
// residual product. This is exactly the repeated *12 integer shift, without
// nested full-Wide loops at every normalization call site.
F12Wide f12_word_left(F12Wide a,uint words){F12Wide r=f12_zero_wide();
    if(words==0u)r=a;
    else if(words==1u){r.low=uint4(0u,a.low.x,a.low.y,a.low.z);r.high=uint4(a.low.w,a.high.x,a.high.y,a.high.z);}
    else if(words==2u){r.low=uint4(0u,0u,a.low.x,a.low.y);r.high=uint4(a.low.z,a.low.w,a.high.x,a.high.y);}
    else if(words==3u){r.low=uint4(0u,0u,0u,a.low.x);r.high=uint4(a.low.y,a.low.z,a.low.w,a.high.x);}
    else if(words==4u)r.high=a.low;
    else if(words==5u)r.high=uint4(0u,a.low.x,a.low.y,a.low.z);
    else if(words==6u)r.high=uint4(0u,0u,a.low.x,a.low.y);
    else if(words==7u)r.high=uint4(0u,0u,0u,a.low.x);return r;}
F12Wide f12_word_right(F12Wide a,uint words){F12Wide r=f12_zero_wide();
    if(words==0u)r=a;
    else if(words==1u){r.low=uint4(a.low.y,a.low.z,a.low.w,a.high.x);r.high=uint4(a.high.y,a.high.z,a.high.w,0u);}
    else if(words==2u){r.low=uint4(a.low.z,a.low.w,a.high.x,a.high.y);r.high=uint4(a.high.z,a.high.w,0u,0u);}
    else if(words==3u){r.low=uint4(a.low.w,a.high.x,a.high.y,a.high.z);r.high=uint4(a.high.w,0u,0u,0u);}
    else if(words==4u)r.low=a.high;
    else if(words==5u)r.low=uint4(a.high.y,a.high.z,a.high.w,0u);
    else if(words==6u)r.low=uint4(a.high.z,a.high.w,0u,0u);
    else if(words==7u)r.low=uint4(a.high.w,0u,0u,0u);return r;}
F12Wide f12_shift(F12Wide a,uint digits){a=f12_word_left(a,digits/4u);uint factor=f12_partial_power(digits%4u);F12Wide r=a;
    if(factor!=1u){uint4 low=a.low*factor,high=a.high*factor;
        r.low=low%F12_R+uint4(0u,low.x/F12_R,low.y/F12_R,low.z/F12_R);
        r.high=high%F12_R+uint4(low.w/F12_R,high.x/F12_R,high.y/F12_R,high.z/F12_R);}return r;}
F12Wide f12_right_shift(F12Wide a,uint digits){a=f12_word_right(a,digits/4u);uint factor=f12_partial_power(digits%4u);F12Wide r=a;
    if(factor!=1u){uint carry_scale=F12_R/factor;
        r.low=a.low/factor+uint4(a.low.y%factor,a.low.z%factor,a.low.w%factor,a.high.x%factor)*carry_scale;
        r.high=a.high/factor+uint4(a.high.y%factor,a.high.z%factor,a.high.w%factor,0u)*carry_scale;}return r;}
uint f12_limb_digits(uint n){uint result=0u;if(n>=1728u)result=4u;else if(n>=144u)result=3u;else if(n>=12u)result=2u;else if(n!=0u)result=1u;return result;}
uint f12_digit_count(F12Wide a){uint offset=0u,limb=a.low.x;
    if(a.high.w!=0u){offset=28u;limb=a.high.w;}else if(a.high.z!=0u){offset=24u;limb=a.high.z;}
    else if(a.high.y!=0u){offset=20u;limb=a.high.y;}else if(a.high.x!=0u){offset=16u;limb=a.high.x;}
    else if(a.low.w!=0u){offset=12u;limb=a.low.w;}else if(a.low.z!=0u){offset=8u;limb=a.low.z;}
    else if(a.low.y!=0u){offset=4u;limb=a.low.y;}return offset+f12_limb_digits(limb);}
uint f12_digit(F12Wide a,uint at){uint divisor=1u;for(uint i=0u;i<at%4u;++i)divisor*=12u;return (f12_get(a,at/4u)/divisor)%12u;}

SmoD12 smo_d12_zero(){SmoD12 a;a.digits=uint4(0u,0u,0u,F12_ZERO_TAG);a.negative=0u;a.diagnostic=0u;return a;}
SmoD12 smo_d12_failure(uint diagnostic){SmoD12 a=smo_d12_zero();a.diagnostic=diagnostic;return a;}
bool smo_d12_empty(SmoD12 a){return (a.digits.x|a.digits.y|a.digits.z)==0u;}
uint smo_d12_diagnostics(SmoD12 a){uint diagnostic=a.diagnostic;
    if(a.negative>1u||(a.digits.w&F12_TAG_MASK)!=F12_TAG||(a.digits.w&511u)>384u||a.digits.x>=F12_R||a.digits.y>=F12_R||a.digits.z>=F12_R)diagnostic|=16u;
    if((diagnostic&D12_FATAL)==0u){if(smo_d12_empty(a)){if(a.negative!=0u||a.digits.w!=F12_ZERO_TAG)diagnostic|=16u;}
        else if(a.digits.z<F12_MIN_HIGH)diagnostic|=16u;}return diagnostic;}
bool smo_d12_valid(SmoD12 a){return (smo_d12_diagnostics(a)&D12_FATAL)==0u;}
F12Number f12_unpack(SmoD12 a){F12Number n;n.coefficient=uint4(a.digits.x,a.digits.y,a.digits.z,0u);n.exponent=int(a.digits.w&511u)-192;n.negative=a.negative;n.diagnostic=a.diagnostic;return n;}
bool f12_number_empty(F12Number n){return (n.coefficient.x|n.coefficient.y|n.coefficient.z)==0u;}
F12Number f12_normalize(F12Wide coefficient,int exponent,uint negative,uint diagnostic){F12Number n;n.coefficient=uint4(0u,0u,0u,0u);n.exponent=0;n.negative=0u;n.diagnostic=diagnostic;
    uint digits=f12_digit_count(coefficient);
    if(digits!=0u){if(digits>12u){uint discarded=digits-12u,guard_word=(discarded-1u)/4u,guard_scale=f12_partial_power((discarded-1u)%4u);
        uint source_limb=f12_get(coefficient,guard_word),guard=(source_limb/guard_scale)%12u,sticky=source_limb%guard_scale;
        // At most32 source digits minus12 retained digits: guard_word<=4.
        if(guard_word>0u)sticky|=coefficient.low.x;if(guard_word>1u)sticky|=coefficient.low.y;
        if(guard_word>2u)sticky|=coefficient.low.z;if(guard_word>3u)sticky|=coefficient.low.w;
        coefficient=f12_right_shift(coefficient,discarded);
        if((guard|sticky)!=0u)n.diagnostic|=D12_INEXACT;
        if(guard>6u||(guard==6u&&(sticky!=0u||(coefficient.low.x&1u)!=0u)))coefficient=f12_add_small(coefficient,1u);
        exponent+=int(discarded);
        if(coefficient.low.w!=0u){coefficient=f12_zero_wide();coefficient.low.z=F12_MIN_HIGH;++exponent;}
    }else if(digits<12u){uint shift=12u-digits;coefficient=f12_shift(coefficient,shift);exponent-=int(shift);}
        n.coefficient=uint4(coefficient.low.x,coefficient.low.y,coefficient.low.z,0u);n.exponent=exponent;n.negative=negative;}return n;}
SmoD12 f12_pack(F12Number n){SmoD12 result=smo_d12_zero();result.diagnostic=n.diagnostic;
    if((n.diagnostic&D12_FATAL)==0u&&(n.coefficient.x|n.coefficient.y|n.coefficient.z)!=0u){
        if(n.exponent>192)result.diagnostic|=1u;else if(n.exponent< -192)result.diagnostic|=2u;
        else{result.digits=uint4(n.coefficient.x,n.coefficient.y,n.coefficient.z,F12_TAG|uint(n.exponent+192));result.negative=n.negative;}}
    return result;}
F12Number f12_multiply(F12Number a,F12Number b,uint negative){F12Wide product=f12_zero_wide();
    F12_LOOP for(uint i=0u;i<3u;++i){uint carry=0u;F12_LOOP for(uint j=0u;j<3u;++j){uint at=i+j,n=a.coefficient[i]*b.coefficient[j]+f12_get(product,at)+carry;f12_set(product,at,n%F12_R);carry=n/F12_R;}
        F12_LOOP for(uint at=i+3u;at<8u&&carry!=0u;++at){uint n=f12_get(product,at)+carry;f12_set(product,at,n%F12_R);carry=n/F12_R;}}
    return f12_normalize(product,a.exponent+b.exponent,negative,a.diagnostic|b.diagnostic);}
// Exact four-limb scratch arithmetic. The division remainder/trial is<R^4:
// divisor<R^3 and (remainder*R+digit)<divisor*R. The restoring square root's
// widened remainder/trial is<12^14, also below R^4=12^16. Fixed component
// operations avoid inlining eight-way Wide getters/setters in these searches.
int f12_compare4(uint4 a,uint4 b){int result=0;if(a.w!=b.w)result=a.w<b.w?-1:1;else if(a.z!=b.z)result=a.z<b.z?-1:1;
    else if(a.y!=b.y)result=a.y<b.y?-1:1;else if(a.x!=b.x)result=a.x<b.x?-1:1;return result;}
uint4 f12_scale4(uint4 a,uint factor){uint4 r;uint product=a.x*factor;r.x=product%F12_R;uint carry=product/F12_R;
    product=a.y*factor+carry;r.y=product%F12_R;carry=product/F12_R;
    product=a.z*factor+carry;r.z=product%F12_R;carry=product/F12_R;
    product=a.w*factor+carry;r.w=product%F12_R;return r;}
uint4 f12_add_small4(uint4 a,uint amount){uint sum=a.x+amount;a.x=sum%F12_R;uint carry=sum/F12_R;
    sum=a.y+carry;a.y=sum%F12_R;carry=sum/F12_R;sum=a.z+carry;a.z=sum%F12_R;carry=sum/F12_R;
    sum=a.w+carry;a.w=sum%F12_R;return a;}
uint4 f12_sub4(uint4 a,uint4 b){uint4 r;uint amount=b.x,borrow=a.x<amount?1u:0u;r.x=a.x+borrow*F12_R-amount;
    amount=b.y+borrow;borrow=a.y<amount?1u:0u;r.y=a.y+borrow*F12_R-amount;
    amount=b.z+borrow;borrow=a.z<amount?1u:0u;r.z=a.z+borrow*F12_R-amount;
    amount=b.w+borrow;borrow=a.w<amount?1u:0u;r.w=a.w+borrow*F12_R-amount;return r;}
bool f12_nonzero4(uint4 a){return (a.x|a.y|a.z|a.w)!=0u;}
F12Number f12_divide(F12Number a,F12Number b,uint negative){F12Wide dividend=f12_coefficient(a.coefficient);uint4 divisor=b.coefficient,remainder=uint4(0u,0u,0u,0u),quotient=uint4(0u,0u,0u,0u);
    uint shift=f12_compare4(a.coefficient,b.coefficient)>=0?11u:12u;dividend=f12_shift(dividend,shift);
    // C<12^12 and shift<=12 imply numerator<R^6. The former two leading
    // zero steps left quotient/remainder zero and produced no diagnostics.
    F12_LOOP for(uint step=0u;step<6u;++step){uint at=5u-step;
        remainder=uint4(f12_get(dividend,at),remainder.x,remainder.y,remainder.z);
        uint low=0u,high=F12_R-1u,best=0u;
        F12_LOOP for(uint search=0u;search<15u&&low<=high;++search){uint middle=(low+high)/2u;uint4 trial=f12_scale4(divisor,middle);
            if(f12_compare4(trial,remainder)<=0){best=middle;low=middle+1u;}else high=middle-1u;}
        quotient=uint4(best,quotient.x,quotient.y,quotient.z);remainder=f12_sub4(remainder,f12_scale4(divisor,best));}
    uint diagnostic=a.diagnostic|b.diagnostic;if(f12_nonzero4(remainder))diagnostic|=D12_INEXACT;
    int rounding=f12_compare4(f12_scale4(remainder,2u),divisor);if(rounding>0||(rounding==0&&(quotient.x&1u)!=0u))quotient=f12_add_small4(quotient,1u);
    F12Wide rounded=f12_zero_wide();rounded.low=quotient;return f12_normalize(rounded,a.exponent-b.exponent-int(shift),negative,diagnostic);}
F12Number f12_add(F12Number a,F12Number b){uint diagnostic=a.diagnostic|b.diagnostic;
    if(f12_number_empty(a)){b.diagnostic=diagnostic;return b;}if(f12_number_empty(b)){a.diagnostic=diagnostic;return a;}
    F12Wide left=f12_coefficient(a.coefficient),right=f12_coefficient(b.coefficient);int delta=a.exponent-b.exponent;
    // Gap13 still matters below the minimum coefficient: its lower neighbour
    // has an ULP twelve times smaller. Exact alignment includes that boundary.
    if(delta>=14){a.diagnostic=diagnostic|D12_INEXACT;return a;}if(delta<= -14){b.diagnostic=diagnostic|D12_INEXACT;return b;}
    int exponent=a.exponent;if(delta>=0){left=f12_shift(left,uint(delta));exponent=b.exponent;}else right=f12_shift(right,uint(-delta));
    if(a.negative==b.negative)return f12_normalize(f12_add_wide(left,right),exponent,a.negative,diagnostic);
    if(f12_compare_wide(left,right)>=0)return f12_normalize(f12_sub_wide(left,right),exponent,a.negative,diagnostic);
    return f12_normalize(f12_sub_wide(right,left),exponent,b.negative,diagnostic);}
F12Number f12_subtract(F12Number a,F12Number b){if(!f12_number_empty(b))b.negative=1u-b.negative;return f12_add(a,b);}

// uint32 has at most nine radix12 digits and is EXACT in the twelve-digit
// format. Normalize its three coefficient limbs directly: no generic Wide
// quantization, discarded digits, or INEXACT result is involved.
SmoD12 smo_d12_uint(uint value){SmoD12 result=smo_d12_zero();
    if(value!=0u){F12Wide input_value=f12_small(value);uint4 coefficient=input_value.low;int exponent=0;
        F12_LOOP for(uint i=0u;i<11u&&coefficient.z<F12_MIN_HIGH;++i){coefficient=f12_scale4(coefficient,12u);--exponent;}
        result.digits=uint4(coefficient.x,coefficient.y,coefficient.z,F12_TAG|uint(exponent+192));}return result;}
bool smo_d12_packed_valid(uint value){for(uint i=0u;i<8u;++i)if(((value>>(4u*i))&15u)>11u)return false;return true;}
uint smo_d12_unpack(uint value){uint result=0u;for(uint i=0u;i<8u;++i)result=result*12u+((value>>(4u*(7u-i)))&15u);return result;}
SmoD12 smo_d12_literal(uint whole,uint fraction,uint count,uint negative){
    SmoD12 result=smo_d12_failure(16u);
    bool valid=count<=8u&&negative<=1u&&smo_d12_packed_valid(whole)&&smo_d12_packed_valid(fraction);
    if(valid){if(count<8u){if((fraction>>(4u*count))!=0u)valid=false;}}
    if(valid){F12Wide integral=f12_small(smo_d12_unpack(whole));uint4 coefficient=integral.low;
        F12_LOOP for(uint i=0u;i<count;++i)coefficient=f12_scale4(coefficient,12u);
        coefficient=f12_add_small4(coefficient,smo_d12_unpack(fraction));F12Wide exact=f12_zero_wide();exact.low=coefficient;
        result=f12_pack(f12_normalize(exact,-int(count),negative,0u));}return result;}
SmoD12 smo_d12_abs(SmoD12 a){uint diagnostic=smo_d12_diagnostics(a);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);a.negative=0u;return a;}
SmoD12 smo_d12_negate(SmoD12 a){uint diagnostic=smo_d12_diagnostics(a);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);if(!smo_d12_empty(a))a.negative=1u-a.negative;return a;}
int smo_d12_compare(SmoD12 a,SmoD12 b){if(!smo_d12_valid(a)||!smo_d12_valid(b))return 2;
    if(a.negative!=b.negative)return a.negative!=0u?-1:1;
    bool az=smo_d12_empty(a),bz=smo_d12_empty(b);int order=0;
    if(az||bz){if(!az)order=1;else if(!bz)order=-1;}
    else{F12Number l=f12_unpack(a),r=f12_unpack(b);if(l.exponent<r.exponent)order=-1;else if(l.exponent>r.exponent)order=1;else order=f12_compare_wide(f12_coefficient(l.coefficient),f12_coefficient(r.coefficient));}
    return a.negative!=0u?-order:order;}
SmoD12 smo_d12_add(SmoD12 a,SmoD12 b){uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    a.diagnostic=diagnostic;b.diagnostic=diagnostic;return f12_pack(f12_add(f12_unpack(a),f12_unpack(b)));}
SmoD12 smo_d12_sub(SmoD12 a,SmoD12 b){return smo_d12_add(a,smo_d12_negate(b));}
SmoD12 smo_d12_magnitude_product(SmoD12 a,SmoD12 b,uint negative){return f12_pack(f12_multiply(f12_unpack(a),f12_unpack(b),negative));}
SmoD12 smo_d12_magnitude_divide(SmoD12 a,SmoD12 b,uint negative){return f12_pack(f12_divide(f12_unpack(a),f12_unpack(b),negative));}
SmoD12 smo_d12_mul(SmoD12 a,SmoD12 b){uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    a.diagnostic=diagnostic;b.diagnostic=diagnostic;if(smo_d12_empty(a)||smo_d12_empty(b)){SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;return zero;}
    return smo_d12_magnitude_product(a,b,a.negative|b.negative);}
SmoD12 smo_d12_div(SmoD12 a,SmoD12 b){uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    a.diagnostic=diagnostic;b.diagnostic=diagnostic;if(smo_d12_empty(b))return a;if(smo_d12_empty(a)){SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;return zero;}
    return smo_d12_magnitude_divide(a,b,a.negative|b.negative);}

F12Number f12_sqrt(F12Number n){uint diagnostic=n.diagnostic;if(n.negative!=0u)return f12_normalize(f12_zero_wide(),0,0u,diagnostic|4u);if(f12_number_empty(n))return n;
    uint shift=n.exponent%2==0?12u:11u;F12Wide radicand=f12_shift(f12_coefficient(n.coefficient),shift);uint4 root=uint4(0u,0u,0u,0u),remainder=uint4(0u,0u,0u,0u);
    F12_LOOP for(uint step=0u;step<12u;++step){uint pair_at=(11u-step)*2u;
        remainder=f12_add_small4(f12_scale4(remainder,144u),f12_digit(radicand,pair_at+1u)*12u+f12_digit(radicand,pair_at));
        uint chosen=0u;uint4 selected=uint4(0u,0u,0u,0u);
        F12_LOOP for(uint attempt=0u;attempt<12u;++attempt){uint digit=11u-attempt;uint4 trial=f12_scale4(f12_add_small4(f12_scale4(root,24u),digit),digit);
            if(f12_compare4(trial,remainder)<=0){chosen=digit;selected=trial;break;}}
        remainder=f12_sub4(remainder,selected);root=f12_add_small4(f12_scale4(root,12u),chosen);}
    if(f12_nonzero4(remainder))diagnostic|=D12_INEXACT;
    if(f12_compare4(remainder,root)>0)root=f12_add_small4(root,1u);
    F12Wide rounded=f12_zero_wide();rounded.low=root;return f12_normalize(rounded,(n.exponent-int(shift))/2,0u,diagnostic);}
SmoD12 smo_d12_sqrt(SmoD12 a){uint diagnostic=smo_d12_diagnostics(a);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);return f12_pack(f12_sqrt(f12_unpack(a)));}

bool f12_small_integer(SmoD12 a,out uint value){value=0u;if(!smo_d12_valid(a))return false;if(smo_d12_empty(a))return true;
    F12Number n=f12_unpack(a);if(n.exponent>0)return false;F12Wide coefficient=f12_coefficient(n.coefficient);
    F12_LOOP for(int i=0;i< -n.exponent;++i){uint remainder=0u;coefficient=f12_div_small(coefficient,12u,remainder);if(remainder!=0u)return false;}
    if((coefficient.low.y|coefficient.low.z|coefficient.low.w|coefficient.high.x|coefficient.high.y|coefficient.high.z|coefficient.high.w)!=0u||coefficient.low.x>64u)return false;
    value=coefficient.low.x;return true;}
SmoD12 smo_d12_power_primary(SmoD12 a,SmoD12 exponent,uint profile,bool inverted){
    if((profile&~63u)!=0u||(profile&12u)==12u)return smo_d12_failure(64u);uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(exponent);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);a.diagnostic=diagnostic;exponent.diagnostic=diagnostic;
    if(a.negative!=0u||smo_d12_empty(a))return smo_d12_failure(diagnostic|4u);uint count=0u;if(!f12_small_integer(exponent,count))return smo_d12_failure(diagnostic|128u);
    if(count==0u){if((profile&16u)!=0u)return a;SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;return zero;}
    if(count==1u&&(profile&32u)!=0u){SmoD12 one=smo_d12_uint(1u);one.negative=exponent.negative;one.diagnostic=diagnostic;return one;}
    uint degree=inverted?(count>=2u?count-2u:1u):count;
    F12Number base=f12_unpack(a),magnitude=f12_unpack(smo_d12_uint(1u));magnitude.diagnostic=diagnostic;
    // Only coefficients round in this left fold. Do not pack/bound intermediate
    // exponents: a tiny denominator can have a representable final reciprocal.
    F12_LOOP for(uint i=0u;i<degree;++i)magnitude=f12_multiply(magnitude,base,0u);
    if(inverted&&count>2u)magnitude=f12_divide(f12_unpack(smo_d12_uint(1u)),magnitude,0u);
    magnitude.negative=exponent.negative;return f12_pack(magnitude);}
SmoD12 smo_d12_pdir(SmoD12 a,SmoD12 exponent){return smo_d12_power_primary(a,exponent,0u,false);}
SmoD12 smo_d12_pinv(SmoD12 a,SmoD12 exponent){return smo_d12_power_primary(a,exponent,0u,true);}

// Explicit scalar evaluation tree for graphics. Unlike the separate fixed D12
// experiment, these helpers make NO fused-precision promise.
SmoD12 smo_d12_discriminant(SmoD12 h,SmoD12 a,SmoD12 c){uint diagnostic=smo_d12_diagnostics(h)|smo_d12_diagnostics(a)|smo_d12_diagnostics(c);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);if(a.negative!=0u||smo_d12_empty(a))return smo_d12_failure(diagnostic|4u);
    F12Number absolute=f12_unpack(h),positive=f12_unpack(a),term=f12_unpack(c);absolute.negative=0u;
    F12Number result=f12_subtract(f12_multiply(absolute,absolute,0u),f12_multiply(positive,term,term.negative));result.diagnostic|=diagnostic;return f12_pack(result);}
F12Number f12_magnitude2(F12Number a,F12Number b,F12Number c){a.negative=0u;b.negative=0u;c.negative=0u;
#ifdef SMO_HLSL
    // One multiply/add callsite prevents DXBC's inliner from copying the
    // checked wide arithmetic for each axis. The RNE tree remains exactly
    // (a*a + b*b) + c*c; axis0 is assigned without an extra addition of zero.
    F12Number result=a;
    [loop]for(uint axis=0u;axis<3u;++axis){F12Number component=a;if(axis==1u)component=b;else if(axis==2u)component=c;
        F12Number term=f12_multiply(component,component,0u);if(axis==0u)result=term;else result=f12_add(result,term);}
    return result;
#else
    return f12_add(f12_add(f12_multiply(a,a,0u),f12_multiply(b,b,0u)),f12_multiply(c,c,0u));
#endif
}
SmoD12 smo_d12_magnitude2(SmoD12 x,SmoD12 y,SmoD12 z){uint diagnostic=smo_d12_diagnostics(x)|smo_d12_diagnostics(y)|smo_d12_diagnostics(z);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    return f12_pack(f12_magnitude2(f12_unpack(x),f12_unpack(y),f12_unpack(z)));}
SmoD12 smo_d12_norm3(SmoD12 x,SmoD12 y,SmoD12 z){uint diagnostic=smo_d12_diagnostics(x)|smo_d12_diagnostics(y)|smo_d12_diagnostics(z);if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    if(smo_d12_empty(y)&&smo_d12_empty(z)){SmoD12 a=smo_d12_abs(x);a.diagnostic=diagnostic;return a;}
    if(smo_d12_empty(x)&&smo_d12_empty(z)){SmoD12 b=smo_d12_abs(y);b.diagnostic=diagnostic;return b;}
    if(smo_d12_empty(x)&&smo_d12_empty(y)){SmoD12 c=smo_d12_abs(z);c.diagnostic=diagnostic;return c;}
    return f12_pack(f12_sqrt(f12_magnitude2(f12_unpack(x),f12_unpack(y),f12_unpack(z))));}
SmoD12 smo_d12_min(SmoD12 a,SmoD12 b){int order=smo_d12_compare(a,b);if(order==2)return smo_d12_failure(smo_d12_diagnostics(a)|smo_d12_diagnostics(b));SmoD12 result=a;if(order>0)result=b;result.diagnostic|=a.diagnostic|b.diagnostic;return result;}
SmoD12 smo_d12_max(SmoD12 a,SmoD12 b){int order=smo_d12_compare(a,b);if(order==2)return smo_d12_failure(smo_d12_diagnostics(a)|smo_d12_diagnostics(b));SmoD12 result=a;if(order<0)result=b;result.diagnostic|=a.diagnostic|b.diagnostic;return result;}
SmoD12 smo_d12_clamp(SmoD12 a,SmoD12 low,SmoD12 high){uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(low)|smo_d12_diagnostics(high);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);if(smo_d12_compare(low,high)>0)return smo_d12_failure(diagnostic|4u);
    a.diagnostic=diagnostic;low.diagnostic=diagnostic;high.diagnostic=diagnostic;return smo_d12_min(smo_d12_max(a,low),high);}
uint smo_d12_round_uint(SmoD12 a,inout uint diagnostic){diagnostic|=smo_d12_diagnostics(a);if(!smo_d12_valid(a))return 0u;
    if(a.negative!=0u||smo_d12_compare(a,smo_d12_uint(255u))>0){diagnostic|=4u;return 0u;}if(smo_d12_empty(a))return 0u;
    F12Number n=f12_unpack(a);F12Wide coefficient=f12_coefficient(n.coefficient);uint guard=0u,sticky=0u;
    F12_LOOP for(int i=0;i< -n.exponent;++i){uint remainder=0u;coefficient=f12_div_small(coefficient,12u,remainder);if(i+1== -n.exponent)guard=remainder;else sticky|=remainder;}
    uint whole=coefficient.low.x;if(guard>6u||(guard==6u&&(sticky!=0u||(whole&1u)!=0u)))++whole;return min(whole,255u);}

SmoD12Result smo_d12_evaluate(uint operation,SmoD12 a,SmoD12 b,uint profile){SmoD12Result result;result.primary=smo_d12_failure(32u);result.alternative=smo_d12_failure(4u);result.flags=profile!=0u?128u:0u;
    if((profile&~63u)!=0u||(profile&12u)==12u){result.primary=smo_d12_failure(64u);return result;}
    uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);if((diagnostic&D12_FATAL)!=0u){result.primary=smo_d12_failure(diagnostic);return result;}a.diagnostic=diagnostic;b.diagnostic=diagnostic;
    switch(operation){
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 0
    case 0u:result.primary=smo_d12_add(a,b);break;
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 1
    case 1u:result.primary=smo_d12_sub(a,b);break;
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 2
    case 2u:if(smo_d12_empty(a)||smo_d12_empty(b)){SmoD12 other=a;if(smo_d12_empty(a))other=b;SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;
        if((profile&2u)!=0u){result.primary=other;result.alternative=zero;}else{result.primary=zero;result.alternative=other;}result.flags|=2u;}
        else result.primary=smo_d12_magnitude_product(a,b,(profile&1u)!=0u?(a.negative^b.negative):(a.negative|b.negative));break;
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 3
    case 3u:if(smo_d12_empty(b)){uint policy=profile&12u;SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;
        if(policy==8u){result.primary=smo_d12_failure(diagnostic|8u);result.flags|=4u;return result;}
        if(policy==4u){result.primary=zero;result.alternative=a;}else{result.primary=a;result.alternative=zero;}result.flags|=2u;}
        else if(smo_d12_empty(a)){result.primary=smo_d12_zero();result.primary.diagnostic=diagnostic;}
        else result.primary=smo_d12_magnitude_divide(a,b,(profile&1u)!=0u?(a.negative^b.negative):(a.negative|b.negative));break;
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
        result.primary=smo_d12_power_primary(a,b,profile,D12_ONLY_OPERATION==5);
#else
        result.primary=smo_d12_power_primary(a,b,profile,operation==5u);
#endif
        if(!smo_d12_valid(result.primary))return result;
        if(smo_d12_empty(b)){SmoD12 zero=smo_d12_zero();zero.diagnostic=diagnostic;if((profile&16u)!=0u)result.alternative=zero;else result.alternative=a;result.flags|=2u;}break;
    }
#endif
#if !defined(D12_ONLY_OPERATION) || D12_ONLY_OPERATION == 9
    case 9u:result.primary=smo_d12_sqrt(a);break;
#endif
    default:break;}
    if(smo_d12_valid(result.primary))result.flags|=1u;return result;}
SmoD12 smo_d12_load(uint storage,uint offset){SmoD12 a;a.digits=uint4(word(storage,offset),word(storage,offset+1u),word(storage,offset+2u),word(storage,offset+3u));a.negative=word(storage,offset+4u);a.diagnostic=word(storage,offset+5u);return a;}
void smo_d12_store(uint storage,uint offset,SmoD12 a){put(storage,offset,a.digits.x);put(storage,offset+1u,a.digits.y);put(storage,offset+2u,a.digits.z);put(storage,offset+3u,a.digits.w);put(storage,offset+4u,a.negative);put(storage,offset+5u,a.diagnostic);}
