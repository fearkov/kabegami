// the PICA200's internal registers, as word indices of the file command lists
// write (3dbrew's GPU/Internal Registers), the ones Kabegami's software GPU
// reads. Zakuro's zakuro-gpu (registers.rs) names the same ones.
#pragma once

#include <cstddef>

namespace kabegami::pica::reg {

constexpr std::size_t count = 0x300;

constexpr std::size_t finalize = 0x0010;
constexpr std::size_t face_culling = 0x0040;
constexpr std::size_t viewport_width = 0x0041;  // half the width, float24
constexpr std::size_t viewport_width_inverse = 0x0042;
constexpr std::size_t viewport_height = 0x0043;  // half the height, float24
constexpr std::size_t viewport_height_inverse = 0x0044;
constexpr std::size_t depthmap_scale = 0x004D;   // float24
constexpr std::size_t depthmap_offset = 0x004E;  // float24
constexpr std::size_t outmap_total = 0x004F;
constexpr std::size_t outmap_o0 = 0x0050;  // to 0x0056
constexpr std::size_t viewport_xy = 0x0068;
constexpr std::size_t depthmap_enable = 0x006D;  // 1 a z-buffer, 0 a w-buffer
constexpr std::size_t outattr_mode = 0x0064;
constexpr std::size_t outattr_clock = 0x006F;

// the texture units: a config register, then each unit's block
constexpr std::size_t texunit_config = 0x0080;
constexpr std::size_t texunit0 = 0x0081;  // border, dim, param, lod, addr1-6, shadow, type at +13
constexpr std::size_t texunit1 = 0x0091;  // border, dim, param, lod, addr, type at +5
constexpr std::size_t texunit2 = 0x0099;
constexpr std::size_t texunit_bases[3] = {texunit0, texunit1, texunit2};

// the texture combiners, five registers a stage
constexpr std::size_t texenv_stages[6] = {0x00C0, 0x00C8, 0x00D0, 0x00D8, 0x00F0, 0x00F8};
constexpr std::size_t texenv_update_buffer = 0x00E0;
constexpr std::size_t texenv_buffer_color = 0x00FD;

// the output merger
constexpr std::size_t color_operation = 0x0100;
constexpr std::size_t blend_func = 0x0101;
constexpr std::size_t logic_op = 0x0102;
constexpr std::size_t blend_color = 0x0103;
constexpr std::size_t alpha_test = 0x0104;
constexpr std::size_t stencil_test = 0x0105;
constexpr std::size_t stencil_op = 0x0106;
constexpr std::size_t depth_color_mask = 0x0107;
constexpr std::size_t framebuffer_invalidate = 0x0110;
constexpr std::size_t framebuffer_flush = 0x0111;
constexpr std::size_t colorbuffer_read = 0x0112;
constexpr std::size_t colorbuffer_write = 0x0113;
constexpr std::size_t depthbuffer_read = 0x0114;
constexpr std::size_t depthbuffer_write = 0x0115;
constexpr std::size_t depthbuffer_format = 0x0116;
constexpr std::size_t colorbuffer_format = 0x0117;
constexpr std::size_t depthbuffer_location = 0x011C;
constexpr std::size_t colorbuffer_location = 0x011D;
constexpr std::size_t framebuffer_dimensions = 0x011E;

// the vertex arrays
constexpr std::size_t attribbuffers_location = 0x0200;
constexpr std::size_t attribbuffers_format_low = 0x0201;
constexpr std::size_t attribbuffers_format_high = 0x0202;
constexpr std::size_t attribbuffer0 = 0x0203;  // twelve loaders of three
constexpr std::size_t loader_count = 12;
constexpr std::size_t indexbuffer_config = 0x0227;
constexpr std::size_t num_vertices = 0x0228;
constexpr std::size_t geostage_config = 0x0229;
constexpr std::size_t vertex_offset = 0x022A;
constexpr std::size_t drawarrays = 0x022E;
constexpr std::size_t drawelements = 0x022F;
constexpr std::size_t fixedattrib_index = 0x0232;
constexpr std::size_t fixedattrib_data0 = 0x0233;  // to 0x0235
constexpr std::size_t cmdbuf_size0 = 0x0238;
constexpr std::size_t cmdbuf_addr0 = 0x023A;
constexpr std::size_t cmdbuf_jump0 = 0x023C;
constexpr std::size_t cmdbuf_jump1 = 0x023D;
constexpr std::size_t vsh_num_attr = 0x0242;
constexpr std::size_t vsh_com_mode = 0x0244;
constexpr std::size_t primitive_config = 0x025E;

// a shader unit's block, the geometry unit's at 0x280 and the vertex unit's
// at 0x2B0, and the offsets inside one
constexpr std::size_t gsh_block = 0x0280;
constexpr std::size_t vsh_block = 0x02B0;
constexpr std::size_t shader_block_size = 0x2E;
namespace shader {
constexpr std::size_t bool_uniform = 0x00;
constexpr std::size_t int_uniform_i0 = 0x01;  // to i3 at 0x04
constexpr std::size_t input_config = 0x09;
constexpr std::size_t entry_point = 0x0A;
constexpr std::size_t attributes_permutation_low = 0x0B;
constexpr std::size_t attributes_permutation_high = 0x0C;
constexpr std::size_t outmap_mask = 0x0D;
constexpr std::size_t code_transfer_end = 0x0F;
constexpr std::size_t float_uniform_index = 0x10;
constexpr std::size_t float_uniform_data0 = 0x11;  // to 0x18
constexpr std::size_t code_transfer_index = 0x1B;
constexpr std::size_t code_transfer_data0 = 0x1C;  // to 0x23
constexpr std::size_t opdescs_index = 0x25;
constexpr std::size_t opdescs_data0 = 0x26;  // to 0x2D
}  // namespace shader

}  // namespace kabegami::pica::reg
