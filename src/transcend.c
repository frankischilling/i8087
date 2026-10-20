/* SPDX-License-Identifier: MIT
 * Optional, explicitly approximate transcendental path. Rounded integer-only
 * binary80 series; no host libm. The original 8087 used different algorithms.
 * In particular, sin/cos here do NOT reproduce the two raw FPTAN CORDIC outputs.
 */
#include "transcend.h"

static const i8087_f80 one={UINT64_C(0x8000000000000000),0x3fff};
static const i8087_f80 half={UINT64_C(0x8000000000000000),0x3ffe};
static const i8087_f80 ln2={UINT64_C(0xb17217f7d1cf79ac),0x3ffe};
static const i8087_f80 invln2={UINT64_C(0xb8aa3b295c17f0bc),0x3fff};
static const i8087_f80 pi4={UINT64_C(0xc90fdaa22168c235),0x3ffe};
static const i8087_f80 log1p_bound={UINT64_C(0x95f619980c4336f7),0x3ffd};
static i8087_f80 add(i8087_f80 a,i8087_f80 b){return sf_add(a,b,64,0).value;}
static i8087_f80 neg(i8087_f80 a){a.sign_exponent^=0x8000;return a;}
static i8087_f80 sub(i8087_f80 a,i8087_f80 b){return add(a,neg(b));}
static i8087_f80 mul(i8087_f80 a,i8087_f80 b){return sf_multiply(a,b,64,0).value;}
static i8087_f80 divv(i8087_f80 a,i8087_f80 b){return sf_divide(a,b,64,0).value;}
static i8087_f80 integer(unsigned n){return sf_from_u64(n,false);}
static bool same(i8087_f80 a,i8087_f80 b){return a.sign_exponent==b.sign_exponent&&a.significand==b.significand;}
static bool finite_normal(i8087_f80 a){return sf_kind(a)==SF_FINITE&&!sf_unnormal(a)&&!sf_denormal(a);}
static bool normal_or_zero(i8087_f80 a){return sf_kind(a)==SF_ZERO||finite_normal(a);}

static i8087_f80 atanh_series(i8087_f80 z) {
    i8087_f80 zz=mul(z,z),term=z,sum=z;
    unsigned n;
    for(n=3;n<203;n+=2) {
        i8087_f80 next;
        term=mul(term,zz);next=add(sum,divv(term,integer(n)));
        if(same(next,sum))break;
        sum=next;
    }
    return mul(sum,integer(2));
}
static i8087_f80 log2_value(i8087_f80 x) {
    int32_t e=sf_exponent(x);
    i8087_f80 mantissa=x, frac,whole;
    mantissa.sign_exponent=0x3fff;
    frac=mul(atanh_series(divv(sub(mantissa,one),add(mantissa,one))),invln2);
    whole=sf_from_u64((uint64_t)(e<0 ? -e : e),e<0);
    return add(whole,frac);
}
static i8087_f80 atan_value(i8087_f80 z) {
    i8087_f80 zz,term,sum,base=sf_zero(false);
    unsigned n;
    if(sf_compare(z,half)>0){z=divv(sub(z,one),add(z,one));base=pi4;}
    zz=neg(mul(z,z));term=z;sum=z;
    for(n=3;n<203;n+=2) {
        i8087_f80 next;
        term=mul(term,zz);next=add(sum,divv(term,integer(n)));
        if(same(next,sum))break;
        sum=next;
    }
    return add(base,sum);
}
bool sf_transcend(uint8_t op, i8087_f80 x, i8087_f80 y,
                  sf_result *out, i8087_f80 *pushed) {
    i8087_f80 z,term,sum,xx;
    unsigned n;
    *out=(sf_result){0};out->flags=I8087_PE;out->rounded_up=false;
    if(op==0xf0) {
        /* 8087's original documented interval, not the later [-1,+1]. */
        if(!normal_or_zero(x)||sf_sign(x)||sf_compare(x,half)>0)return false;
        if(sf_kind(x)==SF_ZERO){out->value=x;out->flags=0;return true;}
        z=mul(x,ln2);sum=z;term=z;
        for(n=2;n<64;++n) {
            i8087_f80 next;
            term=divv(mul(term,z),integer(n));next=add(sum,term);
            if(same(next,sum))break;
            sum=next;
        }
        out->value=sum;return true;
    }
    if(op==0xf1) {
        if(!finite_normal(x)||sf_sign(x)||!normal_or_zero(y))return false;
        {
            sf_result product=sf_multiply(y,log2_value(x),64,0);
            *out=product;
            if(x.significand!=UINT64_C(0x8000000000000000)&&sf_kind(y)!=SF_ZERO)
                out->flags|=I8087_PE;
        }
        return true;
    }
    if(op==0xf9) {
        z=x;z.sign_exponent&=0x7fff;
        if(!normal_or_zero(x)||!normal_or_zero(y)||sf_compare(z,log1p_bound)>0)return false;
        out->value=mul(y,mul(atanh_series(divv(x,add(integer(2),x))),invln2));
        if(sf_kind(x)==SF_ZERO||sf_kind(y)==SF_ZERO)out->flags=0;
        return true;
    }
    if(op==0xf3) {
        if(!finite_normal(x)||!normal_or_zero(y)||sf_sign(x)||sf_sign(y)||sf_compare(y,x)>=0)return false;
        out->value=atan_value(divv(y,x));
        if(sf_kind(y)==SF_ZERO)out->flags=0;
        return true;
    }
    if(op==0xf2) {
        i8087_f80 sine,cosine;
        if(!normal_or_zero(x)||sf_sign(x)||sf_compare(x,pi4)>0)return false;
        xx=neg(mul(x,x));term=x;sine=x;
        for(n=1;n<24;++n) {
            term=divv(mul(term,xx),integer((2*n)*(2*n+1)));
            sine=add(sine,term);
        }
        term=one;cosine=one;
        for(n=1;n<24;++n) {
            term=divv(mul(term,xx),integer((2*n-1)*(2*n)));
            cosine=add(cosine,term);
        }
        out->value=sine;*pushed=cosine;
        if(sf_kind(x)==SF_ZERO)out->flags=0;
        return true;
    }
    return false;
}
