/* SPDX-License-Identifier: MIT */
#include "i8087.h"
#include "soft80.h"
#include "transcend.h"
#include <string.h>

#define CC (I8087_C0|I8087_C1|I8087_C2|I8087_C3)
#define ADDRESS_MASK UINT32_C(0xfffff)
#define EXCEPTIONS 0x003fu
#define JBIT UINT64_C(0x8000000000000000)

typedef struct transaction {
    i8087 f;
    uint16_t raised;
    uint8_t bytes[94];
    size_t write_size;
    bool control;
    bool always_stop;
    bool compare_operation;
    bool register_range_result;
} transaction;

static uint16_t get16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}
static uint64_t get_le(const uint8_t *p,size_t n) {
    uint64_t r=0;size_t i;for(i=0;i<n;++i)r|=(uint64_t)p[i]<<(8*i);return r;
}
static void put_le(uint8_t *p,uint64_t x,size_t n) {
    size_t i;for(i=0;i<n;++i){p[i]=(uint8_t)x;x>>=8;}
}
void i8087_encode_f80(uint8_t p[10],i8087_f80 x) {
    if(!p)return;
    put_le(p,x.significand,8);put_le(p+8,x.sign_exponent,2);
}
i8087_f80 i8087_decode_f80(const uint8_t p[10]) {
    i8087_f80 x={0,0};
    if(p){x.significand=get_le(p,8);x.sign_exponent=get16(p+8);}
    return x;
}
unsigned i8087_top(const i8087 *f){return f?(unsigned)((f->status>>11)&7):0;}
static unsigned physical(const i8087 *f,unsigned i){return (i8087_top(f)+i)&7;}
unsigned i8087_tag(const i8087 *f,unsigned i) {
    return f&&i<8?(unsigned)((f->tags>>(physical(f,i)*2))&3):I8087_TAG_EMPTY;
}
bool i8087_get_st(const i8087 *f,unsigned i,i8087_f80 *x) {
    if(!f||!x||i>=8||i8087_tag(f,i)==I8087_TAG_EMPTY)return false;
    *x=f->registers[physical(f,i)];return true;
}
static void set_top(i8087 *f,unsigned top) {
    f->status=(uint16_t)((f->status&(uint16_t)~I8087_TOP)|((top&7u)<<11));
}
static void set_tag(i8087 *f,unsigned p,unsigned tag) {
    f->tags=(uint16_t)((f->tags&~(3u<<(2*p)))|((tag&3)<<(2*p)));
}
static void set_st(i8087 *f,unsigned i,i8087_f80 x) {
    unsigned p=physical(f,i);f->registers[p]=x;set_tag(f,p,sf_tag(x));
}
/* IR is an exception-event latch, not a combinational function of the six
   sticky status flags and the CURRENT masks. FLDCW must not retroactively
   turn an old masked exception into a new interrupt. */
static void synchronize_busy(i8087 *f) {
    if(f->status&I8087_IR)f->status|=I8087_BUSY;
    else f->status&=(uint16_t)~I8087_BUSY;
}
bool i8087_interrupt_asserted(const i8087 *f) {
    return f&&(f->status&I8087_IR)&&(!(f->control&I8087_INTERRUPT_MASK)||f->cpu_waiting);
}
i8087_result i8087_wait(i8087 *f) {
    if(!f)return I8087_BAD_ARGUMENT;
    f->cpu_waiting=true;
    if(f->status&I8087_IR)return I8087_PENDING_INTERRUPT;
    f->cpu_waiting=false;return I8087_OK;
}
static void software_init(i8087 *f) {
    /* Reserved bit 6 is deterministically zero. CC bits are unspecified by
       the original manual; retaining them also avoids inventing a reset value. */
    f->control=0x03bf;f->status&=CC;f->tags=0xffff;f->cpu_waiting=false;
}
void i8087_init(i8087 *f) {
    if(!f)return;
    memset(f,0,sizeof(*f));f->policy=I8087_STOP_ON_UNVERIFIED;software_init(f);
}
void i8087_set_policy(i8087 *f,i8087_policy p) {
    if(f&&(p==I8087_STOP_ON_UNVERIFIED||p==I8087_ALLOW_APPROXIMATION))f->policy=p;
}
static void gap(transaction *t,uint32_t why){t->f.last_gap|=why;}
static i8087_f80 read_st(transaction *t,unsigned i) {
    i8087_f80 x;
    if(i8087_get_st(&t->f,i,&x))return x;
    t->raised|=I8087_IE;t->f.status&=(uint16_t)~I8087_C1;
    gap(t,I8087_GAP_STACK_FAULT);return sf_indefinite();
}
static void push_tagged(transaction *t,i8087_f80 x,unsigned tag) {
    unsigned p=(i8087_top(&t->f)+7)&7;
    if(((t->f.tags>>(2*p))&3)!=I8087_TAG_EMPTY) {
        gap(t,I8087_GAP_STACK_FAULT);t->raised|=I8087_IE;
        t->f.status|=I8087_C1;x=sf_indefinite();tag=I8087_TAG_SPECIAL;
    }
    set_top(&t->f,p);t->f.registers[p]=x;set_tag(&t->f,p,tag);
}
static void push(transaction *t,i8087_f80 x) {push_tagged(t,x,sf_tag(x));}
static unsigned read_tag(const transaction *t,unsigned i) {
    unsigned tag=i8087_tag(&t->f,i);
    return tag==I8087_TAG_EMPTY?I8087_TAG_SPECIAL:tag;
}
static void write_tagged(i8087 *f,unsigned i,i8087_f80 x,unsigned tag) {
    unsigned p=physical(f,i);f->registers[p]=x;set_tag(f,p,tag);
}
static void pop(transaction *t) {
    set_tag(&t->f,i8087_top(&t->f),I8087_TAG_EMPTY);
    set_top(&t->f,(i8087_top(&t->f)+1)&7);
}
static unsigned precision(transaction *t) {
    unsigned pc=(t->f.control>>8)&3;
    if(pc==1)gap(t,I8087_GAP_RESERVED_PRECISION);
    return pc==0?24:(pc==2?53:64);
}
static unsigned rounding(const transaction *t){return (t->f.control>>10)&3;}
static void accept_numeric(transaction *t,sf_result *r) {
    sf_unbounded *u=&r->unbounded;
    uint16_t range=0;
    if(u->valid) {
        if(u->exponent>16383)range=I8087_OE;
        else if(u->exact_exponent< -16382)range=I8087_UE;
    }
    if(range&&!(t->f.control&range)) {
        int32_t wrapped=u->exponent+(range==I8087_UE?24576:-24576);
        /* Intel S-67/S-68: retain the rounded significand and wrap only the
           exponent on an unmasked REGISTER overflow/underflow. */
        if(wrapped>=-16382&&wrapped<=16383) {
            r->value.significand=u->significand;
            r->value.sign_exponent=(uint16_t)((wrapped+16383)|(u->sign?0x8000:0));
            r->flags=(uint16_t)(u->flags|range);
            r->rounded_up=u->rounded_up;
            t->register_range_result=true;
        } else gap(t,I8087_GAP_RANGE_EXCEPTION);
    } else if(range) {
        r->flags|=range; /* Original underflow also reports exact tiny results. */
        if(range==I8087_OE) {
            unsigned rc=rounding(t);
            if(rc==1||rc==2) {
                /* Intel S-75: masked directed overflow raises PE, not OE.
                   NONNORMAL_RESULT constructs a full-width finite bound after
                   precision adjustment; see research/DIRECTED_OVERFLOW.md. */
                r->flags&=(uint16_t)~I8087_OE;r->flags|=I8087_PE;
                if(sf_kind(r->value)!=SF_INFINITY &&
                   !(t->f.last_gap&(I8087_GAP_UNNORMAL|I8087_GAP_RANGE_EXCEPTION))) {
                    r->value.significand=UINT64_MAX;
                    r->value.sign_exponent=(uint16_t)(0x7ffe|(u->sign?0x8000:0));
                }
            } else if(rc==3) {
                /* Intel's 1981 manual specifies infinity for masked CHOP
                   overflow. Die-level interpretation is not yet conclusive:
                   retain a guard for this specific disagreement. */
                r->value=sf_infinity(u->sign);
                gap(t,I8087_GAP_RANGE_EXCEPTION);
            }
        } else {
            /* Masked register underflow + reduced PC needs silicon vectors. */
            if(((t->f.control>>8)&3)!=3)gap(t,I8087_GAP_RANGE_EXCEPTION);
        }
    } else if((r->flags&(I8087_OE|I8087_UE))||sf_denormal(r->value)) {
        gap(t,I8087_GAP_RANGE_EXCEPTION);
    }
    t->raised|=r->flags;
    /* Arithmetic C1 remains an implementation choice where Intel says undefined. */
    t->f.status=(uint16_t)((t->f.status&~I8087_C1)|(r->rounded_up?I8087_C1:0));
}
static void check_denormal(transaction *t,i8087_f80 x) {
    if(sf_denormal(x))t->raised|=I8087_DE;
}
static void compare(transaction *t,i8087_f80 a,i8087_f80 b,unsigned pops) {
    int relation;
    t->compare_operation=true;
    check_denormal(t,a);check_denormal(t,b);
    /* Original Table S-32: comparing an unnormal (except a pseudozero)
       also raises the denormal-operand exception before normalization. */
    if((sf_unnormal(a)&&a.significand)||(sf_unnormal(b)&&b.significand))t->raised|=I8087_DE;
    if(!(t->f.control&I8087_AFFINE) &&
       (sf_kind(a)==SF_INFINITY||sf_kind(b)==SF_INFINITY))
        relation=sf_kind(a)==SF_INFINITY&&sf_kind(b)==SF_INFINITY?0:2;
    else relation=sf_compare(a,b);
    t->f.status&=(uint16_t)~(I8087_C0|I8087_C2|I8087_C3); /* Preserve C1. */
    if(relation==2){t->raised|=I8087_IE;t->f.status|=I8087_C0|I8087_C2|I8087_C3;}
    else if(relation==0)t->f.status|=I8087_C3;
    else if(relation<0)t->f.status|=I8087_C0;
    while(pops--)pop(t);
}
static void arithmetic(transaction *t,unsigned operation,unsigned dest,
                       i8087_f80 a,i8087_f80 b,bool do_pop) {
    sf_result r;
    unsigned p=precision(t),rc=rounding(t);
    check_denormal(t,a);check_denormal(t,b);
    if(operation==5||operation==7){i8087_f80 tmp=a;a=b;b=tmp;}
    /* Propagate NaN bytes BEFORE subtraction negates an arithmetic operand.
       The sign of a selected NaN is not a subtraction sign adjustment. */
    if(sf_kind(a)==SF_NAN||sf_kind(b)==SF_NAN)r=sf_add(a,b,p,rc);
    else if(operation==0||operation==4||operation==5) {
        if(operation==4||operation==5)b.sign_exponent^=0x8000;
        if(sf_kind(a)==SF_INFINITY&&sf_kind(b)==SF_INFINITY&&!(t->f.control&I8087_AFFINE))
            r=(sf_result){sf_indefinite(),I8087_IE,false,{0}};
        else {
            if(sf_unnormal(a)||sf_unnormal(b)||sf_denormal(a)||sf_denormal(b))gap(t,I8087_GAP_UNNORMAL);
            r=sf_add(a,b,p,rc);
        }
    } else if(operation==1) {
        if(sf_unnormal(a)||sf_unnormal(b)||sf_denormal(a)||sf_denormal(b))gap(t,I8087_GAP_UNNORMAL);
        r=sf_multiply(a,b,p,rc);
    } else {
        if(sf_unnormal(b)||sf_denormal(b))r=(sf_result){sf_indefinite(),I8087_IE,false,{0}};
        else {
            if(sf_unnormal(a)||sf_denormal(a))gap(t,I8087_GAP_UNNORMAL);
            r=sf_divide(a,b,p,rc);
        }
    }
    accept_numeric(t,&r);set_st(&t->f,dest,r.value);if(do_pop)pop(t);
}
static void examine(transaction *t) {
    i8087_f80 x=t->f.registers[i8087_top(&t->f)];
    uint16_t cc=sf_sign(x)?I8087_C1:0;
    if(i8087_tag(&t->f,0)==I8087_TAG_EMPTY)cc|=I8087_C3|I8087_C0;
    else if(sf_unnormal(x)){/* Includes pseudozeros: exponent != 0, fraction == 0. */}
    else if(sf_kind(x)==SF_ZERO)cc|=I8087_C3;
    else if(sf_kind(x)==SF_NAN)cc|=I8087_C0;
    else if(sf_kind(x)==SF_INFINITY)cc|=I8087_C2|I8087_C0;
    else if(sf_denormal(x))cc|=I8087_C3|I8087_C2;
    else if(!sf_unnormal(x))cc|=I8087_C2;
    t->f.status=(uint16_t)((t->f.status&~CC)|cc);
}
static void encode_env(const i8087 *f,uint8_t *p) {
    put_le(p,f->control,2);put_le(p+2,f->status,2);put_le(p+4,f->tags,2);
    put_le(p+6,f->instruction_address&0xffff,2);
    put_le(p+8,((f->instruction_address>>4)&0xf000)|(f->opcode&0x7ff),2);
    put_le(p+10,f->data_address&0xffff,2);
    put_le(p+12,(f->data_address>>4)&0xf000,2);
}
static void decode_env(i8087 *f,const uint8_t *p) {
    f->control=(uint16_t)(get16(p)&0x1fbf);
    f->status=(uint16_t)(get16(p+2)&0xffbf);f->tags=get16(p+4);
    f->instruction_address=(uint32_t)get16(p+6)|((uint32_t)(get16(p+8)&0xf000)<<4);
    f->opcode=(uint16_t)(get16(p+8)&0x7ff);
    f->data_address=(uint32_t)get16(p+10)|((uint32_t)(get16(p+12)&0xf000)<<4);
    synchronize_busy(f);
}
static void load_bcd(transaction *t,const uint8_t *p) {
    uint64_t v=0;int i;bool bad=false;
    for(i=8;i>=0;--i) {
        unsigned high=p[i]>>4,low=p[i]&15;
        if(high>9||low>9)bad=true;
        v=v*100+high*10+low;
    }
    if(bad){gap(t,I8087_GAP_UNDEFINED_OPERAND);t->always_stop=true;return;}
    push(t,sf_from_u64(v,(p[9]&0x80)!=0));
}
static void store_bcd(transaction *t,i8087_f80 x) {
    uint16_t flags=0;bool up=false;
    uint64_t bits,mag;size_t i;
    if(sf_unnormal(x)||sf_denormal(x)){flags|=I8087_IE;bits=0;}
    else bits=sf_to_integer(x,64,rounding(t),&flags,&up);
    mag=sf_sign(x)?UINT64_C(0)-bits:bits;
    if(mag>UINT64_C(999999999999999999))flags=I8087_IE;
    memset(t->bytes,0,10);
    if(flags&I8087_IE) {
        t->bytes[7]=0xc0;t->bytes[8]=0xff;t->bytes[9]=0xff;
    } else {
        for(i=0;i<9;++i){unsigned d=(unsigned)(mag%100);mag/=100;t->bytes[i]=(uint8_t)(((d/10)<<4)|(d%10));}
        t->bytes[9]=sf_sign(x)?0x80:0;
    }
    t->raised|=flags;
    t->f.status=(uint16_t)((t->f.status&~I8087_C1)|(up?I8087_C1:0));
    t->write_size=10;pop(t);
}
static void store_ieee(transaction *t,i8087_f80 x,unsigned width,bool do_pop) {
    uint16_t flags=0;bool up=false;uint64_t bits;
    int32_t emin=width==32?-126:-1022;
    bool unnormal=sf_unnormal(x);
    /* The original test is the stored exponent field, not the exponent after
       normalizing a small significand. */
    if(unnormal && (int32_t)(x.sign_exponent&0x7fff)-16383>=emin) {
        flags|=I8087_IE;x=sf_indefinite();
    }
    bits=sf_store_ieee(x,width,rounding(t),&flags,&up);
    if(sf_kind(x)==SF_FINITE&&sf_exponent(x)<emin)flags|=I8087_UE;
    if(unnormal&&!(flags&I8087_IE)) {
        flags|=I8087_UE;
        if(t->f.control&I8087_UE)gap(t,I8087_GAP_UNNORMAL);
    }
    if((flags&I8087_OE)&&(t->f.control&I8087_OE)) {
        unsigned rc=rounding(t);
        if(rc==1||rc==2) {
            /* S-75 applies the directed flag rule to real stores as well. */
            flags&=(uint16_t)~I8087_OE;flags|=I8087_PE;
        } else if(rc==3) {
            bits=(width==32?UINT64_C(0x7f800000):UINT64_C(0x7ff0000000000000))|
                 ((uint64_t)sf_sign(x)<<(width-1));
            gap(t,I8087_GAP_RANGE_EXCEPTION);
        }
    }
    t->raised|=flags;t->f.status=(uint16_t)((t->f.status&~I8087_C1)|(up?I8087_C1:0));
    t->write_size=width/8;put_le(t->bytes,bits,t->write_size);if(do_pop)pop(t);
}
static void store_integer(transaction *t,i8087_f80 x,unsigned width,bool do_pop) {
    uint16_t flags=0;bool up=false;uint64_t bits;
    if(sf_unnormal(x)||sf_denormal(x)){flags=I8087_IE;bits=UINT64_C(1)<<(width-1);}
    else bits=sf_to_integer(x,width,rounding(t),&flags,&up);
    t->raised|=flags;t->f.status=(uint16_t)((t->f.status&~I8087_C1)|(up?I8087_C1:0));
    t->write_size=width/8;put_le(t->bytes,bits,t->write_size);if(do_pop)pop(t);
}

/* Valid documented memory encodings; newer x87 extensions are not accepted. */
static size_t memory_read_size(uint8_t esc,unsigned reg,bool *valid,bool *control) {
    *valid=true;*control=false;
    if(!(esc&1))return esc==0xd8||esc==0xda?4:(esc==0xdc?8:2);
    switch(esc) {
    case 0xd9:
        switch(reg){case 0:return 4;case 2:case 3:return 0;
        case 4:*control=true;return 14;case 5:*control=true;return 2;
        case 6:case 7:*control=true;return 0;default:break;}break;
    case 0xdb:
        switch(reg){case 0:return 4;case 2:case 3:case 7:return 0;case 5:return 10;default:break;}break;
    case 0xdd:
        switch(reg){case 0:return 8;case 2:case 3:return 0;
        case 4:*control=true;return 94;case 6:case 7:*control=true;return 0;default:break;}break;
    case 0xdf:
        switch(reg){case 0:return 2;case 2:case 3:case 6:case 7:return 0;
        case 4:return 10;case 5:return 8;default:break;}break;
    default:break;
    }
    *valid=false;return 0;
}
static i8087_result memory_instruction(transaction *t,uint8_t esc,unsigned reg) {
    i8087_f80 x;
    unsigned i,width;
    if(!(esc&1)) {
        if(esc==0xd8||esc==0xdc)x=sf_load_ieee(get_le(t->bytes,esc==0xd8?4:8),esc==0xd8?32:64,&t->raised);
        else x=sf_from_signed_bits(get_le(t->bytes,esc==0xda?4:2),esc==0xda?32:16);
        if(reg==2||reg==3)compare(t,read_st(t,0),x,reg==3?1:0);
        else arithmetic(t,reg,0,read_st(t,0),x,false);
        return I8087_OK;
    }
    if(esc==0xd9) {
        switch(reg) {
        case 0:x=sf_load_ieee(get_le(t->bytes,4),32,&t->raised);push(t,x);break;
        case 2:case 3:store_ieee(t,read_st(t,0),32,reg==3);break;
        case 4:decode_env(&t->f,t->bytes);break;
        case 5:t->f.control=(uint16_t)(get16(t->bytes)&0x1fbf);break;
        case 6:encode_env(&t->f,t->bytes);t->write_size=14;t->f.control|=EXCEPTIONS;break;
        case 7:put_le(t->bytes,t->f.control,2);t->write_size=2;break;
        default:return I8087_BAD_OPCODE;
        }
    } else if(esc==0xdb) {
        switch(reg) {
        case 0:push(t,sf_from_signed_bits(get_le(t->bytes,4),32));break;
        case 2:case 3:store_integer(t,read_st(t,0),32,reg==3);break;
        case 5:push(t,i8087_decode_f80(t->bytes));break;
        case 7:i8087_encode_f80(t->bytes,read_st(t,0));t->write_size=10;pop(t);break;
        default:return I8087_BAD_OPCODE;
        }
    } else if(esc==0xdd) {
        switch(reg) {
        case 0:x=sf_load_ieee(get_le(t->bytes,8),64,&t->raised);push(t,x);break;
        case 2:case 3:store_ieee(t,read_st(t,0),64,reg==3);break;
        case 4:
            decode_env(&t->f,t->bytes);
            for(i=0;i<8;++i)t->f.registers[physical(&t->f,i)]=i8087_decode_f80(t->bytes+14+10*i);
            break;
        case 6:
            encode_env(&t->f,t->bytes);
            for(i=0;i<8;++i)i8087_encode_f80(t->bytes+14+10*i,t->f.registers[physical(&t->f,i)]);
            t->write_size=94;software_init(&t->f);break;
        case 7:put_le(t->bytes,t->f.status,2);t->write_size=2;break;
        default:return I8087_BAD_OPCODE;
        }
    } else {
        switch(reg) {
        case 0:case 5:width=reg==0?16:64;push(t,sf_from_signed_bits(get_le(t->bytes,width/8),width));break;
        case 2:case 3:store_integer(t,read_st(t,0),16,reg==3);break;
        case 4:load_bcd(t,t->bytes);break;
        case 6:store_bcd(t,read_st(t,0));break;
        case 7:store_integer(t,read_st(t,0),64,true);break;
        default:return I8087_BAD_OPCODE;
        }
    }
    return I8087_OK;
}
static void extract(transaction *t) {
    i8087_f80 x=read_st(t,0),exponent,significand=x;
    enum sf_kind k=sf_kind(x);
    if(k==SF_INFINITY||k==SF_NAN) {
        /* Exact paired exceptional outputs remain a hardware-vector gap. */
        gap(t,I8087_GAP_UNDEFINED_OPERAND);
        t->raised|=I8087_IE;exponent=sf_indefinite();significand=k==SF_NAN?x:sf_indefinite();
    } else if(!x.significand&&!(x.sign_exponent&0x7fff)) {
        /* Original 8087, not later x87: two zeros, BOTH with input sign. */
        exponent=significand=x;
    } else {
        /* FXTRACT separates fields; it does NOT normalize an unnormal first.
           Intel iAPX 86/88 User's Manual, Numerics Supplement, S-35. */
        int32_t e=(int32_t)(x.sign_exponent&0x7fff)-16383;
        exponent=sf_from_u64((uint64_t)(e<0?-e:e),e<0);
        significand.sign_exponent=(uint16_t)(0x3fff|(sf_sign(x)?0x8000:0));
    }
    set_st(&t->f,0,exponent);push(t,significand);
}
static void remainder_instruction(transaction *t) {
    i8087_f80 a=read_st(t,0),b=read_st(t,1),r;
    uint8_t q=0;bool partial=false;unsigned width=0;
    check_denormal(t,a);check_denormal(t,b);
    if(sf_kind(a)==SF_NAN||sf_kind(b)==SF_NAN) {
        sf_result n=sf_add(a,b,64,0);t->raised|=I8087_IE;r=n.value;
        t->f.status&=(uint16_t)~I8087_C2;
    } else if(sf_kind(a)==SF_INFINITY||sf_kind(b)==SF_ZERO||sf_unnormal(b)||sf_denormal(b)) {
        t->raised|=I8087_IE;r=sf_indefinite();
        t->f.status&=(uint16_t)~I8087_C2;
    } else {
        r=sf_remainder(a,b,&q,&partial,&width);
        if(partial) {
            /* This bounded reduction is not the original microcode sequence. */
            gap(t,I8087_GAP_PARTIAL_REMAINDER);t->f.status|=I8087_C2;
        } else {
            /* Intel 8087 datasheet Table 4b: for quotients below 4, old
               condition bits shift INTO the result. Modern x87 differs. */
            unsigned old=((t->f.status&I8087_C0)?4u:0u)|
                         ((t->f.status&I8087_C3)?2u:0u)|
                         ((t->f.status&I8087_C1)?1u:0u);
            if(width<3)q=(uint8_t)(((old<<width)|q)&7u);
            t->f.status&=(uint16_t)~CC;
            if(q&4)t->f.status|=I8087_C0;
            if(q&1)t->f.status|=I8087_C1;
            if(q&2)t->f.status|=I8087_C3;
        }
        if(sf_denormal(r)) {
            t->raised|=I8087_UE;gap(t,I8087_GAP_RANGE_EXCEPTION);
        }
    }
    set_st(&t->f,0,r);
}
static void scale_instruction(transaction *t) {
    i8087_f80 x,y;
    uint16_t flags=0;bool up=false;uint64_t bits;int32_t delta;
    sf_result r;
    /* The original microcode tests the tags BEFORE inspecting either value.
       In particular, 0 scaled by an empty register is a no-op, and an empty
       ST(0) scaled by zero is also a no-op. See SOURCES.md, FSCALE research. */
    if(i8087_tag(&t->f,0)==I8087_TAG_ZERO || i8087_tag(&t->f,1)==I8087_TAG_ZERO)return;
    x=read_st(t,0);y=read_st(t,1);
    check_denormal(t,x);check_denormal(t,y);
    if(sf_kind(x)==SF_NAN||sf_kind(y)==SF_NAN)r=sf_add(x,y,64,0);
    else if(sf_kind(y)==SF_INFINITY)r=(sf_result){sf_indefinite(),I8087_IE,false,{0}};
    else {
        bits=sf_to_integer(y,32,3,&flags,&up);
        if((flags&I8087_IE)||sf_unnormal(y)||sf_denormal(y)) {
            gap(t,I8087_GAP_UNDEFINED_OPERAND);t->always_stop=true;return;
        }
        {
            uint64_t magnitude=sf_sign(y)?UINT64_C(0)-bits:bits;
            /* Original FSCALE has an undefined result for 0 < |scale| < 1.
               Do not quietly substitute modern x87's truncated-zero rule. */
            if(magnitude>32767 || !magnitude){
                gap(t,I8087_GAP_UNDEFINED_OPERAND);t->always_stop=true;return;
            }
            delta=sf_sign(y)?-(int32_t)magnitude:(int32_t)magnitude;
        }
        if(sf_kind(x)==SF_INFINITY)r=(sf_result){x,0,false,{0}};
        else {
            int32_t biased=(int32_t)(x.sign_exponent&0x7fff);
            if(!biased)biased=1; /* denormal -> equivalent unnormal */
            biased+=delta;
            if(biased>0&&biased<0x7fff) {
                /* Adjust the exponent ONLY: lost significance is retained. */
                x.sign_exponent=(uint16_t)(biased|(sf_sign(x)?0x8000:0));
                r=(sf_result){x,0,false,{0}};
            } else {
                if(sf_unnormal(x)||sf_denormal(x))gap(t,I8087_GAP_RANGE_EXCEPTION);
                r=sf_scale(x,delta,rounding(t));
            }
        }
    }
    accept_numeric(t,&r);set_st(&t->f,0,r.value);
}
static i8087_result special_instruction(transaction *t,uint8_t m) {
    static const i8087_f80 constants[7]={
        {UINT64_C(0x8000000000000000),0x3fff},
        {UINT64_C(0xd49a784bcd1b8afe),0x4000},
        {UINT64_C(0xb8aa3b295c17f0bc),0x3fff},
        {UINT64_C(0xc90fdaa22168c235),0x4000},
        {UINT64_C(0x9a209a84fbcff799),0x3ffd},
        {UINT64_C(0xb17217f7d1cf79ac),0x3ffe},
        {0,0}
    };
    i8087_f80 x,y,pushed=sf_zero(false);sf_result r;
    if(m>=0xe8&&m<=0xee) {
        if(m!=0xe8&&m!=0xee)gap(t,I8087_GAP_CONSTANT_ROM);
        push(t,constants[m-0xe8]);return I8087_OK;
    }
    switch(m) {
    case 0xd0:return I8087_OK;
    case 0xe0:case 0xe1:{
        unsigned tag=read_tag(t,0);
        x=read_st(t,0);
        if(m==0xe0)x.sign_exponent^=0x8000;else x.sign_exponent&=0x7fff;
        write_tagged(&t->f,0,x,tag);break;
    }
    case 0xe4:compare(t,read_st(t,0),sf_zero(false),0);break;
    case 0xe5:examine(t);break;
    case 0xf4:extract(t);break;
    case 0xf6:set_top(&t->f,(i8087_top(&t->f)+7)&7);break;
    case 0xf7:set_top(&t->f,(i8087_top(&t->f)+1)&7);break;
    case 0xf8:remainder_instruction(t);break;
    case 0xfa:
        x=read_st(t,0);check_denormal(t,x);
        if(sf_unnormal(x)||sf_denormal(x)||(sf_kind(x)==SF_INFINITY&&!(t->f.control&I8087_AFFINE)))
            r=(sf_result){sf_indefinite(),I8087_IE,false,{0}};
        else r=sf_sqrt(x,precision(t),rounding(t));
        accept_numeric(t,&r);set_st(&t->f,0,r.value);break;
    case 0xfc:
        x=read_st(t,0);check_denormal(t,x);r=sf_round_integer(x,rounding(t));
        accept_numeric(t,&r);set_st(&t->f,0,r.value);break;
    case 0xfd:scale_instruction(t);break;
    case 0xf0:case 0xf1:case 0xf2:case 0xf3:case 0xf9:
        /* F2XM1 #0902 returns on a ZERO tag before signaling precision or
           writing a result. Restrict this exit to canonical signed zeros. */
        if(m==0xf0 && i8087_tag(&t->f,0)==I8087_TAG_ZERO &&
           i8087_get_st(&t->f,0,&x) && !x.significand && !(x.sign_exponent&0x7fff))break;
        /* Die-decoded FPTAN #1039..1060: zero and positive normal angles
           with exponent <= -64 bypass CORDIC and return the original angle
           plus denominator 1. PC and RC do not round these raw transfers. */
        if(m==0xf2 && i8087_get_st(&t->f,0,&x) &&
           ((!x.significand&&!(x.sign_exponent&0x7fff)) ||
            (!sf_sign(x) && sf_kind(x)==SF_FINITE && !sf_unnormal(x) &&
             !sf_denormal(x) && (x.sign_exponent&0x7fff)<=0x3fbf))) {
            if(x.significand)t->raised|=I8087_PE;
            push(t,constants[0]);break;
        }
        gap(t,I8087_GAP_TRANSCENDENTAL);
        x=read_st(t,0);y=(m==0xf1||m==0xf3||m==0xf9)?read_st(t,1):sf_zero(false);
        if(!sf_transcend(m,x,y,&r,&pushed)){gap(t,I8087_GAP_UNDEFINED_OPERAND);t->always_stop=true;break;}
        accept_numeric(t,&r);
        if(m==0xf1||m==0xf3||m==0xf9){set_st(&t->f,1,r.value);pop(t);}
        else {set_st(&t->f,0,r.value);if(m==0xf2)push(t,pushed);}
        break;
    default:return I8087_BAD_OPCODE;
    }
    return I8087_OK;
}
static bool valid_register(uint8_t esc,uint8_t m) {
    unsigned group=(m>>3)&7;
    switch(esc) {
    case 0xd8:return true;
    case 0xd9:
        return m<=0xcf||m==0xd0||m==0xe0||m==0xe1||m==0xe4||m==0xe5||
               (m>=0xe8&&m<=0xee)||(m>=0xf0&&m<=0xf4)||
               (m>=0xf6&&m<=0xfa)||m==0xfc||m==0xfd;
    case 0xdb:return m>=0xe0&&m<=0xe3;
    case 0xdc:return group!=2&&group!=3;
    case 0xdd:return group==0||group==2||group==3;
    case 0xde:return group==0||group==1||group>=4||m==0xd9;
    default:return false;
    }
}
static i8087_result register_instruction(transaction *t,uint8_t esc,uint8_t m) {
    unsigned group=(m>>3)&7,i=m&7,op=group;
    i8087_f80 a,b;
    if(esc==0xdb) {
        switch(m) {
        case 0xe0:t->f.control&=(uint16_t)~I8087_INTERRUPT_MASK;break;
        case 0xe1:t->f.control|=I8087_INTERRUPT_MASK;break;
        case 0xe2:t->f.status&=(uint16_t)(UINT16_MAX^(EXCEPTIONS|I8087_IR|I8087_BUSY));break;
        case 0xe3:software_init(&t->f);break;
        default:return I8087_BAD_OPCODE;
        }
        return I8087_OK;
    }
    if(esc==0xd9) {
        if(m<=0xc7){unsigned tag=read_tag(t,i);a=read_st(t,i);push_tagged(t,a,tag);return I8087_OK;}
        if(m<=0xcf){
            unsigned ta=read_tag(t,0),tb=read_tag(t,i);
            a=read_st(t,0);b=read_st(t,i);
            write_tagged(&t->f,0,b,tb);write_tagged(&t->f,i,a,ta);return I8087_OK;
        }
        return special_instruction(t,m);
    }
    if(esc==0xdd) {
        if(group==0)set_tag(&t->f,physical(&t->f,i),I8087_TAG_EMPTY);
        else {unsigned tag=read_tag(t,0);a=read_st(t,0);write_tagged(&t->f,i,a,tag);if(group==3)pop(t);}
        return I8087_OK;
    }
    if(esc==0xde&&m==0xd9){a=read_st(t,0);b=read_st(t,1);compare(t,a,b,2);return I8087_OK;}
    a=read_st(t,0);b=read_st(t,i);
    if(esc==0xd8) {
        if(group==2||group==3)compare(t,a,b,group==3?1:0);
        else arithmetic(t,op,0,a,b,false);
    } else {
        /* DC/DE reverse the subtraction/division encodings relative to D8. */
        if(group>=4)op=group^1u;
        arithmetic(t,op,i,b,a,esc==0xde);
    }
    return I8087_OK;
}
i8087_result i8087_execute(i8087 *f,const i8087_bus *bus,
                          uint8_t esc,uint8_t m,uint32_t ip,uint32_t ea) {
    transaction t;
    bool memory=m<0xc0,valid=true,control=false;
    size_t n=0;
    i8087_result result;
    uint16_t unmasked;
    if(!f||esc<0xd8||esc>0xdf)return I8087_BAD_ARGUMENT;
    if(memory) n=memory_read_size(esc,(m>>3)&7,&valid,&control);
    else {valid=valid_register(esc,m);control=esc==0xdb;}
    if(!valid)return I8087_BAD_OPCODE;
    /* ESC execution and WAIT are separate. An already latched interrupt
       does not prevent no-WAIT instructions from executing. A WAIT that the
       host has not yet serviced does hold the CPU at the synchronization point. */
    if(f->cpu_waiting&&(f->status&I8087_IR)&&!control)return I8087_PENDING_INTERRUPT;
    memset(&t,0,sizeof(t));t.f=*f;t.f.last_gap=0;t.f.cpu_waiting=false;t.control=control;
    if(!control) {
        t.f.instruction_address=ip&ADDRESS_MASK;
        t.f.opcode=(uint16_t)(((esc&7u)<<8)|m);
        if(memory)t.f.data_address=ea&ADDRESS_MASK;
    }
    if(n) {
        if(!bus||!bus->read||!bus->read(bus->context,ea&ADDRESS_MASK,t.bytes,n))return I8087_MEMORY_FAULT;
    }
    result=memory?memory_instruction(&t,esc,(m>>3)&7):register_instruction(&t,esc,m);
    if(result!=I8087_OK)return result;
    /* A before-operation exception has priority over a would-be result's
       after-operation exceptions. Preserve preexisting sticky flags later. */
    if((t.raised&I8087_DE)&&!(t.f.control&I8087_DE))t.raised=I8087_DE;
    else if(t.raised&I8087_IE)t.raised&=(uint16_t)(I8087_IE|I8087_DE);
    else if(t.raised&I8087_ZE)t.raised&=(uint16_t)(I8087_ZE|I8087_DE);
    unmasked=(uint16_t)(t.raised&~t.f.control&EXCEPTIONS);
    /* A trapping memory range conversion does not proceed to the rounded
       store/precision stage. Register range results are handled separately. */
    if((unmasked&(I8087_OE|I8087_UE))&&t.write_size) {
        t.raised&=(uint16_t)~I8087_PE;
        unmasked=(uint16_t)(t.raised&~t.f.control&EXCEPTIONS);
    }
    if(unmasked&I8087_DE)t.f.last_gap&=(uint32_t)~(I8087_GAP_UNNORMAL|
        I8087_GAP_RANGE_EXCEPTION|I8087_GAP_RESERVED_PRECISION|I8087_GAP_STACK_FAULT);
    /* Register range exceptions require wrapped exponents, not an IEEE
       overflow result. Any path without the wrapped range metadata remains
       guarded. Invalid-comparison continuation also remains guarded. */
    if(((unmasked&(I8087_OE|I8087_UE))&&!t.register_range_result&&!t.write_size) ||
       ((unmasked&I8087_IE)&&t.compare_operation))gap(&t,I8087_GAP_EXCEPTION_RESTART);
    if(t.always_stop||(t.f.policy==I8087_STOP_ON_UNVERIFIED&&t.f.last_gap)) {
        f->last_gap=t.f.last_gap;return I8087_UNVERIFIED;
    }
    if((unmasked&(I8087_IE|I8087_DE|I8087_ZE)) ||
       ((unmasked&(I8087_OE|I8087_UE))&&!t.register_range_result)) {
        /* IE/DE/ZE are before-operation exceptions. An unmasked real store
           range exception likewise suppresses both the store and its pop.
           Unsupported register range handling remains a labeled fallback. */
        memcpy(t.f.registers,f->registers,sizeof(f->registers));t.f.tags=f->tags;
        t.f.status=(uint16_t)((t.f.status&~I8087_TOP)|(f->status&I8087_TOP));
        if(t.compare_operation&&(unmasked&I8087_DE))
            t.f.status=(uint16_t)((t.f.status&~CC)|(f->status&CC));
        t.write_size=0;
    }
    /* PE is AFTER the operation: keep the rounded result, store and pop. */
    t.f.status|=t.raised;
    if(unmasked)t.f.status|=I8087_IR;
    synchronize_busy(&t.f);
    if(t.write_size&&(!bus||!bus->write||!bus->write(bus->context,ea&ADDRESS_MASK,t.bytes,t.write_size)))
        return I8087_MEMORY_FAULT;
    t.f.used_approximations|=t.f.last_gap;*f=t.f;
    return unmasked?I8087_PENDING_INTERRUPT:I8087_OK;
}
const char *i8087_result_string(i8087_result r) {
    switch(r) {
    case I8087_OK:return "ok";
    case I8087_PENDING_INTERRUPT:return "pending 8087 interrupt";
    case I8087_UNVERIFIED:return "unverified hardware behavior; instruction not committed";
    case I8087_BAD_OPCODE:return "not a supported documented 8087 encoding";
    case I8087_MEMORY_FAULT:return "operand memory transfer failed";
    case I8087_BAD_ARGUMENT:return "invalid API argument";
    default:return "unknown result";
    }
}
