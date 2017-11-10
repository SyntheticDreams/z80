
/*  Z80 CPU Simulator.

    Copyright (C) 2017 Ivan Kosarev.
    ivan@kosarev.info

    Published under the MIT license.
*/

#ifndef Z80_H
#define Z80_H

#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace z80 {

#if UINT_FAST8_MAX < UINT_MAX
typedef unsigned fast_u8;
#else
typedef uint_fast8_t fast_u8;
#endif

#if UINT_FAST16_MAX < UINT_MAX
typedef unsigned fast_u16;
#else
typedef uint_fast16_t fast_u16;
#endif

typedef uint_least8_t least_u8;
typedef uint_least16_t least_u16;

typedef uint_fast32_t size_type;

static const fast_u8 mask8 = 0xff;
static const fast_u8 sign8_mask = 0x80;
static const fast_u16 mask16 = 0xffff;

static inline void unused(...) {}

static inline bool get_sign8(fast_u8 n) {
    return (n & sign8_mask) != 0;
}

static inline fast_u8 add8(fast_u8 a, fast_u8 b) {
    return (a + b) & mask8;
}

static inline fast_u8 sub8(fast_u8 a, fast_u8 b) {
    return (a - b) & mask8;
}

static inline fast_u8 inc8(fast_u8 n) {
    return add8(n, 1);
}

static inline fast_u8 dec8(fast_u8 n) {
    return sub8(n, 1);
}

static inline fast_u8 ror8(fast_u8 n) {
    return ((n >> 1) | (n << 7)) & mask8;
}

static inline fast_u8 neg8(fast_u8 n) {
    return ((n ^ mask8) + 1) & mask8;
}

static inline fast_u8 abs8(fast_u8 n) {
    return !get_sign8(n) ? n : neg8(n);
}

static inline int sign_extend8(fast_u8 n) {
    auto a = static_cast<int>(abs8(n));
    return !get_sign8(n) ? a : -a;
}

static inline fast_u8 get_low8(fast_u16 n) {
    return n & mask8;
}

static inline fast_u8 get_high8(fast_u16 n) {
    return (n >> 8) & mask8;
}

static inline fast_u16 make16(fast_u8 hi, fast_u8 lo) {
    return (static_cast<fast_u16>(hi) << 8) | lo;
}

static inline fast_u16 add16(fast_u16 a, fast_u16 b) {
    return (a + b) & mask16;
}

static inline fast_u16 sub16(fast_u16 a, fast_u16 b) {
    return (a - b) & mask16;
}

static inline fast_u16 inc16(fast_u16 n) {
    return add16(n, 1);
}

static inline fast_u16 dec16(fast_u16 n) {
    return sub16(n, 1);
}

enum class reg { b, c, d, e, h, l, at_hl, a };

enum class regp { bc, de, hl, sp };
enum class regp2 { bc, de, hl, af };
enum class index_regp { hl, ix, iy };

enum class instruction_prefix { none, cb, ed };

enum class alu { add, adc, sub, sbc, and_a, xor_a, or_a, cp };
enum class block_ld { ldi, ldd, ldir, lddr };

enum condition { nz, z, nc, c, po, pe, p, m };

template<typename D>
class instructions_decoder {
public:
    instructions_decoder() {}

    index_regp get_index_rp_kind() const { return state.index_rp; }
    index_regp get_next_index_rp_kind() const { return state.next_index_rp; }

    fast_u8 read_disp_or_null(bool may_need_disp = true) {
        if(get_index_rp_kind() == index_regp::hl || !may_need_disp)
            return 0;
        fast_u8 d = (*this)->on_disp_read();
        (*this)->on_5t_exec_cycle();
        return d;
    }

    fast_u8 read_disp_or_null(reg r) {
        return read_disp_or_null(r == reg::at_hl);
    }

    fast_u8 read_disp_or_null(reg r1, reg r2) {
        return read_disp_or_null(r1 == reg::at_hl || r2 == reg::at_hl);
    }

    instruction_prefix get_prefix() const { return state.prefix; }
    void set_prefix(instruction_prefix p) { state.prefix = p; }

    void on_ed_prefix() {
        // TODO: Should we reset the index register pair here?
        set_prefix(instruction_prefix::ed);
    }

    void on_cb_prefix() {
        set_prefix(instruction_prefix::cb);
        state.next_index_rp = state.index_rp;
    }

    void on_prefix_reset() {
        set_prefix(instruction_prefix::none);
    }

    void on_set_next_index_rp(index_regp irp) {
        // TODO: Should we reset the prefix here?
        state.next_index_rp = irp;
    }

    unsigned decode_int_mode(fast_u8 y) {
        y &= 3;
        return y < 2 ? 0 : y - 1;
    }

    void decode_unprefixed() {
        fast_u8 op = (*this)->on_fetch();
        fast_u8 y = get_y_part(op);
        fast_u8 z = get_z_part(op);
        fast_u8 p = get_p_part(op);

        switch(op & x_mask) {
        case 0100: {
            // LD r[y], r[z] or HALT (in place of LD (HL), (HL))
            // LD r, r              f(4)
            // LD r, (HL)           f(4)           r(3)
            // LD r, (i+d)     f(4) f(4) r(3) e(5) r(3)
            // LD (HL), r           f(4)           w(3)
            // LD (i+d), r     f(4) f(4) r(3) e(5) w(3)
            // HALT                 f(4)
            auto rd = static_cast<reg>(y);
            auto rs = static_cast<reg>(z);
            if(rd == reg::at_hl && rs == reg::at_hl)
                assert(0);  // TODO: return (*this)->on_halt();
            return (*this)->on_ld_r_r(rd, rs, read_disp_or_null(rd, rs)); }
        case 0200: {
            // alu[y] r[z]
            // alu r            f(4)
            // alu (HL)         f(4)           r(3)
            // alu (i+d)   f(4) f(4) r(3) e(5) r(3)
            auto k = static_cast<alu>(y);
            auto r = static_cast<reg>(z);
            return (*this)->on_alu_r(k, r, read_disp_or_null(r)); }
        }
        switch(op & (x_mask | z_mask)) {
        case 0004: {
            // INC r[y]
            // INC r            f(4)
            // INC (HL)         f(4)           r(4) w(3)
            // INC (i+d)   f(4) f(4) r(3) e(5) r(4) w(3)
            auto r = static_cast<reg>(y);
            return (*this)->on_inc_r(r, read_disp_or_null(r)); }
        case 0005: {
            // DEC r[y]
            // DEC r            f(4)
            // DEC (HL)         f(4)           r(4) w(3)
            // DEC (i+d)   f(4) f(4) r(3) e(5) r(4) w(3)
            auto r = static_cast<reg>(y);
            return (*this)->on_dec_r(r, read_disp_or_null(r)); }
        case 0006: {
            // LD r[y], n
            // LD r, n              f(4)      r(3)
            // LD (HL), n           f(4)      r(3) w(3)
            // LD (i+d), n     f(4) f(4) r(3) r(5) w(3)
            auto r = static_cast<reg>(y);
            fast_u8 d, n;
            if(r != reg::at_hl || get_index_rp_kind() == index_regp::hl) {
                d = 0;
                n = (*this)->on_3t_imm8_read();
            } else {
                d = (*this)->on_disp_read();
                n = (*this)->on_5t_imm8_read();
            }
            return (*this)->on_ld_r_n(r, d, n); }
        case 0300: {
            // RET cc[y]  f(5) + r(3) r(3)
            (*this)->on_5t_fetch_cycle();
            auto cc = static_cast<condition>(y);
            return (*this)->on_ret_cc(cc); }
        case 0306: {
            // alu[y] n  f(4) r(3)
            auto k = static_cast<alu>(y);
            return (*this)->on_alu_n(k, (*this)->on_3t_imm8_read()); }
        }
        if((op & (x_mask | z_mask | (y_mask - 0030))) == 0040) {
            // JR cc[y-4], d  f(4) r(3) + e(5)
            auto cc = static_cast<condition>((op & (y_mask - 0040)) >> 3);
            return (*this)->on_jr_cc(cc, (*this)->on_disp_read());
        }
        switch(op & (x_mask | z_mask | q_mask)) {
        case 0001: {
            // LD rp[p], nn
            // LD rr, nn        f(4) r(3) r(3)
            // LD i, nn    f(4) f(4) r(3) r(3)
            auto rp = static_cast<regp>(p);
            return (*this)->on_ld_rp_nn(rp, (*this)->on_3t_3t_imm16_read()); }
        case 0003: {
            // INC rp[p]
            // INC rr           f(6)
            // INC i       f(4) f(6)
            (*this)->on_6t_fetch_cycle();
            auto rp = static_cast<regp>(p);
            return (*this)->on_inc_rp(rp); }
        case 0011: {
            // ADD HL, rp[p]
            // ADD HL, rr           f(4) e(4) e(3)
            // ADD i, rr       f(4) f(4) e(4) e(3)
            auto rp = static_cast<regp>(p);
            return (*this)->on_add_irp_rp(rp); }
        case 0013: {
            // DEC rp[p]
            // DEC rr           f(6)
            // DEC i       f(4) f(6)
            (*this)->on_6t_fetch_cycle();
            auto rp = static_cast<regp>(p);
            return (*this)->on_dec_rp(rp); }
        case 0301: {
            // POP rp2[p]
            // POP rr           f(4) r(3) r(3)
            // POP i       f(4) f(4) r(3) r(3)
            auto rp = static_cast<regp2>(p);
            return (*this)->on_pop_rp(rp); }
        case 0305: {
            // PUSH rp2[p]
            // PUSH rr          f(5) w(3) w(3)
            // PUSH i      f(4) f(5) w(3) w(3)
            (*this)->on_5t_fetch_cycle();
            auto rp = static_cast<regp2>(p);
            return (*this)->on_push_rp(rp); }
        }
        switch(op) {
        case 0x00:
            return (*this)->on_nop();
        case 0x0f:
            // RRCA  f(4)
            return (*this)->on_rrca();
        case 0x10:
            // DJNZ  f(5) r(3) + e(5)
            (*this)->on_5t_fetch_cycle();
            return (*this)->on_djnz((*this)->on_disp_read());
        case 0x18:
            // JR d  f(4) r(3) e(5)
            return (*this)->on_jr((*this)->on_disp_read());
        case 0x22:
            // LD (nn), HL          f(4) r(3) r(3) w(3) w(3)
            // LD (nn), i      f(4) f(4) r(3) r(3) w(3) w(3)
            return (*this)->on_ld_at_nn_irp((*this)->on_3t_3t_imm16_read());
        case 0x2a:
            // LD HL, (nn)          f(4) r(3) r(3) r(3) r(3)
            // LD i, (nn)      f(4) f(4) r(3) r(3) r(3) r(3)
            return (*this)->on_ld_irp_at_nn((*this)->on_3t_3t_imm16_read());
        case 0x32:
            // LD (nn), A  f(4) r(3) r(3) w(3)
            return (*this)->on_ld_at_nn_a((*this)->on_3t_3t_imm16_read());
        case 0x37:
            // SCF  f(4)
            return (*this)->on_scf();
        case 0x3a:
            // LD A, (nn)  f(4) r(3) r(3) r(3)
            return (*this)->on_ld_a_at_nn((*this)->on_3t_3t_imm16_read());
        case 0x3f:
            // CCF  f(4)
            return (*this)->on_ccf();
        case 0xc3:
            // JP nn  f(4) r(3) r(3)
            return (*this)->on_jp_nn((*this)->on_3t_3t_imm16_read());
        case 0xc9:
            // RET  f(4) r(3) r(3)
            return (*this)->on_ret();
        case 0xcb:
            // CB prefix.
            return (*this)->on_cb_prefix();
        case 0xcd:
            // CALL nn  f(4) r(3) r(4) w(3) w(3)
            return (*this)->on_call_nn((*this)->on_3t_4t_imm16_read());
        case 0xd3:
            // OUT (n), A  f(4) r(3) o(4)
            return (*this)->on_out_n_a((*this)->on_3t_imm8_read());
        case 0xd9:
            // EXX  f(4)
            return (*this)->on_exx();
        case 0xe9:
            // JP HL            f(4)
            // JP i        f(4) f(4)
            return (*this)->on_jp_irp();
        case 0xeb:
            // EX DE, HL  f(4)
            return (*this)->on_ex_de_hl();
        case 0xed:
            // ED prefix.
            return (*this)->on_ed_prefix();
        case 0xf3:
            // DI  f(4)
            return (*this)->on_di();
        case 0xf9:
            // LD SP, HL        f(6)
            // LD SP, i    f(4) f(6)
            (*this)->on_6t_fetch_cycle();
            return (*this)->on_ld_sp_irp();
        case 0xfb:
            // EI  f(4)
            return (*this)->on_ei();
        case 0xfd:
            // FD prefix (IY-indexed instructions).
            return (*this)->on_set_next_index_rp(index_regp::iy);
        }

        // TODO
        std::fprintf(stderr, "Unknown opcode 0x%02x at 0x%04x.\n",
                     static_cast<unsigned>(op),
                     static_cast<unsigned>((*this)->get_last_read_addr()));
        std::abort();
    }

    void decode_cb_prefixed() {
        prefix_reset_guard guard(this);

        fast_u8 d = 0;
        index_regp irp = get_index_rp_kind();
        if(irp != index_regp::hl)
            d = (*this)->on_disp_read();

        fast_u8 op = (*this)->on_fetch();
        if(irp != index_regp::hl)
            (*this)->on_5t_fetch_cycle();

        fast_u8 y = get_y_part(op);
        fast_u8 z = get_z_part(op);

        auto b = static_cast<unsigned>(y);
        auto r = static_cast<reg>(z);

        switch(op & x_mask) {
        case 0100:
            // BIT y, r[z]
            // BIT b, r             f(4)      f(4)
            // BIT b, (HL)          f(4)      f(4) r(4)
            // BIT b, (i+d)    f(4) f(4) r(3) f(5) r(4) */
            return (*this)->on_bit(b, r, d);
        case 0200:
            // RES y, r[z]
            // RES b, r             f(4)      f(4)
            // RES b, (HL)          f(4)      f(4) r(4) w(3)
            // RES b, (i+d)    f(4) f(4) r(3) f(5) r(4) w(3)
            return (*this)->on_res(b, r, d);
        case 0300:
            // SET y, r[z]
            // SET b, r             f(4)      f(4)
            // SET b, (HL)          f(4)      f(4) r(4) w(3)
            // SET b, (i+d)    f(4) f(4) r(3) f(5) r(4) w(3)
            return (*this)->on_set(b, r, d);
        }

        std::fprintf(stderr, "Unknown CB-prefixed opcode 0x%02x at 0x%04x.\n",
                     static_cast<unsigned>(op),
                     static_cast<unsigned>((*this)->get_last_read_addr()));
        std::abort();
    }

    void decode_ed_prefixed() {
        prefix_reset_guard guard(this);
        fast_u8 op = (*this)->on_fetch();
        fast_u8 y = get_y_part(op);
        fast_u8 p = get_p_part(op);

        switch(op & (x_mask | z_mask)) {
        case 0102: {
            // ADC HL, rp[p]  f(4) f(4) e(4) e(3)
            // SBC HL, rp[p]  f(4) f(4) e(4) e(3)
            auto rp = static_cast<regp>(p);
            return op & q_mask ? (*this)->on_adc_hl_rp(rp) :
                                 (*this)->on_sbc_hl_rp(rp); }
        case 0103: {
            // LD rp[p], (nn)  f(4) f(4) r(3) r(3) r(3) r(3)
            // LD (nn), rp[p]  f(4) f(4) r(3) r(3) w(3) w(3)
            auto rp = static_cast<regp>(p);
            fast_u16 nn = (*this)->on_3t_3t_imm16_read();
            return op & q_mask ? (*this)->on_ld_rp_at_nn(rp, nn) :
                                 (*this)->on_ld_at_nn_rp(nn, rp); }
        case 0106: {
            // IM im[y]  f(4) f(4)
            return (*this)->on_im(decode_int_mode(y)); }
        case 0200: {
            // LDI, LDD, LDIR, LDDR  f(4) f(4) r(3) w(5) + e(5)
            if(y < 4)
                return (*this)->on_noni_ed(op);
            auto k = static_cast<block_ld>(y - 4);
            return (*this)->on_block_ld(k); }
        }
        switch(op) {
        case 0x47: {
            // LD I, A  f(4) f(5)
            (*this)->on_5t_fetch_cycle();
            return (*this)->on_ld_i_a(); }
        }

        std::fprintf(stderr, "Unknown ED-prefixed opcode 0x%02x at 0x%04x.\n",
                     static_cast<unsigned>(op),
                     static_cast<unsigned>((*this)->get_last_read_addr()));
        std::abort();
    }

    void on_decode() {
        state.index_rp = state.next_index_rp;
        state.next_index_rp = index_regp::hl;

        switch(get_prefix()) {
        case instruction_prefix::none:
            return decode_unprefixed();
        case instruction_prefix::cb:
            return decode_cb_prefixed();
        case instruction_prefix::ed:
            return decode_ed_prefixed();
        }
        assert(0);
    }

    void decode() { (*this)->on_decode(); }

protected:
    class prefix_reset_guard {
    public:
        prefix_reset_guard(instructions_decoder *decoder)
            : decoder(decoder)
        {}

        ~prefix_reset_guard() {
            (*decoder)->on_prefix_reset();
        }

    private:
        instructions_decoder *decoder;
    };

    D *operator -> () { return static_cast<D*>(this); }

    static const fast_u8 x_mask = 0300;

    static const fast_u8 y_mask = 0070;
    fast_u8 get_y_part(fast_u8 op) { return (op & y_mask) >> 3; }

    static const fast_u8 z_mask = 0007;
    fast_u8 get_z_part(fast_u8 op) { return op & z_mask; }

    static const fast_u8 p_mask = 0060;
    fast_u8 get_p_part(fast_u8 op) { return (op & p_mask) >> 4; }

    static const fast_u8 q_mask = 0010;

private:
    struct decoder_state {
        decoder_state()
            : index_rp(index_regp::hl), next_index_rp(index_regp::hl),
              prefix(instruction_prefix::none)
        {}

        index_regp index_rp;
        index_regp next_index_rp;
        instruction_prefix prefix;
    };

    decoder_state state;
};

const char *get_reg_name(reg r);
const char *get_reg_name(regp rp, index_regp irp = index_regp::hl);
const char *get_reg_name(regp2 rp, index_regp irp = index_regp::hl);
const char *get_reg_name(index_regp irp);
const char *get_mnemonic(alu k);
const char *get_mnemonic(block_ld k);
bool is_two_operand_alu_instr(alu k);
const char *get_index_reg_name(index_regp irp);
const char *get_condition_name(condition cc);

class disassembler_base {
public:
    disassembler_base() {}
    virtual ~disassembler_base() {}

    virtual void on_output(const char *out) = 0;

    void on_format(const char *fmt) {
        on_format_impl(fmt, /* args= */ nullptr);
    }

    template<typename... types>
    void on_format(const char *fmt, const types &... args) {
        const void *ptrs[] = { static_cast<const void*>(&args)... };
        on_format_impl(fmt, ptrs);
    }

    virtual void on_format_impl(const char *fmt, const void *args[]);
};

template<typename D>
class disassembler : public instructions_decoder<D>,
                     public disassembler_base {
public:
    typedef instructions_decoder<D> decoder;

    disassembler() {}

    fast_u8 on_fetch() { return (*this)->on_read(); }
    void on_5t_fetch_cycle() {}
    void on_6t_fetch_cycle() {}

    fast_u8 on_3t_imm8_read() { return (*this)->on_read(); }
    fast_u8 on_5t_imm8_read() { return (*this)->on_read(); }

    fast_u16 on_3t_3t_imm16_read() {
        fast_u8 lo = (*this)->on_read();
        fast_u8 hi = (*this)->on_read();
        return make16(hi, lo); }
    fast_u16 on_3t_4t_imm16_read() {
        fast_u8 lo = (*this)->on_read();
        fast_u8 hi = (*this)->on_read();
        return make16(hi, lo); }

    fast_u8 on_disp_read() { return (*this)->on_read(); }

    void on_3t_exec_cycle() {}
    void on_4t_exec_cycle() {}
    void on_5t_exec_cycle() {}

    void on_ed_prefix() { decoder::on_ed_prefix();
                          (*this)->on_format("noni 0xed"); }

    void on_noni_ed(fast_u8 op) {
        (*this)->on_format("noni N, N", 0xed, op); }

    void on_add_irp_rp(regp rp) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("add P, P", regp::hl, irp, rp, irp); }
    void on_adc_hl_rp(regp rp) {
        (*this)->on_format("adc hl, P", rp, index_regp::hl); }
    void on_alu_n(alu k, fast_u8 n) {
        (*this)->on_format("A N", k, n); }
    void on_alu_r(alu k, reg r, fast_u8 d) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("A R", k, r, irp, d); }
    void on_block_ld(block_ld k) {
        (*this)->on_format("L", k); }
    void on_bit(unsigned b, reg r, fast_u8 d) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("bit U, R", b, r, irp, d); }
    void on_call_nn(fast_u16 nn) {
        (*this)->on_format("call W", nn); }
    void on_ccf() {
        (*this)->on_format("ccf"); }
    void on_dec_r(reg r, fast_u8 d) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("dec R", r, irp, d); }
    void on_dec_rp(regp rp) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("dec P", rp, irp); }
    void on_di() {
        (*this)->on_format("di"); }
    void on_djnz(fast_u8 d) {
        (*this)->on_format("djnz D", sign_extend8(d) + 2); }
    void on_ei() {
        (*this)->on_format("ei"); }
    void on_ex_de_hl() {
        (*this)->on_format("ex de, hl"); }
    void on_exx() {
        (*this)->on_format("exx"); }
    void on_im(unsigned mode) {
        (*this)->on_format("im U", mode); }
    void on_inc_r(reg r, fast_u8 d) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("inc R", r, irp, d); }
    void on_inc_rp(regp rp) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("inc P", rp, irp); }
    void on_jp_irp() {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("jp (P)", regp::hl, irp); }
    void on_jp_nn(fast_u16 nn) {
        (*this)->on_format("jp W", nn); }
    void on_jr(fast_u8 d) {
        (*this)->on_format("jr D", sign_extend8(d) + 2); }
    void on_jr_cc(condition cc, fast_u8 d) {
        (*this)->on_format("jr C, D", cc, sign_extend8(d) + 2); }
    void on_ld_i_a() {
        (*this)->on_format("ld i, a"); }
    void on_ld_r_r(reg rd, reg rs, fast_u8 d) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("ld R, R", rd, irp, d, rs, irp, d); }
    void on_ld_r_n(reg r, fast_u8 d, fast_u8 n) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("ld R, N", r, irp, d, n); }
    void on_ld_rp_nn(regp rp, fast_u16 nn) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("ld P, W", rp, irp, nn); }
    void on_ld_irp_at_nn(fast_u16 nn) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("ld P, (W)", regp::hl, irp, nn); }
    void on_ld_at_nn_irp(fast_u16 nn) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("ld (W), P", nn, regp::hl, irp); }
    void on_ld_rp_at_nn(regp rp, fast_u16 nn) {
        (*this)->on_format("ld P, (W)", rp, index_regp::hl, nn); }
    void on_ld_at_nn_rp(fast_u16 nn, regp rp) {
        (*this)->on_format("ld (W), P", nn, rp, index_regp::hl); }
    void on_ld_a_at_nn(fast_u16 nn) {
        (*this)->on_format("ld a, (W)", nn); }
    void on_ld_at_nn_a(fast_u16 nn) {
        (*this)->on_format("ld (W), a", nn); }
    void on_ld_sp_irp() {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("ld sp, P", regp::hl, irp); }
    void on_nop() {
        (*this)->on_format("nop"); }
    void on_out_n_a(fast_u8 n) {
        (*this)->on_format("out (N), a", n); }
    void on_pop_rp(regp2 rp) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("pop G", rp, irp); }
    void on_push_rp(regp2 rp) {
        index_regp irp = (*this)->get_index_rp_kind();
        (*this)->on_format("push G", rp, irp); }
    void on_res(unsigned b, reg r, fast_u8 d) {
        index_regp irp = (*this)->get_index_rp_kind();
        if(irp == index_regp::hl || r == reg::at_hl)
            (*this)->on_format("res U, R", b, reg::at_hl, irp, d);
        else
            (*this)->on_format("res U, R, R", b, reg::at_hl, irp, d,
                               r, index_regp::hl, 0); }
    void on_ret() {
        (*this)->on_format("ret"); }
    void on_ret_cc(condition cc) {
        (*this)->on_format("ret C", cc); }
    void on_rrca() {
        (*this)->on_format("rrca"); }
    void on_scf() {
        (*this)->on_format("scf"); }
    void on_set(unsigned b, reg r, fast_u8 d) {
        index_regp irp = (*this)->get_index_rp_kind();
        if(irp == index_regp::hl || r == reg::at_hl)
            (*this)->on_format("set U, R", b, reg::at_hl, irp, d);
        else
            (*this)->on_format("set U, R, R", b, reg::at_hl, irp, d,
                               r, index_regp::hl, 0); }
    void on_sbc_hl_rp(regp rp) {
        (*this)->on_format("sbc hl, P", rp, index_regp::hl); }

    void disassemble() { (*this)->decode(); }

protected:
    D *operator -> () { return static_cast<D*>(this); }
};

class processor_base {
public:
    processor_base() {}

protected:
    static const unsigned sf_bit = 7;
    static const unsigned zf_bit = 6;
    static const unsigned yf_bit = 5;
    static const unsigned hf_bit = 4;
    static const unsigned xf_bit = 3;
    static const unsigned pf_bit = 2;
    static const unsigned nf_bit = 1;
    static const unsigned cf_bit = 0;

    static const fast_u8 sf_mask = 1 << sf_bit;
    static const fast_u8 zf_mask = 1 << zf_bit;
    static const fast_u8 yf_mask = 1 << yf_bit;
    static const fast_u8 hf_mask = 1 << hf_bit;
    static const fast_u8 xf_mask = 1 << xf_bit;
    static const fast_u8 pf_mask = 1 << pf_bit;
    static const fast_u8 nf_mask = 1 << nf_bit;
    static const fast_u8 cf_mask = 1 << cf_bit;

    template<typename T>
    fast_u8 zf_ari(T n) {
        return (n == 0 ? 1u : 0u) << zf_bit;
    }

    template<typename T>
    fast_u8 hf_ari(T r, T a, T b) {
        return (r ^ a ^ b) & hf_mask;
    }

    fast_u8 hf_dec(fast_u8 n) {
        return (n & 0xf) == 0xf ? hf_mask : 0;
    }

    fast_u8 hf_inc(fast_u8 n) {
        return (n & 0xf) == 0x0 ? hf_mask : 0;
    }

    template<typename T>
    fast_u8 pf_ari(T r, T a, T b) {
        fast_u16 x = r ^ a ^ b;
        return ((x >> 6) ^ (x >> 5)) & pf_mask;
    }

    bool pf_log4(fast_u8 n) {
        return 0x9669 & (1 << (n & 0xf));
    }

    fast_u8 pf_log(fast_u8 n) {
        bool lo = pf_log4(n);
        bool hi = pf_log4(n >> 4);
        return lo == hi ? pf_mask : 0;
    }

    fast_u8 pf_dec(fast_u8 n) {
        return n == 0x7f ? pf_mask : 0;
    }

    fast_u8 pf_inc(fast_u8 n) {
        return n == 0x80 ? pf_mask : 0;
    }

    fast_u8 cf_ari(bool c) {
        return c ? cf_mask : 0;
    }

    struct processor_state {
        processor_state()
            : last_read_addr(0), disable_int(false),
              bc(0), de(0), hl(0), af(0), ix(0), iy(0),
              alt_bc(0), alt_de(0), alt_hl(0),
              pc(0), sp(0), ir(0), memptr(0),
              iff1(false), iff2(false), int_mode(0)
        {}

        void ex_de_hl() {
            std::swap(de, hl);
        }

        void exx() {
            std::swap(bc, alt_bc);
            std::swap(de, alt_de);
            std::swap(hl, alt_hl);
        }

        fast_u16 last_read_addr;
        bool disable_int;
        fast_u16 bc, de, hl, af, ix, iy;
        fast_u16 alt_bc, alt_de, alt_hl;
        fast_u16 pc, sp, ir, memptr;
        bool iff1, iff2;
        unsigned int_mode;
    } state;
};

template<typename D>
class processor : public instructions_decoder<D>,
                  public processor_base {
public:
    typedef instructions_decoder<D> decoder;

    processor() {}

    fast_u8 get_b() const { return get_high8(state.bc); }
    void set_b(fast_u8 b) { state.bc = make16(b, get_c()); }

    fast_u8 on_get_b() const { return get_b(); }
    void on_set_b(fast_u8 b) { set_b(b); }

    fast_u8 get_c() const { return get_low8(state.bc); }
    void set_c(fast_u8 c) { state.bc = make16(get_b(), c); }

    fast_u8 on_get_c() const { return get_c(); }
    void on_set_c(fast_u8 c) { set_c(c); }

    fast_u8 get_d() const { return get_high8(state.de); }
    void set_d(fast_u8 d) { state.de = make16(d, get_e()); }

    fast_u8 on_get_d() const { return get_d(); }
    void on_set_d(fast_u8 d) { set_d(d); }

    fast_u8 get_e() const { return get_low8(state.de); }
    void set_e(fast_u8 e) { state.de = make16(get_d(), e); }

    fast_u8 on_get_e() const { return get_e(); }
    void on_set_e(fast_u8 e) { set_e(e); }

    fast_u8 get_h() const { return get_high8(state.hl); }
    void set_h(fast_u8 h) { state.hl = make16(h, get_l()); }

    fast_u8 on_get_h() const { return get_h(); }
    void on_set_h(fast_u8 h) { set_h(h); }

    fast_u8 get_l() const { return get_low8(state.hl); }
    void set_l(fast_u8 l) { state.hl = make16(get_h(), l); }

    fast_u8 on_get_l() const { return get_l(); }
    void on_set_l(fast_u8 l) { set_l(l); }

    fast_u8 get_a() const { return get_high8(state.af); }
    void set_a(fast_u8 a) { state.af = make16(a, get_f()); }

    fast_u8 on_get_a() const { return get_a(); }
    void on_set_a(fast_u8 a) { set_a(a); }

    fast_u8 get_f() const { return get_low8(state.af); }
    void set_f(fast_u8 f) { state.af = make16(get_a(), f); }

    fast_u8 on_get_f() const { return get_f(); }
    void on_set_f(fast_u8 f) { set_f(f); }

    fast_u8 get_ixh() const { return get_high8(state.ix); }
    void set_ixh(fast_u8 ixh) { state.ix = make16(ixh, get_ixl()); }

    fast_u8 on_get_ixh() const { return get_ixh(); }
    void on_set_ixh(fast_u8 ixh) { set_ixh(ixh); }

    fast_u8 get_ixl() const { return get_low8(state.ix); }
    void set_ixl(fast_u8 ixl) { state.ix = make16(get_ixh(), ixl); }

    fast_u8 on_get_ixl() const { return get_ixl(); }
    void on_set_ixl(fast_u8 ixl) { set_ixl(ixl); }

    fast_u8 get_iyh() const { return get_high8(state.iy); }
    void set_iyh(fast_u8 iyh) { state.iy = make16(iyh, get_iyl()); }

    fast_u8 on_get_iyh() const { return get_iyh(); }
    void on_set_iyh(fast_u8 iyh) { set_iyh(iyh); }

    fast_u8 get_iyl() const { return get_low8(state.iy); }
    void set_iyl(fast_u8 iyl) { state.iy = make16(get_iyh(), iyl); }

    fast_u8 on_get_iyl() const { return get_iyl(); }
    void on_set_iyl(fast_u8 iyl) { set_iyl(iyl); }

    fast_u8 get_i() const { return get_high8(state.ir); }
    void set_i(fast_u8 i) { state.ir = make16(i, get_r_reg()); }

    fast_u8 on_get_i() const { return get_i(); }
    void on_set_i(fast_u8 i) { set_i(i); }

    void set_i_on_ld(fast_u8 i) { (*this)->on_set_i(i); }

    fast_u8 get_r_reg() const { return get_low8(state.ir); }
    void set_r_reg(fast_u8 r) { state.ir = make16(get_i(), r); }

    fast_u16 get_af() const { return state.af; }
    void set_af(fast_u16 af) { state.af = af; }

    fast_u16 on_get_af() {
        // Always get the low byte first.
        fast_u8 f = (*this)->on_get_f();
        fast_u8 a = (*this)->on_get_a();
        return make16(a, f); }
    void on_set_af(fast_u16 af) {
        // Always set the low byte first.
        (*this)->on_set_f(get_low8(af));
        (*this)->on_set_a(get_high8(af)); }

    fast_u16 get_hl() const { return state.hl; }
    void set_hl(fast_u16 hl) { state.hl = hl; }

    fast_u16 on_get_hl() {
        // Always get the low byte first.
        fast_u8 l = (*this)->on_get_l();
        fast_u8 h = (*this)->on_get_h();
        return make16(h, l); }
    void on_set_hl(fast_u16 hl) {
        // Always set the low byte first.
        (*this)->on_set_l(get_low8(hl));
        (*this)->on_set_h(get_high8(hl)); }

    fast_u16 get_bc() const { return state.bc; }
    void set_bc(fast_u16 bc) { state.bc = bc; }

    fast_u16 on_get_bc() {
        // Always get the low byte first.
        fast_u8 l = (*this)->on_get_c();
        fast_u8 h = (*this)->on_get_b();
        return make16(h, l); }
    void on_set_bc(fast_u16 bc) {
        // Always set the low byte first.
        (*this)->on_set_c(get_low8(bc));
        (*this)->on_set_b(get_high8(bc)); }

    fast_u16 get_de() const { return state.de; }
    void set_de(fast_u16 de) { state.de = de; }

    fast_u16 on_get_de() {
        // Always get the low byte first.
        fast_u8 l = (*this)->on_get_e();
        fast_u8 h = (*this)->on_get_d();
        return make16(h, l); }
    void on_set_de(fast_u16 de) {
        // Always set the low byte first.
        (*this)->on_set_e(get_low8(de));
        (*this)->on_set_d(get_high8(de)); }

    fast_u16 get_ix() const { return state.ix; }
    void set_ix(fast_u16 ix) { state.ix = ix; }

    fast_u16 on_get_ix() {
        // Always get the low byte first.
        fast_u8 l = (*this)->on_get_ixl();
        fast_u8 h = (*this)->on_get_ixh();
        return make16(h, l); }
    void on_set_ix(fast_u16 ix) {
        // Always set the low byte first.
        (*this)->on_set_ixl(get_low8(ix));
        (*this)->on_set_ixh(get_high8(ix)); }

    fast_u16 get_iy() const { return state.iy; }
    void set_iy(fast_u16 iy) { state.iy = iy; }

    fast_u16 on_get_iy() {
        // Always get the low byte first.
        fast_u8 l = (*this)->on_get_iyl();
        fast_u8 h = (*this)->on_get_iyh();
        return make16(h, l); }
    void on_set_iy(fast_u16 iy) {
        // Always set the low byte first.
        (*this)->on_set_iyl(get_low8(iy));
        (*this)->on_set_iyh(get_high8(iy)); }

    fast_u16 get_sp() const { return state.sp; }
    void set_sp(fast_u16 sp) { state.sp = sp; }

    fast_u16 on_get_sp() { return get_sp(); }
    void on_set_sp(fast_u16 sp) { set_sp(sp); }

    fast_u16 get_pc() const { return state.pc; }
    void set_pc(fast_u16 pc) { state.pc = pc; }

    fast_u16 on_get_pc() const { return get_pc(); }
    void on_set_pc(fast_u16 pc) { set_pc(pc); }

    fast_u16 get_pc_on_fetch() const { return (*this)->on_get_pc(); }
    void set_pc_on_fetch(fast_u16 pc) { (*this)->on_set_pc(pc); }

    fast_u16 get_pc_on_imm8_read() const { return (*this)->on_get_pc(); }
    void set_pc_on_imm8_read(fast_u16 pc) { (*this)->on_set_pc(pc); }

    fast_u16 get_pc_on_imm16_read() const { return (*this)->on_get_pc(); }
    void set_pc_on_imm16_read(fast_u16 pc) { (*this)->on_set_pc(pc); }

    fast_u16 get_pc_on_disp_read() const { return (*this)->on_get_pc(); }
    void set_pc_on_disp_read(fast_u16 pc) { (*this)->on_set_pc(pc); }

    fast_u16 get_pc_on_jump() const { return (*this)->on_get_pc(); }
    void set_pc_on_jump(fast_u16 pc) { (*this)->on_set_pc(pc); }

    fast_u16 get_pc_on_block_instr() const { return (*this)->on_get_pc(); }
    void set_pc_on_block_instr(fast_u16 pc) { (*this)->on_set_pc(pc); }

    void set_pc_on_call(fast_u16 pc) { (*this)->on_set_pc(pc); }
    void set_pc_on_return(fast_u16 pc) { (*this)->on_set_pc(pc); }

    fast_u16 get_ir() const { return state.ir; }

    fast_u16 on_get_ir() const { return (*this)->get_ir(); }

    fast_u16 get_ir_on_refresh() const { return (*this)->on_get_ir(); }

    fast_u16 get_memptr() const { return state.memptr; }
    void set_memptr(fast_u16 memptr) { state.memptr = memptr; }

    fast_u16 on_get_memptr() const { return get_memptr(); }
    void on_set_memptr(fast_u16 memptr) { set_memptr(memptr); }

    bool get_iff1() const { return state.iff1; }
    void set_iff1(bool iff1) { state.iff1 = iff1; }

    bool on_get_iff1() const { return get_iff1(); }
    void on_set_iff1(bool iff1) { set_iff1(iff1); }

    void set_iff1_on_di(bool iff1) { (*this)->on_set_iff1(iff1); }
    void set_iff1_on_ei(bool iff1) { (*this)->on_set_iff1(iff1); }

    bool get_iff2() const { return state.iff2; }
    void set_iff2(bool iff2) { state.iff2 = iff2; }

    bool on_get_iff2() const { return get_iff2(); }
    void on_set_iff2(bool iff2) { set_iff2(iff2); }

    void set_iff2_on_di(bool iff2) { (*this)->on_set_iff2(iff2); }
    void set_iff2_on_ei(bool iff2) { (*this)->on_set_iff2(iff2); }

    unsigned get_int_mode() const { return state.int_mode; }
    void set_int_mode(unsigned mode) { state.int_mode = mode; }

    bool on_get_int_mode() const { return get_int_mode(); }
    void on_set_int_mode(unsigned mode) { set_int_mode(mode); }

    void disable_int() { state.disable_int = true; }
    void on_disable_int() { disable_int(); }

    void on_set_next_index_rp(index_regp irp) {
        decoder::on_set_next_index_rp(irp);
        (*this)->on_disable_int();
    }

    fast_u16 get_disp_target(fast_u16 base, fast_u8 d) {
        return !get_sign8(d) ? add16(base, d) : sub16(base, neg8(d));
    }

    fast_u8 read_at_disp(fast_u8 d, bool long_read_cycle = false) {
        fast_u16 addr = get_disp_target((*this)->on_get_index_rp(), d);
        fast_u8 res = long_read_cycle ? (*this)->on_4t_read_cycle(addr) :
                                        (*this)->on_3t_read_cycle(addr);
        if((*this)->get_index_rp_kind() != index_regp::hl)
            (*this)->on_set_memptr(addr);
        return res;
    }

    void write_at_disp(fast_u8 d, fast_u8 n) {
        fast_u16 addr = get_disp_target((*this)->on_get_index_rp(), d);
        (*this)->on_3t_write_cycle(addr, n);
        if((*this)->get_index_rp_kind() != index_regp::hl)
            (*this)->on_set_memptr(addr);
    }

    fast_u8 get_r(reg r) {
        switch(r) {
        case reg::b: return get_b();
        case reg::c: return get_c();
        case reg::d: return get_d();
        case reg::e: return get_e();
        case reg::h: return get_h();
        case reg::l: return get_l();
        case reg::at_hl: return (*this)->on_access(get_hl());
        case reg::a: return get_a();
        }
        assert(0);
    }

    fast_u8 on_get_r(reg r, fast_u8 d = 0, bool long_read_cycle = false) {
        switch(r) {
        case reg::b: return (*this)->on_get_b();
        case reg::c: return (*this)->on_get_c();
        case reg::d: return (*this)->on_get_d();
        case reg::e: return (*this)->on_get_e();
        case reg::h: return (*this)->on_get_h();
        case reg::l: return (*this)->on_get_l();
        case reg::at_hl: return read_at_disp(d, long_read_cycle);
        case reg::a: return (*this)->on_get_a();
        }
        assert(0);
    }

    void on_set_r(reg r, fast_u8 d, fast_u8 n) {
        switch(r) {
        case reg::b: return (*this)->on_set_b(n);
        case reg::c: return (*this)->on_set_c(n);
        case reg::d: return (*this)->on_set_d(n);
        case reg::e: return (*this)->on_set_e(n);
        case reg::h: return (*this)->on_set_h(n);
        case reg::l: return (*this)->on_set_l(n);
        case reg::at_hl: return write_at_disp(d, n);
        case reg::a: return (*this)->on_set_a(n);
        }
        assert(0);
    }

    fast_u16 on_get_rp(regp rp) {
        switch(rp) {
        case regp::bc: return (*this)->on_get_bc();
        case regp::de: return (*this)->on_get_de();
        case regp::hl: return (*this)->on_get_index_rp();
        case regp::sp: return (*this)->on_get_sp();
        }
        assert(0);
    }

    void on_set_rp(regp rp, fast_u16 nn) {
        switch(rp) {
        case regp::bc: return (*this)->on_set_bc(nn);
        case regp::de: return (*this)->on_set_de(nn);
        case regp::hl: return (*this)->on_set_index_rp(nn);
        case regp::sp: return (*this)->on_set_sp(nn);
        }
        assert(0);
    }

    fast_u16 on_get_rp2(regp2 rp) {
        switch(rp) {
        case regp2::bc: return (*this)->on_get_bc();
        case regp2::de: return (*this)->on_get_de();
        case regp2::hl: return (*this)->on_get_index_rp();
        case regp2::af: return (*this)->on_get_af();
        }
        assert(0);
    }

    void on_set_rp2(regp2 rp, fast_u16 nn) {
        switch(rp) {
        case regp2::bc: return (*this)->on_set_bc(nn);
        case regp2::de: return (*this)->on_set_de(nn);
        case regp2::hl: return (*this)->on_set_index_rp(nn);
        case regp2::af: return (*this)->on_set_af(nn);
        }
        assert(0);
    }

    fast_u16 get_index_rp(index_regp irp) {
        switch(irp) {
        case index_regp::hl: return get_hl();
        case index_regp::ix: return get_ix();
        case index_regp::iy: return get_iy();
        }
        assert(0);
    }

    fast_u16 on_get_index_rp() {
        switch((*this)->get_index_rp_kind()) {
        case index_regp::hl: return (*this)->on_get_hl();
        case index_regp::ix: return (*this)->on_get_ix();
        case index_regp::iy: return (*this)->on_get_iy();
        }
        assert(0);
    }

    void on_set_index_rp(fast_u16 nn) {
        switch((*this)->get_index_rp_kind()) {
        case index_regp::hl: return (*this)->on_set_hl(nn);
        case index_regp::ix: return (*this)->on_set_ix(nn);
        case index_regp::iy: return (*this)->on_set_iy(nn);
        }
        assert(0);
    }

    fast_u16 get_last_read_addr() const { return state.last_read_addr; }

    void do_alu(alu k, fast_u8 n) {
        fast_u8 a = (*this)->on_get_a();
        fast_u8 f;
        switch(k) {
        case alu::add: {
            fast_u8 t = add8(a, n);
            f = (t & (sf_mask | yf_mask | xf_mask)) | zf_ari(t) |
                    hf_ari(t, a, n) | pf_ari(a + n, a, n) | cf_ari(t < a);
            a = t;
            break; }
        case alu::adc: assert(0); break;  // TODO
        case alu::sub: {
            fast_u8 t = sub8(a, n);
            f = (t & (sf_mask | yf_mask | xf_mask)) | zf_ari(t) |
                    hf_ari(t, a, n) | pf_ari(a - n, a, n) | cf_ari(t > a) |
                    nf_mask;
            a = t;
            break; }
        case alu::sbc: assert(0); break;  // TODO
        case alu::and_a:
            a &= n;
            f = (a & (sf_mask | yf_mask | xf_mask)) | zf_ari(a) | pf_log(a) |
                    hf_mask;
            break;
        case alu::xor_a:
            a ^= n;
            f = (a & (sf_mask | yf_mask | xf_mask)) | zf_ari(a) | pf_log(a);
            break;
        case alu::or_a:
            a |= n;
            f = (a & (sf_mask | yf_mask | xf_mask)) | zf_ari(a) | pf_log(a);
            break;
        case alu::cp: {
            fast_u8 t = sub8(a, n);
            f = (t & sf_mask) | zf_ari(t) | (n & (yf_mask | xf_mask)) |
                    hf_ari(t, a, n) | pf_ari(a - n, a, n) | cf_ari(t > a) |
                    nf_mask;
            break; }
        }
        if(k != alu::cp)
            (*this)->on_set_a(a);
        (*this)->on_set_f(f);
    }

    fast_u8 get_flag_mask(condition cc) {
        switch(cc / 2) {
        case 0: return zf_mask;
        case 1: return cf_mask;
        case 2: return pf_mask;
        case 3: return sf_mask;
        }
        assert(0);
    }

    bool check_condition(condition cc) {
        bool actual = (*this)->on_get_f() & get_flag_mask(cc);
        bool expected = cc & 1;
        return actual == expected;
    }

    void on_noni_ed(fast_u8 op) {
        // TODO: Forbid INT after this instruction.
        unused(op);
    }

    void on_push(fast_u16 nn) {
        fast_u16 sp = (*this)->on_get_sp();
        sp = dec16(sp);
        (*this)->on_3t_write_cycle(sp, get_high8(nn));
        sp = dec16(sp);
        (*this)->on_3t_write_cycle(sp, get_low8(nn));
        (*this)->on_set_sp(sp);
    }

    fast_u16 on_pop() {
        fast_u16 sp = (*this)->on_get_sp();
        fast_u8 lo = (*this)->on_3t_read_cycle(sp);
        sp = inc16(sp);
        fast_u8 hi = (*this)->on_3t_read_cycle(sp);
        sp = inc16(sp);
        (*this)->on_set_sp(sp);
        return make16(hi, lo);
    }

    void on_call(fast_u16 nn) {
        (*this)->on_push((*this)->on_get_pc());
        (*this)->set_memptr(nn);
        (*this)->set_pc_on_call(nn);
    }

    void on_return() {
        fast_u16 pc = (*this)->on_pop();
        (*this)->set_memptr(pc);
        (*this)->set_pc_on_return(pc);
    }

    void on_relative_jump(fast_u8 d) {
        (*this)->on_5t_exec_cycle();
        fast_u16 memptr = get_disp_target((*this)->get_pc_on_jump(), d);
        (*this)->on_set_memptr(memptr);
        (*this)->set_pc_on_jump(memptr);
    }

    void on_add_irp_rp(regp rp) {
        fast_u16 i = (*this)->on_get_index_rp();
        fast_u16 n = (*this)->on_get_rp(rp);
        fast_u8 f = (*this)->on_get_f();

        (*this)->on_4t_exec_cycle();
        (*this)->on_3t_exec_cycle();

        fast_u16 r = add16(i, n);
        f = (f & (sf_mask | zf_mask | pf_mask)) |
                (get_high8(r) & (yf_mask | xf_mask)) |
                hf_ari(r >> 8, i >> 8, n >> 8) | cf_ari(r < i);

        (*this)->on_set_memptr(inc16(i));
        (*this)->on_set_index_rp(r); }
    void on_adc_hl_rp(regp rp) {
        fast_u16 hl = (*this)->on_get_hl();
        fast_u16 n = (*this)->on_get_rp(rp);
        bool cf = (*this)->on_get_f() & cf_mask;

        (*this)->on_4t_exec_cycle();
        (*this)->on_3t_exec_cycle();

        fast_u16 t = add16(n, cf);
        bool of = cf && t == 0;
        fast_u16 r = add16(hl, t);
        fast_u8 f = (get_high8(r) & (sf_mask | yf_mask | xf_mask)) | zf_ari(r) |
                        hf_ari(r >> 8, hl >> 8, n >> 8) |
                        (pf_ari(r >> 8, hl >> 8, n >> 8) ^ (of ? pf_mask : 0)) |
                        cf_ari(r < hl || of);

        (*this)->on_set_memptr(inc16(hl));
        (*this)->on_set_hl(r);
        (*this)->on_set_f(f); }
    void on_alu_n(alu k, fast_u8 n) {
        do_alu(k, n); }
    void on_alu_r(alu k, reg r, fast_u8 d) {
        do_alu(k, (*this)->on_get_r(r, d)); }
    void on_block_ld(block_ld k) {
        fast_u16 bc = (*this)->on_get_bc();
        fast_u16 de = (*this)->on_get_de();
        fast_u16 hl = (*this)->on_get_hl();
        // TODO: Block loads implicitly depend on the register 'a'. We probably
        // want to request its value here with a special handler.
        fast_u8 a = (*this)->on_get_a();
        fast_u8 f = (*this)->on_get_f();

        fast_u8 t = (*this)->on_3t_read_cycle(hl);
        (*this)->on_5t_write_cycle(de, t);

        bc = dec16(bc);
        t += a;
        f = (f & (sf_mask | zf_mask | cf_mask)) |
                ((t << 4) & yf_mask) | (t & xf_mask) | (bc != 0 ? pf_mask : 0);
        if(static_cast<unsigned>(k) & 1) {
            // LDI, LDIR
            hl = dec16(hl);
            de = dec16(de);
        } else {
            // LDD, LDDR
            hl = inc16(hl);
            de = inc16(de);
        }

        (*this)->on_set_bc(bc);
        (*this)->on_set_de(de);
        (*this)->on_set_hl(hl);
        (*this)->on_set_f(f);

        // LDIR, LDDR
        if((static_cast<unsigned>(k) & 2) && bc) {
            (*this)->on_5t_exec_cycle();
            fast_u16 pc = (*this)->get_pc_on_block_instr();
            (*this)->on_set_memptr(inc16(pc));
            (*this)->set_pc_on_block_instr(sub16(pc, 2));
        } }
    void on_bit(unsigned b, reg r, fast_u8 d) {
        fast_u8 v = (*this)->on_get_r(r, d, /* long_read_cycle= */ true);
        fast_u8 f = (*this)->on_get_f();
        fast_u8 m = v & (1u << b);
        f = (f & cf_mask) | hf_mask | (m ? (m & sf_mask) : (zf_mask | pf_mask));
        if((*this)->get_index_rp_kind() != index_regp::hl || r == reg::at_hl)
            v = get_high8((*this)->on_get_memptr());
        f |= v & (xf_mask | yf_mask);
        (*this)->on_set_f(f); }
    void on_call_nn(fast_u16 nn) {
        (*this)->on_call(nn); }
    void on_ccf() {
        fast_u8 a = (*this)->on_get_a();
        fast_u8 f = (*this)->on_get_f();
        bool cf = f & cf_mask;
        f = (f & (sf_mask | zf_mask | pf_mask)) | (a & (yf_mask | xf_mask)) |
                (cf ? hf_mask : 0) | cf_ari(!cf);
        (*this)->on_set_f(f); }
    void on_dec_r(reg r, fast_u8 d) {
        fast_u8 v = (*this)->on_get_r(r, d, /* long_read_cycle= */ true);
        fast_u8 f = (*this)->on_get_f();
        v = dec8(v);
        f = (f & cf_mask) | (v & (sf_mask | yf_mask | xf_mask)) | zf_ari(v) |
                hf_dec(v) | pf_dec(v) | nf_mask;
        (*this)->on_set_r(r, d, v);
        (*this)->on_set_f(f); }
    void on_dec_rp(regp rp) {
        (*this)->on_set_rp(rp, dec16((*this)->on_get_rp(rp))); }
    void on_di() {
        (*this)->set_iff1_on_di(false);
        (*this)->set_iff2_on_di(false); }
    void on_djnz(fast_u8 d) {
        fast_u8 b = (*this)->on_get_b();
        b = dec8(b);
        (*this)->on_set_b(b);
        if(b)
            (*this)->on_relative_jump(d); }
    void on_ei() {
        (*this)->set_iff1_on_ei(true);
        (*this)->set_iff2_on_ei(true);
        (*this)->on_disable_int(); }
    void on_ex_de_hl() {
        state.ex_de_hl(); }
    void on_exx() {
        state.exx(); }
    void on_im(unsigned mode) {
        (*this)->on_set_int_mode(mode); }
    void on_inc_r(reg r, fast_u8 d) {
        fast_u8 v = (*this)->on_get_r(r, d, /* long_read_cycle= */ true);
        fast_u8 f = (*this)->on_get_f();
        v = inc8(v);
        f = (f & cf_mask) | (v & (sf_mask | yf_mask | xf_mask)) | zf_ari(v) |
                hf_inc(v) | pf_inc(v);
        (*this)->on_set_r(r, d, v);
        (*this)->on_set_f(f); }
    void on_inc_rp(regp rp) {
        (*this)->on_set_rp(rp, inc16((*this)->on_get_rp(rp))); }
    void on_jp_irp() {
        (*this)->set_pc_on_jump((*this)->on_get_index_rp()); }
    void on_jp_nn(fast_u16 nn) {
        (*this)->on_set_memptr(nn);
        (*this)->set_pc_on_jump(nn); }
    void on_jr(fast_u8 d) {
        (*this)->on_relative_jump(d); }
    void on_jr_cc(condition cc, fast_u8 d) {
        if(check_condition(cc))
            (*this)->on_relative_jump(d); }
    void on_ld_i_a() {
        (*this)->set_i_on_ld((*this)->on_get_a()); }
    void on_ld_r_r(reg rd, reg rs, fast_u8 d) {
        (*this)->on_set_r(rd, d, (*this)->on_get_r(rs, d)); }
    void on_ld_r_n(reg r, fast_u8 d, fast_u8 n) {
        (*this)->on_set_r(r, d, n); }
    void on_ld_rp_nn(regp rp, fast_u16 nn) {
        (*this)->on_set_rp(rp, nn); }

    void on_ld_irp_at_nn(fast_u16 nn) {
        fast_u8 lo = (*this)->on_3t_read_cycle(nn);
        nn = inc16(nn);
        (*this)->on_set_memptr(nn);
        fast_u8 hi = (*this)->on_3t_read_cycle(nn);
        (*this)->on_set_index_rp(make16(hi, lo)); }
    void on_ld_at_nn_irp(fast_u16 nn) {
        fast_u16 irp = (*this)->on_get_index_rp();
        (*this)->on_3t_write_cycle(nn, get_low8(irp));
        nn = inc16(nn);
        (*this)->on_set_memptr(nn);
        (*this)->on_3t_write_cycle(nn, get_high8(irp)); }

    void on_ld_rp_at_nn(regp rp, fast_u16 nn) {
        fast_u8 lo = (*this)->on_3t_read_cycle(nn);
        nn = inc16(nn);
        (*this)->on_set_memptr(nn);
        fast_u8 hi = (*this)->on_3t_read_cycle(nn);
        (*this)->on_set_rp(rp, make16(hi, lo)); }
    void on_ld_at_nn_rp(fast_u16 nn, regp rp) {
        fast_u16 rpv = (*this)->on_get_rp(rp);
        (*this)->on_3t_write_cycle(nn, get_low8(rpv));
        nn = inc16(nn);
        (*this)->on_set_memptr(nn);
        (*this)->on_3t_write_cycle(nn, get_high8(rpv)); }
    void on_ld_a_at_nn(fast_u16 nn) {
        (*this)->on_set_memptr(inc16(nn));
        (*this)->on_set_a((*this)->on_3t_read_cycle(nn)); }
    void on_ld_at_nn_a(fast_u16 nn) {
        fast_u8 a = (*this)->on_get_a();
        (*this)->on_set_memptr(make16(a, inc8(get_low8(nn))));
        (*this)->on_3t_write_cycle(nn, a); }
    void on_ld_sp_irp() {
        (*this)->on_set_sp((*this)->on_get_index_rp()); }
    void on_nop() {}
    void on_out_n_a(fast_u8 n) {
        fast_u8 a = (*this)->on_get_a();
        (*this)->on_output_cycle(make16(a, n), a);
        (*this)->on_set_memptr(make16(a, inc8(n))); }
    void on_pop_rp(regp2 rp) {
        (*this)->on_set_rp2(rp, (*this)->on_pop()); }
    void on_push_rp(regp2 rp) {
        (*this)->on_push((*this)->on_get_rp2(rp)); }
    void on_res(unsigned b, reg r, fast_u8 d) {
        index_regp irp = (*this)->get_index_rp_kind();
        reg access_r = irp == index_regp::hl ? r : reg::at_hl;
        fast_u8 v = (*this)->on_get_r(access_r, d, /* long_read_cycle= */ true);
        v &= ~(1u << b);
        (*this)->on_set_r(access_r, d, v);
        if(irp != index_regp::hl && r != reg::at_hl)
            (*this)->on_set_r(r, /* d= */ 0, v); }
    void on_ret() {
        (*this)->on_return(); }
    void on_ret_cc(condition cc) {
        if(check_condition(cc))
            (*this)->on_return(); }
    void on_rrca() {
        fast_u8 a = (*this)->on_get_a();
        fast_u8 f = (*this)->on_get_f();
        a = ror8(a);
        f = (f & (sf_mask | zf_mask | pf_mask)) | (a & (yf_mask | xf_mask)) |
                cf_ari(a & 0x80);
        (*this)->on_set_a(a);
        (*this)->on_set_f(f); }
    void on_scf() {
        fast_u8 a = (*this)->on_get_a();
        fast_u8 f = (*this)->on_get_f();
        f = (f & (sf_mask | zf_mask | pf_mask)) | (a & (yf_mask | xf_mask)) |
                cf_mask;
        (*this)->on_set_f(f); }
    void on_set(unsigned b, reg r, fast_u8 d) {
        index_regp irp = (*this)->get_index_rp_kind();
        reg access_r = irp == index_regp::hl ? r : reg::at_hl;
        fast_u8 v = (*this)->on_get_r(access_r, d, /* long_read_cycle= */ true);
        v |= 1u << b;
        (*this)->on_set_r(access_r, d, v);
        if(irp != index_regp::hl && r != reg::at_hl)
            (*this)->on_set_r(r, /* d= */ 0, v); }
    void on_sbc_hl_rp(regp rp) {
        fast_u16 hl = (*this)->on_get_hl();
        fast_u16 n = (*this)->on_get_rp(rp);
        bool cf = (*this)->on_get_f() & cf_mask;

        (*this)->on_4t_exec_cycle();
        (*this)->on_3t_exec_cycle();

        fast_u16 t = add16(n, cf);
        bool of = cf && t == 0;
        fast_u16 r = sub16(hl, t);
        fast_u8 f = (get_high8(r) & (sf_mask | yf_mask | xf_mask)) | zf_ari(r) |
                        hf_ari(r >> 8, hl >> 8, n >> 8) |
                        (pf_ari(r >> 8, hl >> 8, n >> 8) ^ (of ? pf_mask : 0)) |
                        cf_ari(r > hl || of) | nf_mask;

        (*this)->on_set_memptr(inc16(hl));
        (*this)->on_set_hl(r);
        (*this)->on_set_f(f); }

    fast_u8 on_fetch() {
        fast_u16 pc = (*this)->get_pc_on_fetch();
        fast_u8 op = (*this)->on_fetch_cycle(pc);
        (*this)->set_pc_on_fetch(inc16(pc));
        return op;
    }

    void on_set_addr_bus(fast_u16 addr) {
        unused(addr);
    }

    fast_u8 on_fetch_cycle(fast_u16 addr) {
        (*this)->on_set_addr_bus(addr);
        fast_u8 b = (*this)->on_access(addr);
        (*this)->tick(2);
        (*this)->on_set_addr_bus((*this)->get_ir_on_refresh());
        (*this)->tick(2);
        state.last_read_addr = addr;
        return b;
    }

    void on_5t_fetch_cycle() {
        (*this)->tick(1);
    }

    void on_6t_fetch_cycle() {
        (*this)->tick(2);
    }

    fast_u8 on_3t_read_cycle(fast_u16 addr) {
        (*this)->on_set_addr_bus(addr);
        fast_u8 b = (*this)->on_access(addr);
        (*this)->tick(3);
        state.last_read_addr = addr;
        return b;
    }

    fast_u8 on_4t_read_cycle(fast_u16 addr) {
        (*this)->on_set_addr_bus(addr);
        fast_u8 b = (*this)->on_access(addr);
        (*this)->tick(4);
        state.last_read_addr = addr;
        return b;
    }

    fast_u8 on_5t_read_cycle(fast_u16 addr) {
        (*this)->on_set_addr_bus(addr);
        fast_u8 b = (*this)->on_access(addr);
        (*this)->tick(5);
        state.last_read_addr = addr;
        return b;
    }

    fast_u8 on_disp_read_cycle(fast_u16 addr) {
        return (*this)->on_3t_read_cycle(addr);
    }

    void on_3t_write_cycle(fast_u16 addr, fast_u8 n) {
        (*this)->on_set_addr_bus(addr);
        (*this)->on_access(addr) = static_cast<least_u8>(n);
        (*this)->tick(3);
    }

    void on_5t_write_cycle(fast_u16 addr, fast_u8 n) {
        (*this)->on_set_addr_bus(addr);
        (*this)->on_access(addr) = static_cast<least_u8>(n);
        (*this)->tick(5);
    }

    void on_3t_exec_cycle() {
        (*this)->tick(3);
    }

    void on_4t_exec_cycle() {
        (*this)->tick(4);
    }

    void on_5t_exec_cycle() {
        (*this)->tick(5);
    }

    void on_output_cycle(fast_u16 addr, fast_u8 b) {
        // TODO: Shall we set the address bus here?
        unused(addr, b);
        (*this)->tick(4);
    }

    fast_u8 on_3t_imm8_read() {
        fast_u16 pc = (*this)->get_pc_on_imm8_read();
        fast_u8 op = (*this)->on_3t_read_cycle(pc);
        (*this)->set_pc_on_imm8_read(inc16(pc));
        return op;
    }

    fast_u8 on_5t_imm8_read() {
        fast_u16 pc = (*this)->get_pc_on_imm8_read();
        fast_u8 op = (*this)->on_5t_read_cycle(pc);
        (*this)->set_pc_on_imm8_read(inc16(pc));
        return op;
    }

    fast_u16 on_3t_3t_imm16_read() {
        fast_u16 pc = (*this)->get_pc_on_imm16_read();
        fast_u8 lo = (*this)->on_3t_read_cycle(pc);
        pc = inc16(pc);
        fast_u8 hi = (*this)->on_3t_read_cycle(pc);
        (*this)->set_pc_on_imm16_read(inc16(pc));
        return make16(hi, lo);
    }

    fast_u16 on_3t_4t_imm16_read() {
        fast_u16 pc = (*this)->get_pc_on_imm16_read();
        fast_u8 lo = (*this)->on_3t_read_cycle(pc);
        pc = inc16(pc);
        fast_u8 hi = (*this)->on_4t_read_cycle(pc);
        (*this)->set_pc_on_imm16_read(inc16(pc));
        return make16(hi, lo);
    }

    fast_u8 on_disp_read() {
        fast_u16 pc = (*this)->get_pc_on_disp_read();
        fast_u8 op = (*this)->on_disp_read_cycle(pc);
        (*this)->set_pc_on_disp_read(inc16(pc));
        return op;
    }

    void on_step() { (*this)->decode(); }
    void step() { return (*this)->on_step(); }

protected:
    D *operator -> () { return static_cast<D*>(this); }
    const D *operator -> () const { return static_cast<const D*>(this); }
};

}  // namespace z80

#endif  // Z80_H
