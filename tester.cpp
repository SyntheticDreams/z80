
/*  Z80 CPU Simulator.

    Copyright (C) 2017 Ivan Kosarev.
    ivan@kosarev.info

    Published under the MIT license.
*/

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "z80.h"

namespace {

#if defined(__GNUC__) || defined(__clang__)
# define LIKE_PRINTF(format, args) \
      __attribute__((__format__(__printf__, format, args)))
#else
# define LIKE_PRINTF(format, args) /* nothing */
#endif

[[noreturn]] LIKE_PRINTF(1, 2)
void error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    std::fprintf(stderr, "tester: ");
    std::vfprintf(stderr, format, args);
    std::fprintf(stderr, "\n");
    va_end(args);
    exit(EXIT_FAILURE);
}

class disassembler : public z80::instructions_decoder<disassembler>,
                     public z80::disassembler<disassembler> {
public:
    disassembler() {}

    const char *get_output() const {
        return output_buff;
    }

    void output(const char *str) {
        std::snprintf(output_buff, max_output_buff_size, "%s", str);
    }

    z80::fast_u16 get_instr_addr() const {
        return 0;
    }

    z80::fast_u8 fetch_next_opcode() {
        return 0;
    }

private:
    static const std::size_t max_output_buff_size = 32;
    char output_buff[max_output_buff_size];
};

class machine : public z80::memory_interface<machine>,
                public z80::instructions_decoder<machine>,
                public z80::processor<machine> {
public:
    typedef uint_fast32_t ticks_type;

    machine() {}

    void tick(unsigned t) { ticks.tick(t); }

    ticks_type get_ticks() const { return ticks.get_ticks(); }

    z80::least_u8 &at(z80::fast_u16 addr) {
        assert(addr < image_size);
        return image[addr];
    }

private:
    z80::trivial_ticks_counter<ticks_type> ticks;

    static const z80::size_type image_size = 0x10000;  // 64K bytes.
    z80::least_u8 image[image_size];
};

}  // anonymous namespace

static void test_disassembling() {
    disassembler disasm;
    disasm.disassemble();
    assert(std::strcmp(disasm.get_output(), "nop") == 0);
}

static void test_execution() {
    machine mach;
    assert(mach.get_pc() == 0);
    assert(mach.get_ticks() == 0);
    mach.step();
    assert(mach.get_pc() == 1);
    assert(mach.get_ticks() == 4);
}

int main(int argc, char *argv[]) {
    if(argc != 2)
        error("usage: tester <test-input>\n");

    FILE *f = fopen(argv[1], "r");
    if(!f)
        error("cannot open test input '%s': %s\n", argv[1],
              std::strerror(errno));

    test_disassembling();
    test_execution();

    if(fclose(f) != 0)
        error("cannot close test input '%s': %s\n", argv[1],
              std::strerror(errno));
}
