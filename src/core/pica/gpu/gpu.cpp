#include "core/pica/gpu/gpu.hpp"

namespace kabegami::pica {
namespace {

// the bits of a register a command's byte enable mask writes
constexpr std::uint32_t byte_mask(std::uint32_t mask) {
    std::uint32_t bits = 0;
    for (int b = 0; b < 4; ++b)
        if (mask & (1u << b)) bits |= 0xFFu << (8 * b);
    return bits;
}

std::vector<std::uint32_t> read_words(const Memory& memory, std::uint32_t paddr, std::uint32_t size) {
    std::vector<std::uint32_t> words(size / 4);
    for (std::size_t i = 0; i < words.size(); ++i) words[i] = memory.read_u32(paddr + static_cast<std::uint32_t>(i * 4));
    return words;
}

constexpr std::size_t vsh_end = reg::vsh_block + reg::shader_block_size;
constexpr std::size_t gsh_end = reg::gsh_block + reg::shader_block_size;

}  // namespace

Gpu::Gpu() { fixed_attributes.fill({0, 0, 0, 1}); }

void Gpu::run_command_list(Memory& memory, std::uint32_t paddr, std::uint32_t size) {
    auto words = read_words(memory, paddr, size);
    // buffers jump into each other; real lists stay far below this
    std::uint32_t jumps = 0;
    jump_pending_ = false;
    std::size_t index = 0;
    while (index + 1 < words.size()) {
        auto data = words[index], header = words[index + 1];
        index += 2;
        // a parameter, then the header: the register in bits 0-15, the byte
        // enable mask in 16-19, the extra parameters in 20-27, and bit 31 for
        // consecutive registers
        std::size_t reg_index = header & 0xFFFF;
        auto mask = (header >> 16) & 0xF;
        std::size_t extra = (header >> 20) & 0xFF;
        bool consecutive = header & 0x80000000;
        // no register lives past 0x300; one there means the list is not one
        if (reg_index >= reg::count) break;
        write(memory, reg_index, data, mask);
        auto burst = std::min(extra, words.size() - index);
        for (std::size_t i = 0; i < burst; ++i) {
            auto target = consecutive ? reg_index + 1 + i : reg_index;
            if (target < reg::count) write(memory, target, words[index + i], mask);
        }
        index += burst;
        // an odd number of extra parameters is padded to keep commands 8-byte aligned
        if (extra % 2) ++index;
        index = std::min(index, words.size());
        if (jump_pending_) {
            jump_pending_ = false;
            if (++jumps > 0x10000) break;
            words = read_words(memory, jump_address_, jump_size_);
            index = 0;
        }
    }
    flush_immediate(memory);
}

void Gpu::flush_immediate(Memory& memory) {
    if (immediate_.empty()) return;
    auto vertices = std::move(immediate_);
    immediate_.clear();
    draws.push_back(draw_immediate(registers, vertex_shader, vertices, memory));
}

void Gpu::configure_shader(ShaderUnit& unit, std::size_t offset, std::uint32_t value) {
    namespace s = reg::shader;
    if (offset == s::bool_uniform) {
        unit.bool_uniforms = static_cast<std::uint16_t>(value);
    } else if (offset >= s::int_uniform_i0 && offset <= s::int_uniform_i0 + 3) {
        for (std::size_t k = 0; k < 4; ++k) unit.int_uniforms[offset - s::int_uniform_i0][k] = static_cast<std::uint8_t>(value >> (8 * k));
    } else if (offset == s::entry_point) {
        unit.entry_point = value & (program_size - 1);
    } else if (offset == s::float_uniform_index) {
        unit.set_float_uniform_index(value);
    } else if (offset >= s::float_uniform_data0 && offset <= s::float_uniform_data0 + 7) {
        unit.upload_float_uniform(value);
    } else if (offset == s::code_transfer_index) {
        unit.program_write_offset = value & (program_size - 1);
    } else if (offset >= s::code_transfer_data0 && offset <= s::code_transfer_data0 + 7) {
        unit.upload_program(value);
    } else if (offset == s::opdescs_index) {
        unit.descriptor_write_offset = value & (descriptor_size - 1);
    } else if (offset >= s::opdescs_data0 && offset <= s::opdescs_data0 + 7) {
        unit.upload_descriptor(value);
    }
}

void Gpu::write(Memory& memory, std::size_t reg_index, std::uint32_t value, std::uint32_t mask) {
    if (reg_index >= reg::count) return;
    // immediate-mode vertices are drawn with the state they were sent
    // under, so a write to anything else draws them first
    bool fixed_data = reg_index >= reg::fixedattrib_data0 && reg_index <= reg::fixedattrib_data0 + 2;
    if (!fixed_data) flush_immediate(memory);
    auto bits = byte_mask(mask);
    auto updated = (registers[reg_index] & ~bits) | (value & bits);
    registers[reg_index] = updated;

    if (reg_index == reg::drawarrays || reg_index == reg::drawelements) {
        draws.push_back(draw(registers, vertex_shader, fixed_attributes, memory, reg_index == reg::drawelements));
    } else if (reg_index == reg::cmdbuf_jump0 || reg_index == reg::cmdbuf_jump1) {
        auto channel = reg_index - reg::cmdbuf_jump0;
        jump_size_ = (registers[reg::cmdbuf_size0 + channel] & 0x1FFFFF) * 8;
        jump_address_ = (registers[reg::cmdbuf_addr0 + channel] & 0x1FFFFFFF) * 8;
        jump_pending_ = true;
    } else if (reg_index == reg::fixedattrib_index) {
        fixed_words_ = 0;
        immediate_next_ = 0;
    } else if (fixed_data) {
        fixed_staging_[fixed_words_++] = updated;
        if (fixed_words_ == 3) {
            fixed_words_ = 0;
            // packed as float uniforms are, four float24s in three words, w first
            auto [a, b, c] = fixed_staging_;
            Vec4 attribute{decode_float24(c & 0xFFFFFF), decode_float24((b & 0xFFFF) << 8 | c >> 24),
                           decode_float24((a & 0xFF) << 16 | b >> 16), decode_float24(a >> 8)};
            auto index = registers[reg::fixedattrib_index] & 0xF;
            if (index < 15) {
                fixed_attributes[index] = attribute;
                // on to the next, so a burst sets several
                registers[reg::fixedattrib_index] = (registers[reg::fixedattrib_index] & ~0xFu) | (index + 1);
            } else {
                // immediate mode: the attributes in order, the last one making a vertex
                immediate_attributes_[immediate_next_] = attribute;
                auto last = static_cast<std::size_t>(registers[reg::vsh_num_attr] & 0xF);
                if (immediate_next_ < last) {
                    ++immediate_next_;
                } else {
                    immediate_next_ = 0;
                    immediate_.push_back(immediate_attributes_);
                }
            }
        }
    } else if (reg_index >= reg::gsh_block && reg_index < gsh_end) {
        configure_shader(geometry_shader, reg_index - reg::gsh_block, updated);
    } else if (reg_index >= reg::vsh_block && reg_index < vsh_end) {
        auto offset = reg_index - reg::vsh_block;
        configure_shader(vertex_shader, offset, updated);
        // the geometry unit takes the vertex unit's program and descriptors
        // too, unless the list sets it up on its own
        namespace s = reg::shader;
        bool program = (offset >= s::code_transfer_index && offset <= s::code_transfer_data0 + 7) ||
                       (offset >= s::opdescs_index && offset <= s::opdescs_data0 + 7);
        if (program && !(registers[reg::vsh_com_mode] & 1)) configure_shader(geometry_shader, offset, updated);
    }
}

void Gpu::memory_fill(Memory& memory, std::uint32_t start, std::uint32_t end, std::uint32_t value, std::uint32_t width) {
    if (end <= start) return;
    std::size_t pattern = width == 2 || width == 3 ? width : 4;
    std::vector<std::uint8_t> bytes(end - start);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(value >> (8 * (i % pattern)));
    memory.write(start, bytes);
}

image::Image read_color_buffer(const Memory& memory, std::uint32_t addr, std::uint32_t width, std::uint32_t height, ColorFormat format) {
    image::Image out(static_cast<int>(width), static_cast<int>(height));
    auto bpp = bytes_per_pixel(format);
    std::uint8_t pixel[4];
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            memory.read(addr + morton_offset(x, y, width, bpp), {pixel, bpp});
            auto rgba = decode_pixel(format, pixel);
            std::copy(rgba.begin(), rgba.end(), out.at(static_cast<int>(x), static_cast<int>(y)));
        }
    }
    return out;
}

image::Image rotate_counter_clockwise(const image::Image& picture) {
    image::Image out(picture.height, picture.width);
    // the source's right column becomes the top row
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) std::copy_n(picture.at(picture.width - 1 - y, x), 4, out.at(x, y));
    return out;
}

}  // namespace kabegami::pica
