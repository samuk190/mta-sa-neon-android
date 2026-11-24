#pragma once
// DDS images for dxCreateTexture. Desktop hands DDS files to D3DX; the phone's
// image decoder has no DDS, and S3TC is not available on most mobile GPUs, so
// the top mip level is decoded to 32-bit ARGB here.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace WorldRPG::Script {

// Decodes the first mip level of a DDS file: DXT1, DXT3, DXT5 (also as DX10
// BC1-BC3) and uncompressed 32/24-bit RGB(A). Pixels are 0xAARRGGBB, row by row.
// False for other formats or a truncated file.
bool DecodeDds(const uint8_t* data, size_t size, std::vector<uint32_t>& argb, unsigned& width, unsigned& height);

}  // namespace WorldRPG::Script
