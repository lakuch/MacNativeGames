// Tegra 块线性 ↔ 线性。级 / 层偏移与逐级块高块深一律取 nvn/nvn_tex.c（nvn_tex_level_offset / nvn_tex_layout / nvn_tex_level_tiles）。
// Tegra block-linear ↔ linear. Level / layer offsets and per-level block height and block depth always come from nvn/nvn_tex.c (nvn_tex_level_offset / nvn_tex_layout / nvn_tex_level_tiles).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../nvn/nvn_tex.h"

// 把第 level 级、第 layer 层（数组 / 立方体的层；非分层贴图取 0）从整张贴图存储 src_storage 解到线性 dst_linear。
// dst 按块行排：每行 dst_pitch 字节（≥ 本级块宽 × 每块字节），压缩格式一行 = 一行块。
// 3D 贴图：一次解出本级全部深度切片，切片 k 位于 dst_linear + k × dst_pitch × 本级块行数。
// 线性 / 线性渲染目标按行距复制；不支持多重采样、BUFFER，返回 -1。成功返回 0。
// Decode level number level, layer number layer (layer of an array / cube; 0 for non-layered textures) from the whole-texture storage src_storage into linear dst_linear.
// dst is laid out by block rows: dst_pitch bytes per row (≥ width in blocks of this level × bytes per block); for compressed formats one row = one row of blocks.
// 3D textures: all depth slices of this level are decoded at once; slice k is at dst_linear + k × dst_pitch × block row count of this level.
// Linear / linear-render-target storage is copied by pitch. Multisampling and BUFFER return -1; success returns 0.
int nvn_deswizzle(const nvn_texdesc *d, unsigned level, unsigned layer, const void *src_storage, void *dst_linear, size_t dst_pitch);
// 反向（单测往返用）：线性 src_linear → 写进整张贴图存储 dst_storage 的对应级 / 层
// Reverse (for unit test round trips): linear src_linear → written into the corresponding level / layer of the whole-texture storage dst_storage
int nvn_swizzle(const nvn_texdesc *d, unsigned level, unsigned layer, const void *src_linear, size_t src_pitch, void *dst_storage);
// 单点地址：本级本层内，线性 (x 字节, y 块行, z 切片) → 相对本级本层起点的块线性字节偏移（单测按手算核）
// Single-point address: within this level and layer, linear (x byte, y block row, z slice) → block-linear byte offset relative to the start of this level and layer (unit tests verify against hand calculation)
uint64_t nvn_bl_offset(uint32_t x, uint32_t y, uint32_t z, uint32_t row_bytes, uint32_t rows, unsigned tileH, unsigned tileD);

// 区域搬运：本级本层内 (x0 字节, y0 块行, z0 切片) 起、wb 字节 × hb 块行 × nz 切片，与线性缓冲 lin（行距 row、切片距 img）互拷。
// storage 为 NULL 时只算范围。lo / hi：触到的存储字节范围 [lo, hi)（相对整张贴图存储起点）。不支持的布局（线性等）返回 -1
// Region copy: within this level and layer, from (x0 bytes, y0 block rows, z0 slices), wb bytes x hb block rows x nz slices,
// to or from the linear buffer lin (row pitch row, slice pitch img). With storage NULL only the range is computed. lo / hi:
// the storage bytes touched, [lo, hi) from the start of the whole texture storage. Unsupported layouts (linear etc.) return -1
int nvn_bl_region(const nvn_texdesc *d, unsigned level, unsigned layer, uint8_t *storage, uint32_t x0, uint32_t y0, uint32_t z0,
                  uint32_t wb, uint32_t hb, uint32_t nz, uint8_t *lin, size_t row, size_t img, int to_linear, uint64_t *lo, uint64_t *hi);

int nvn_deswizzle_geom(const nvn_texdesc *d, unsigned level, unsigned layer,
                       uint32_t *row_bytes, uint32_t *rows, uint32_t *slices,
                       uint64_t *base, unsigned *tileH, unsigned *tileD, uint32_t *bpb);
