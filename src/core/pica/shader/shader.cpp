#include "core/pica/shader/shader.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace kabegami::pica {
namespace {

// the opcodes, by the top six bits of an instruction (Zakuro isa::OpCode)
enum : std::uint8_t {
    op_add = 0x00, op_dp3 = 0x01, op_dp4 = 0x02, op_dph = 0x03, op_dst = 0x04, op_ex2 = 0x05, op_lg2 = 0x06,
    op_litp = 0x07, op_mul = 0x08, op_sge = 0x09, op_slt = 0x0A, op_flr = 0x0B, op_max = 0x0C, op_min = 0x0D,
    op_rcp = 0x0E, op_rsq = 0x0F, op_mova = 0x12, op_mov = 0x13, op_dphi = 0x18, op_dsti = 0x19, op_sgei = 0x1A,
    op_slti = 0x1B, op_break = 0x20, op_nop = 0x21, op_end = 0x22, op_breakc = 0x23, op_call = 0x24,
    op_callc = 0x25, op_callu = 0x26, op_ifu = 0x27, op_ifc = 0x28, op_loop = 0x29, op_emit = 0x2A,
    op_setemit = 0x2B, op_jmpc = 0x2C, op_jmpu = 0x2D, op_cmp = 0x2E, op_madi = 0x30, op_mad = 0x38,
};

std::uint8_t normalize(std::uint8_t raw) {
    // the low opcode bit of cmp belongs to its first comparison mode, and
    // multiply-add uses only the top three bits, the rest is its destination
    if (raw == 0x2F) return op_cmp;
    if (raw >= 0x30 && raw <= 0x37) return op_madi;
    if (raw >= 0x38) return op_mad;
    return raw;
}

bool inverted(std::uint8_t op) { return op == op_dphi || op == op_dsti || op == op_sgei || op == op_slti; }

// a source's swizzle and negation from a descriptor, source 1, 2 or 3.
ShaderUnit::Operand operand_of(std::uint32_t descriptor, int source, std::uint32_t reg, std::uint32_t index) {
    static constexpr int shifts[] = {5, 14, 23};
    auto shift = shifts[source - 1];
    auto pattern = (descriptor >> shift) & 0xFF;
    ShaderUnit::Operand o;
    o.reg = reg;
    o.index = index;
    for (int c = 0; c < 4; ++c) o.swizzle[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>((pattern >> (6 - c * 2)) & 3);
    o.negate = (descriptor >> (shift - 1)) & 1;
    return o;
}

// Rust's float-to-int cast: truncation, NaN to 0, out of range held at the ends.
std::int32_t to_i32(float f) {
    if (std::isnan(f)) return 0;
    if (f >= 2147483648.0f) return std::numeric_limits<std::int32_t>::max();
    if (f < -2147483648.0f) return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(f);
}

float dot(const Vec4& a, const Vec4& b, int n) {
    // Rust's sum of floats starts from -0
    float sum = -0.0f;
    for (int i = 0; i < n; ++i) sum += multiply(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(i)]);
    return sum;
}

constexpr Vec4 temp_start{0, 0, 0, 1};

}  // namespace

float decode_float24(std::uint32_t raw) {
    raw &= 0x00FFFFFF;
    if (raw == 0) return 0.0f;
    std::uint32_t sign = (raw >> 23) & 1, exponent = (raw >> 16) & 0x7F, mantissa = raw & 0xFFFF;
    if (exponent == 0) return sign ? -0.0f : 0.0f;
    if (exponent == 0x7F) return sign ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
    return std::bit_cast<float>(sign << 31 | (exponent + 64) << 23 | mantissa << 7);
}

std::uint32_t encode_float24(float value) {
    auto bits = std::bit_cast<std::uint32_t>(value);
    std::uint32_t sign = bits >> 31, exponent = (bits >> 23) & 0xFF, mantissa = bits & 0x7FFFFF;
    if (exponent == 0) return sign << 23;
    if (exponent == 0xFF) return sign << 23 | 0x7F << 16 | (mantissa ? 0xFFFF : 0);
    std::int32_t e = static_cast<std::int32_t>(exponent) - 64;
    if (e <= 0) return sign << 23;
    if (e >= 0x7F) return sign << 23 | 0x7F << 16;
    return sign << 23 | static_cast<std::uint32_t>(e) << 16 | mantissa >> 7;
}

float multiply(float a, float b) {
    float product = a * b;
    if (std::isnan(product) && !std::isnan(a) && !std::isnan(b)) return 0.0f;
    return product;
}

void ShaderUnit::upload_program(std::uint32_t word) {
    if (program_write_offset < program_size) {
        if (program[program_write_offset] != word) {
            program[program_write_offset] = word;
            decoded_ = false;
        }
        ++program_write_offset;
    }
}

void ShaderUnit::upload_descriptor(std::uint32_t word) {
    if (descriptor_write_offset < descriptor_size) {
        if (descriptors[descriptor_write_offset] != word) {
            descriptors[descriptor_write_offset] = word;
            decoded_ = false;
        }
        ++descriptor_write_offset;
    }
}

void ShaderUnit::set_float_uniform_index(std::uint32_t raw) {
    float_index_ = raw & 0x7F;
    float_component_ = 0;
    float_wide_ = raw & 0x80000000;
}

void ShaderUnit::upload_float_uniform(std::uint32_t word) {
    std::size_t needed = float_wide_ ? 4 : 3;
    if (float_component_ < 4) float_staging_[float_component_] = word;
    ++float_component_;
    if (float_component_ < needed) return;
    float_component_ = 0;
    auto& s = float_staging_;
    Vec4 value;
    if (float_wide_) {
        // the words come w, z, y, x
        value = {std::bit_cast<float>(s[3]), std::bit_cast<float>(s[2]), std::bit_cast<float>(s[1]), std::bit_cast<float>(s[0])};
    } else {
        // four float24s packed across three words, w first
        value = {decode_float24(s[2] & 0x00FFFFFF), decode_float24((s[1] & 0x0000FFFF) << 8 | s[2] >> 24),
                 decode_float24((s[0] & 0x000000FF) << 16 | s[1] >> 16), decode_float24(s[0] >> 8)};
    }
    if (float_index_ < float_uniform_count) float_uniforms[float_index_] = value;
    ++float_index_;
}

void ShaderUnit::decode() {
    ops_.assign(program_size, {});
    for (std::size_t i = 0; i < program_size; ++i) {
        auto word = program[i];
        auto& op = ops_[i];
        op.word = word;
        op.opcode = normalize(static_cast<std::uint8_t>(word >> 26));
        if (op.opcode == op_mad || op.opcode == op_madi) {
            auto descriptor = descriptors[(word & 0x1F) % descriptor_size];
            std::uint32_t src1 = (word >> 17) & 0x1F, src2, src3;
            if (op.opcode == op_madi) {
                src2 = (word >> 12) & 0x1F;
                src3 = (word >> 5) & 0x7F;
            } else {
                src2 = (word >> 10) & 0x7F;
                src3 = (word >> 5) & 0x1F;
            }
            // the address register applies to whichever source is the wide one
            auto index = (word >> 22) & 0x3;
            op.destination = (word >> 24) & 0x1F;
            op.mask = descriptor & 0xF;
            op.sources = {operand_of(descriptor, 1, src1, 0), operand_of(descriptor, 2, src2, op.opcode == op_madi ? 0 : index),
                          operand_of(descriptor, 3, src3, op.opcode == op_madi ? index : 0)};
        } else {
            auto descriptor = descriptors[(word & 0x7F) % descriptor_size];
            std::uint32_t src1, src2;
            if (inverted(op.opcode)) {
                src1 = (word >> 14) & 0x1F;
                src2 = (word >> 7) & 0x7F;
            } else {
                src1 = (word >> 12) & 0x7F;
                src2 = (word >> 7) & 0x1F;
            }
            // only the wide operand can be a uniform, and only a uniform read
            // can be indexed by an address register
            auto index = (word >> 19) & 0x3;
            op.destination = (word >> 21) & 0x1F;
            op.mask = descriptor & 0xF;
            op.sources = {operand_of(descriptor, 1, src1, inverted(op.opcode) ? 0 : index),
                          operand_of(descriptor, 2, src2, inverted(op.opcode) ? index : 0), Operand{}};
        }
    }
    decoded_ = true;
}

namespace {

struct Run {
    const ShaderUnit& unit;
    ShaderState& state;

    std::uint32_t indexed(std::uint32_t reg, std::uint32_t index) const {
        if (reg < 0x20 || index == 0) return reg;
        auto offset = state.address[(index - 1) % 3];
        auto uniform = static_cast<std::int64_t>(reg) - 0x20 + offset;
        // out of range reads fall on no uniform at all
        if (uniform >= 0 && uniform < static_cast<std::int64_t>(float_uniform_count)) return static_cast<std::uint32_t>(uniform) + 0x20;
        return 0xFFFFFFFF;
    }

    Vec4 source(std::uint32_t reg) const {
        if (reg < 0x10) return state.input[reg];
        if (reg < 0x20) return state.temp[reg - 0x10];
        auto u = reg - 0x20;
        return u < float_uniform_count ? unit.float_uniforms[u] : Vec4{};
    }

    Vec4 operand(const ShaderUnit::Operand& o) const {
        auto value = source(indexed(o.reg, o.index));
        Vec4 out;
        for (std::size_t c = 0; c < 4; ++c) {
            out[c] = value[o.swizzle[c]];
            if (o.negate) out[c] = -out[c];
        }
        return out;
    }

    void write(std::uint32_t reg, std::uint32_t mask, const Vec4& value) {
        Vec4* slot;
        if (reg < 0x10)
            slot = &state.output[reg];
        else if (reg < 0x20)
            slot = &state.temp[reg - 0x10];
        else
            return;
        for (std::size_t c = 0; c < 4; ++c)
            if (mask & (0b1000u >> c)) (*slot)[c] = value[c];
    }

    static bool compare(std::uint32_t mode, float a, float b) {
        switch (mode) {
            case 0: return a == b;
            case 1: return a != b;
            case 2: return a < b;
            case 3: return a <= b;
            case 4: return a > b;
            case 5: return a >= b;
            default: return true;
        }
    }

    bool condition(std::uint32_t word, std::uint8_t opcode) const {
        auto bool_index = (word >> 22) & 0xF;
        switch (opcode) {
            case op_call: return true;
            case op_callu:
            case op_ifu: return unit.bool_uniforms & (1u << bool_index);
            case op_jmpu: {
                // the low bit of the otherwise unused count inverts the test
                bool set = unit.bool_uniforms & (1u << bool_index);
                return set == ((word & 0xFF & 1) == 0);
            }
            default: {
                bool x = state.condition[0] == (((word >> 25) & 1) != 0);
                bool y = state.condition[1] == (((word >> 24) & 1) != 0);
                switch ((word >> 22) & 0x3) {
                    case 0: return x || y;
                    case 1: return x && y;
                    case 2: return x;
                    default: return y;
                }
            }
        }
    }

    std::uint32_t enter(std::uint32_t start, std::uint32_t count, std::uint32_t return_address) {
        state.blocks.push_back({start + count, return_address, 0, 0, start, false});
        return start;
    }

    void arithmetic(const ShaderUnit::Op& op) {
        auto a = operand(op.sources[0]);
        auto b = operand(op.sources[1]);
        Vec4 r{};
        auto each = [&](auto f) {
            for (std::size_t c = 0; c < 4; ++c) r[c] = f(a[c], b[c]);
        };
        switch (op.opcode) {
            case op_add: each([](float x, float y) { return x + y; }); break;
            case op_mul: each(multiply); break;
            // max(0, NaN) is NaN but max(NaN, 0) is 0, as on the hardware
            case op_max: each([](float x, float y) { return x > y ? x : y; }); break;
            case op_min: each([](float x, float y) { return x < y ? x : y; }); break;
            case op_dp3: r.fill(dot(a, b, 3)); break;
            case op_dp4: r.fill(dot(a, b, 4)); break;
            // dph takes the first operand's w as one
            case op_dph:
            case op_dphi: r.fill(dot(a, b, 3) + b[3]); break;
            case op_mov: r = a; break;
            case op_flr: each([](float x, float) { return std::floor(x); }); break;
            case op_rcp: r.fill(1.0f / a[0]); break;
            case op_rsq: r.fill(1.0f / std::sqrt(a[0])); break;
            case op_ex2: r.fill(std::exp2(a[0])); break;
            case op_lg2: r.fill(std::log2(a[0])); break;
            case op_sge:
            case op_sgei: each([](float x, float y) { return x >= y ? 1.0f : 0.0f; }); break;
            case op_slt:
            case op_slti: each([](float x, float y) { return x < y ? 1.0f : 0.0f; }); break;
            // the attenuation vector (1, d, d², 1/d)
            case op_dst:
            case op_dsti: r = {1.0f, multiply(a[1], b[1]), a[2], b[3]}; break;
            case op_litp:
                state.condition = {a[0] >= 0.0f, a[3] >= 0.0f};
                r = {a[0] > 0.0f ? a[0] : 0.0f, std::clamp(a[1], -127.9961f, 127.9961f), 0.0f, a[3] > 0.0f ? a[3] : 0.0f};
                break;
            case op_mova:
                if (op.mask & 0b1000) state.address[0] = to_i32(a[0]);
                if (op.mask & 0b0100) state.address[1] = to_i32(a[1]);
                return;
            case op_cmp:
                state.condition[0] = compare((op.word >> 24) & 0x7, a[0], b[0]);
                state.condition[1] = compare((op.word >> 21) & 0x7, a[1], b[1]);
                return;
            default: return;  // nothing the unit models
        }
        write(op.destination, op.mask, r);
    }

    void multiply_add(const ShaderUnit::Op& op) {
        auto a = operand(op.sources[0]);
        auto b = operand(op.sources[1]);
        auto c = operand(op.sources[2]);
        Vec4 r;
        for (std::size_t i = 0; i < 4; ++i) r[i] = multiply(a[i], b[i]) + c[i];
        write(op.destination, op.mask, r);
    }
};

}  // namespace

void ShaderUnit::run(ShaderState& state) {
    if (!decoded_) decode();
    state.output = {};
    state.temp.fill(temp_start);
    state.address = {};
    state.condition = {};
    state.blocks.clear();
    Run run{*this, state};
    std::uint32_t pc = entry_point;
    // a program that never ends is cut off rather than hanging
    for (std::uint32_t budget = 0;; ++budget) {
        // finishing a block: go round a loop again, or return past it.
        // several blocks can end at the same address
        while (!state.blocks.empty() && state.blocks.back().end == pc) {
            auto& top = state.blocks.back();
            // the counter wraps, as an int8 increment can take it past either end
            state.address[2] = static_cast<std::int32_t>(static_cast<std::uint32_t>(state.address[2]) + static_cast<std::uint32_t>(top.increment));
            if (top.repeat == 0) {
                pc = top.return_address;
                state.blocks.pop_back();
            } else {
                --top.repeat;
                pc = top.start;
            }
        }
        if (pc >= program_size || budget >= 0x10000) break;
        auto& op = ops_[pc];
        auto word = op.word;
        std::uint32_t next = pc + 1;
        std::uint32_t destination = (word >> 10) & 0xFFF, count = word & 0xFF;
        switch (op.opcode) {
            case op_end: return;
            case op_nop: break;
            case op_setemit:
                state.emit_slot = std::min<std::size_t>((word >> 24) & 0x3, 2);
                state.emit_completes = (word >> 23) & 1;
                break;
            case op_emit:
                state.emit_slots[state.emit_slot] = state.output;
                if (state.emit_completes) state.triangles.push_back(state.emit_slots);
                break;
            case op_mad:
            case op_madi: run.multiply_add(op); break;
            case op_call:
            case op_callu:
            case op_callc:
                if (run.condition(word, op.opcode)) next = run.enter(destination, count, pc + 1);
                break;
            case op_jmpc:
            case op_jmpu:
                if (run.condition(word, op.opcode)) next = destination;
                break;
            case op_ifc:
            case op_ifu:
                if (run.condition(word, op.opcode))
                    // the body, then past the else block
                    next = run.enter(pc + 1, destination - std::min(pc + 1, destination), destination + count);
                else
                    // the else block, then on after it
                    next = run.enter(destination, count, destination + count);
                break;
            case op_loop: {
                auto& integers = int_uniforms[(word >> 22) & 0x3];
                state.address[2] = integers[1];
                state.blocks.push_back({destination + 1, destination + 1, integers[0],
                                        static_cast<std::int8_t>(integers[2]), pc + 1, true});
                break;
            }
            case op_break:
            case op_breakc:
                if (op.opcode == op_break || run.condition(word, op.opcode)) {
                    // out of the innermost loop, and any if or call inside it
                    while (!state.blocks.empty()) {
                        auto block = state.blocks.back();
                        state.blocks.pop_back();
                        if (block.is_loop) {
                            next = block.return_address;
                            break;
                        }
                    }
                }
                break;
            default: run.arithmetic(op);
        }
        pc = next;
    }
}

std::array<Vec4, output_registers> ShaderUnit::run(const std::array<Vec4, input_registers>& inputs) {
    ShaderState state;
    state.input = inputs;
    run(state);
    return state.output;
}

}  // namespace kabegami::pica
