// the PICA200's shader unit: its program and operand descriptor memory, its
// uniforms and their upload cursors, and an interpreter that runs a program on
// one vertex. a port of Zakuro's zakuro-gpu (src/shader/mod.rs and isa.rs, by
// the same author, MIT), which games check, with the same float rules: the
// multiply that gives 0 for 0 times infinity, temporaries that start at
// (0, 0, 0, 1), and the hardware's NaN behaviour of max and min.
//
// what the GPU drives it by (see pica/gpu): GPUREG_VSH_CODETRANSFER_* for the
// program, _OPDESCS_* for the descriptors, _FLOATUNIFORM_INDEX and _DATA,
// _INTUNIFORM_I0-I3, _BOOLUNIFORM and _ENTRYPOINT.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kabegami::pica {

constexpr std::size_t program_size = 4096;
constexpr std::size_t descriptor_size = 128;
constexpr std::size_t float_uniform_count = 96;
constexpr std::size_t input_registers = 16;
constexpr std::size_t output_registers = 16;
constexpr std::size_t temp_registers = 16;

using Vec4 = std::array<float, 4>;

// the 24-bit floats of uniforms sent packed and of the DVLE constants: a
// sign, a 7-bit exponent biased by 63 and a 16-bit mantissa; zero exponents
// read as zero and the top one as infinity (Zakuro isa::decode_float24).
float decode_float24(std::uint32_t raw);
// the nearest float24 to a float, truncating the mantissa as the GPU does
// when it takes a 32-bit float into a 24-bit register.
std::uint32_t encode_float24(float value);

// the shader's multiply: 0 for zero times infinity rather than NaN.
float multiply(float a, float b);

struct ShaderState;

class ShaderUnit {
public:
    std::array<std::uint32_t, program_size> program{};
    std::array<std::uint32_t, descriptor_size> descriptors{};
    std::array<Vec4, float_uniform_count> float_uniforms{};
    std::array<std::array<std::uint8_t, 4>, 4> int_uniforms{};
    std::uint16_t bool_uniforms = 0;
    std::uint32_t entry_point = 0;
    std::size_t program_write_offset = 0;
    std::size_t descriptor_write_offset = 0;

    void upload_program(std::uint32_t word);
    void upload_descriptor(std::uint32_t word);
    // starts a float uniform upload at the register raw names; bit 31 picks
    // four 32-bit words per vector, else three words of packed float24s.
    void set_float_uniform_index(std::uint32_t raw);
    void upload_float_uniform(std::uint32_t word);

    // runs the program from the entry point on state's inputs.
    void run(ShaderState& state);
    // the same for one vertex's inputs, giving its outputs.
    std::array<Vec4, output_registers> run(const std::array<Vec4, input_registers>& inputs);

    struct Operand {
        std::uint32_t reg = 0;
        std::array<std::uint8_t, 4> swizzle{0, 1, 2, 3};
        bool negate = false;
        std::uint32_t index = 0;  // the address register that offsets it, 0 for none
    };
    struct Op {
        std::uint32_t word = 0;
        std::uint8_t opcode = 0;  // the raw 6-bit opcode, normalised for cmp and mad
        std::uint32_t destination = 0;
        std::uint32_t mask = 0;  // the most significant of four bits is x
        std::array<Operand, 3> sources;
    };

private:
    void decode();
    std::vector<Op> ops_;
    bool decoded_ = false;
    std::size_t float_index_ = 0;
    std::size_t float_component_ = 0;
    bool float_wide_ = false;
    std::array<std::uint32_t, 4> float_staging_{};
};

// what one run of the program keeps.
struct ShaderState {
    std::array<Vec4, input_registers> input{};
    std::array<Vec4, output_registers> output{};
    std::array<Vec4, temp_registers> temp{};
    std::array<std::int32_t, 3> address{};  // a0.x, a0.y, aL
    std::array<bool, 2> condition{};
    struct Block {
        std::uint32_t end, return_address, repeat;
        std::int32_t increment;
        std::uint32_t start;
        bool is_loop;
    };
    std::vector<Block> blocks;
    // geometry shaders: the slots emit fills and the triangles finished
    std::array<std::array<Vec4, output_registers>, 3> emit_slots{};
    std::size_t emit_slot = 0;
    bool emit_completes = false;
    std::vector<std::array<std::array<Vec4, output_registers>, 3>> triangles;
};

}  // namespace kabegami::pica
