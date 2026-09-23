// TextureDDS.h - D3DX-free DDS texture pipeline (DX9).
//
// Replaces the D3DX texture services the engine used:
//   D3DXGetImageInfoFromFileInMemory -> xrDDS_Parse
//   D3DXCreateTextureFromFileInMemoryEx (as-is) -> xrDDS_Load2D / xrDDS_LoadCube
//   D3DXCreateTextureFromFileInMemoryEx (forced A8R8G8B8) -> xrDDS_LoadAsARGB
//   D3DXCreateTexture / CreateVolumeTexture -> device calls directly
//   D3DXLoadSurfaceFromSurface (same format) -> xrSurface_Copy
//   A8R8G8B8 -> DXT5 -> xrBC_EncodeLevel_DXT5 (stb_dxt)
//   DXT5 -> A8R8G8B8 -> xrBC_DecodeLevel (exact BC1/2/3 decode)
//   D3DXComputeNormalMap -> xrNormalMap_Generate (Sobel, wrap)
//   D3DXSaveTextureToFile (debug) -> xrDDS_Save2D
//
// Load failures return E_FAIL so callers fall back to ed_not_existing_texture,
// exactly like the D3DX failure path did.
#pragma once

#define XR_DDS_MAGIC 0x20534444 // "DDS "
#define XR_DDS_DXT10 0x30315844 // "DX10"

struct XR_DDS_PixelFormat
{
    u32 dwSize;
    u32 dwFlags;
    u32 dwFourCC;
    u32 dwRGBBitCount;
    u32 dwRBitMask;
    u32 dwGBitMask;
    u32 dwBBitMask;
    u32 dwABitMask;
};

struct XR_DDS_Header
{
    u32 dwSize;
    u32 dwFlags;
    u32 dwHeight;
    u32 dwWidth;
    u32 dwPitchOrLinearSize;
    u32 dwDepth;
    u32 dwMipMapCount;
    u32 dwReserved1[11];
    XR_DDS_PixelFormat ddspf;
    u32 dwCaps;
    u32 dwCaps2;
    u32 dwCaps3;
    u32 dwCaps4;
    u32 dwReserved2;
};

struct XR_DDSInfo
{
    u32 width;
    u32 height;
    u32 depth; // >1 for volume textures
    u32 mips;
    u32 faces; // 1 or 6
    D3DFORMAT format;
    u32 dataOffset;
};

// Block geometry of a D3DFORMAT. Returns false for unsupported formats.
inline bool xrDDS_FormatInfo(D3DFORMAT fmt, u32* blockBytes, u32* bytesPerPixel)
{
    switch (fmt)
    {
    case D3DFMT_DXT1: *blockBytes = 8; *bytesPerPixel = 0; return true;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5: *blockBytes = 16; *bytesPerPixel = 0; return true;
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8: *blockBytes = 0; *bytesPerPixel = 4; return true;
    case D3DFMT_L8:
    case D3DFMT_A8: *blockBytes = 0; *bytesPerPixel = 1; return true;
    default: return false;
    }
}

// Byte size of one mip level (width/height already clamped to >= 1).
inline u32 xrDDS_LevelSize(u32 w, u32 h, u32 blockBytes, u32 bytesPerPixel)
{
    if (blockBytes)
        return ((w + 3) / 4) * ((h + 3) / 4) * blockBytes;
    return w * h * bytesPerPixel;
}

// Row stride of one mip level in bytes (file layout).
inline u32 xrDDS_LevelRowBytes(u32 w, u32 h, u32 blockBytes, u32 bytesPerPixel)
{
    if (blockBytes)
        return ((w + 3) / 4) * blockBytes;
    (void)h;
    return w * bytesPerPixel;
}

inline HRESULT xrDDS_Parse(const void* fileData, u32 fileSize, XR_DDSInfo* info)
{
    if (!fileData || !info || fileSize < 4 + sizeof(XR_DDS_Header))
        return E_FAIL;
    const u8* base = (const u8*)fileData;
    u32 magic = 0;
    CopyMemory(&magic, base, 4);
    if (magic != XR_DDS_MAGIC)
        return E_FAIL;
    XR_DDS_Header h;
    CopyMemory(&h, base + 4, sizeof(h));
    if (h.dwSize != 124 || h.ddspf.dwSize != 32 || h.dwWidth == 0 || h.dwHeight == 0)
        return E_FAIL;

    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    if (h.ddspf.dwFlags & 0x4) // DDPF_FOURCC
    {
        if (h.ddspf.dwFourCC == XR_DDS_DXT10)
            return E_FAIL; // DX10 extension header: not used by game content
        switch (h.ddspf.dwFourCC)
        {
        case 0x31545844: fmt = D3DFMT_DXT1; break; // "DXT1"
        case 0x32545844: // "DXT2"
        case 0x33545844: fmt = D3DFMT_DXT3; break; // "DXT3"
        case 0x34545844: // "DXT4"
        case 0x35545844: fmt = D3DFMT_DXT5; break; // "DXT5"
        default: return E_FAIL;
        }
    }
    else if (h.ddspf.dwFlags & 0x40) // DDPF_RGB
    {
        if (h.ddspf.dwRGBBitCount == 32
            && h.ddspf.dwRBitMask == 0x00FF0000
            && h.ddspf.dwGBitMask == 0x0000FF00
            && h.ddspf.dwBBitMask == 0x000000FF)
            fmt = (h.ddspf.dwABitMask == 0xFF000000) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
        else if (h.ddspf.dwRGBBitCount == 8)
            fmt = D3DFMT_L8;
        else
            return E_FAIL;
    }
    else if (h.ddspf.dwFlags & 0x2) // DDPF_ALPHA: A8 font/coverage textures
    {
        if (h.ddspf.dwRGBBitCount == 8)
            fmt = D3DFMT_A8;
        else
            return E_FAIL;
    }
    else
    {
        return E_FAIL;
    }

    u32 blockBytes = 0, bytesPerPixel = 0;
    if (!xrDDS_FormatInfo(fmt, &blockBytes, &bytesPerPixel))
        return E_FAIL;

    u32 mips = (h.dwFlags & 0x20000) ? h.dwMipMapCount : 1; // DDSD_MIPMAPCOUNT
    if (mips == 0)
        mips = 1;
    const bool volume = (h.dwCaps2 & 0x200000) != 0; // DDSCAPS2_VOLUME
    u32 depth = volume ? h.dwDepth : 1;
    if (depth == 0)
        depth = 1;
    u32 faces = (h.dwCaps2 & 0x200) ? 6 : 1; // DDSCAPS2_CUBEMAP
    if (faces == 6 && (h.dwCaps2 & 0xFC00) != 0xFC00)
        return E_FAIL; // partial cube
    if (volume && faces != 1)
        return E_FAIL;

    // Validate the data actually present (guards corrupt files).
    // Volume layout: for each mip, all depth slices contiguously.
    u32 need = 0;
    u32 w = h.dwWidth, hh = h.dwHeight, dd = depth;
    for (u32 f = 0; f < faces; ++f)
    {
        w = h.dwWidth;
        hh = h.dwHeight;
        dd = depth;
        for (u32 m = 0; m < mips; ++m)
        {
            need += _max(1u, dd) * xrDDS_LevelSize(_max(1u, w), _max(1u, hh), blockBytes, bytesPerPixel);
            w /= 2;
            hh /= 2;
            dd /= 2;
        }
    }
    if (fileSize < 4 + 124 + need)
        return E_FAIL;

    info->width = h.dwWidth;
    info->height = h.dwHeight;
    info->depth = depth;
    info->mips = mips;
    info->faces = faces;
    info->format = fmt;
    info->dataOffset = 4 + 124;
    return S_OK;
}

// Next power of two >= v (v >= 1).
inline u32 xr_POTUp(u32 v)
{
    u32 p = 1;
    while (p < v)
        p *= 2;
    return p;
}

// D3DX "bound to device restrictions" normalization, proven by probe:
// BC formats go to per-dim max(nextPOT, 4); uncompressed stays as stored.
inline void xrDDS_NormalizeDims(D3DFORMAT fmt, u32 w, u32 h, u32* outW, u32* outH)
{
    u32 blockBytes = 0, bytesPerPixel = 0;
    if (xrDDS_FormatInfo(fmt, &blockBytes, &bytesPerPixel) && blockBytes)
    {
        *outW = _max(xr_POTUp(w), 4u);
        *outH = _max(xr_POTUp(h), 4u);
    }
    else
    {
        *outW = w;
        *outH = h;
    }
}

// Bilinear resample of A8R8G8B8 (BGRA bytes) with clamped edges.
inline void xrResample_Bilinear(const u8* srcBits, u32 srcPitch, u32 sw, u32 sh,
                                u8* dstBits, u32 dstPitch, u32 dw, u32 dh)
{
    for (u32 y = 0; y < dh; ++y)
    {
        const float sy = (y + 0.5f) * sh / dh - 0.5f;
        u32 y0 = (u32)clampr(iFloor(sy), 0, (int)sh - 1);
        u32 y1 = _min(y0 + 1, sh - 1);
        const float fy = clampr(sy - y0, 0.f, 1.f);
        for (u32 x = 0; x < dw; ++x)
        {
            const float sx = (x + 0.5f) * sw / dw - 0.5f;
            u32 x0 = (u32)clampr(iFloor(sx), 0, (int)sw - 1);
            u32 x1 = _min(x0 + 1, sw - 1);
            const float fx = clampr(sx - x0, 0.f, 1.f);
            u8* dst = dstBits + y * dstPitch + x * 4;
            for (int c = 0; c < 4; ++c)
            {
                const float s00 = srcBits[y0 * srcPitch + x0 * 4 + c];
                const float s10 = srcBits[y0 * srcPitch + x1 * 4 + c];
                const float s01 = srcBits[y1 * srcPitch + x0 * 4 + c];
                const float s11 = srcBits[y1 * srcPitch + x1 * 4 + c];
                const float v = (s00 * (1 - fx) + s10 * fx) * (1 - fy)
                              + (s01 * (1 - fx) + s11 * fx) * fy;
                dst[c] = (u8)clampr(iFloor(v + 0.5f), 0, 255);
            }
        }
    }
}

// Decodes one level of BC1/2/3 bits to A8R8G8B8 (BGRA bytes).
inline void xrBC_DecodeLevel(D3DFORMAT fmt, const u8* srcBits, u32 srcRowBytes,
                             u8* dstBits, u32 dstPitch, u32 w, u32 h);

// Encodes one ARGB image (BGRA bytes) to BC blocks (see definition below).
inline void xrBC_EncodeLevel(D3DFORMAT fmt, const u8* srcARGB, u32 srcPitch,
                             u32 w, u32 h, u8* dstBlocks, u32 dstPitch);

// Decodes one file level to A8R8G8B8 (BGRA bytes). Supports the formats the
// engine forces to ARGB; returns false for anything else.
inline bool xrDDS_DecodeLevelToARGB(D3DFORMAT fmt, const u8* srcBits, u32 srcRowBytes,
                                    u32 w, u32 h, u8* dstARGB, u32 dstPitch)
{
    if (fmt == D3DFMT_A8R8G8B8)
    {
        for (u32 y = 0; y < h; ++y)
            CopyMemory(dstARGB + y * dstPitch, srcBits + y * w * 4, w * 4);
        return true;
    }
    if (fmt == D3DFMT_X8R8G8B8)
    {
        for (u32 y = 0; y < h; ++y)
        {
            u8* row = dstARGB + y * dstPitch;
            CopyMemory(row, srcBits + y * w * 4, w * 4);
            for (u32 x = 0; x < w; ++x)
                row[4 * x + 3] = 255;
        }
        return true;
    }
    if (fmt == D3DFMT_L8 || fmt == D3DFMT_A8)
    {
        const bool isAlpha = (fmt == D3DFMT_A8);
        for (u32 y = 0; y < h; ++y)
        {
            u8* row = dstARGB + y * dstPitch;
            for (u32 x = 0; x < w; ++x)
            {
                const u8 v = srcBits[y * w + x];
                row[4 * x + 0] = isAlpha ? 0 : v;
                row[4 * x + 1] = isAlpha ? 0 : v;
                row[4 * x + 2] = isAlpha ? 0 : v;
                row[4 * x + 3] = isAlpha ? v : 255;
            }
        }
        return true;
    }
    if (fmt == D3DFMT_DXT1 || fmt == D3DFMT_DXT3 || fmt == D3DFMT_DXT5)
    {
        u32 blockBytes = 0, bytesPerPixel = 0;
        xrDDS_FormatInfo(fmt, &blockBytes, &bytesPerPixel);
        xrBC_DecodeLevel(fmt, srcBits, srcRowBytes, dstARGB, dstPitch, w, h);
        return true;
    }
    return false;
}

// Copies one level from file-layout bits to a locked rect (same format).
inline void xrDDS_CopyLevel(u8* dstBits, u32 dstPitch, const u8* srcBits,
                            u32 w, u32 h, u32 blockBytes, u32 bytesPerPixel)
{
    u32 rowBytes = xrDDS_LevelRowBytes(w, h, blockBytes, bytesPerPixel);
    u32 rows = blockBytes ? ((h + 3) / 4) : h;
    for (u32 y = 0; y < rows; ++y)
        CopyMemory(dstBits + y * dstPitch, srcBits + y * rowBytes, rowBytes);
}

#ifdef __cplusplus
extern "C" {
#endif
void stb_compress_dxt_block(unsigned char* dest, const unsigned char* src, int alpha, int mode);
#ifdef __cplusplus
}
#endif
#define XR_DXT_HIGHQUAL 2

inline HRESULT xrDDS_Load2D(IDirect3DDevice9* dev, const void* fileData, u32 fileSize,
                            D3DPOOL pool, IDirect3DTexture9** out)
{
    XR_DDSInfo info;
    if (FAILED(xrDDS_Parse(fileData, fileSize, &info)) || info.faces != 1)
        return E_FAIL;
    // Volumes load slice 0 as 2D (matches the old Ex-loader behavior).
    // Non-POT BC levels are normalized (resampled); see xrDDS_NormalizeDims.
    IDirect3DTexture9* tex = NULL;
    u32 nw0 = 0, nh0 = 0;
    xrDDS_NormalizeDims(info.format, info.width, info.height, &nw0, &nh0);
    if (FAILED(dev->CreateTexture(nw0, nh0, info.mips, 0, info.format, pool, &tex, NULL)))
        return E_FAIL;
    u32 blockBytes = 0, bytesPerPixel = 0;
    xrDDS_FormatInfo(info.format, &blockBytes, &bytesPerPixel);
    const u8* src = (const u8*)fileData + info.dataOffset;
    // Resample scratch (only used when the top level is normalized).
    u8* argbScratch = NULL;
    u8* resampleScratch = NULL;
    if (nw0 != info.width || nh0 != info.height)
    {
        argbScratch = xr_alloc<u8>(info.width * info.height * 4);
        resampleScratch = xr_alloc<u8>(nw0 * nh0 * 4);
    }
    u32 w = info.width, hh = info.height, dd = info.depth;
    u32 nw = nw0, nh = nh0;
    HRESULT hr = S_OK;
    for (u32 m = 0; m < info.mips && SUCCEEDED(hr); ++m)
    {
        const u32 lw = _max(1u, w), lh = _max(1u, hh);
        const u32 dw = _max(1u, nw), dh = _max(1u, nh);
        D3DLOCKED_RECT lr;
        hr = tex->LockRect(m, &lr, NULL, 0);
        if (SUCCEEDED(hr))
        {
            if (dw == lw && dh == lh)
            {
                xrDDS_CopyLevel((u8*)lr.pBits, lr.Pitch, src, lw, lh, blockBytes, bytesPerPixel);
            }
            else if (xrDDS_DecodeLevelToARGB(info.format, src,
                                             xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel),
                                             lw, lh, argbScratch, lw * 4))
            {
                // Resample, then store (encode back for BC targets).
                xrResample_Bilinear(argbScratch, lw * 4, lw, lh, resampleScratch, dw * 4, dw, dh);
                if (blockBytes)
                {
                    // Only DXT1/5 have encoders here (punch-aware DXT1 included).
                    if (info.format != D3DFMT_DXT5 && info.format != D3DFMT_DXT1)
                        hr = E_FAIL;
                    else
                        xrBC_EncodeLevel(info.format, resampleScratch, dw * 4, dw, dh,
                                         (u8*)lr.pBits, lr.Pitch);
                }
                else
                {
                    for (u32 y = 0; y < dh; ++y)
                        CopyMemory((u8*)lr.pBits + y * lr.Pitch, resampleScratch + y * dw * 4, dw * 4);
                }
            }
            else
            {
                hr = E_FAIL;
            }
            tex->UnlockRect(m);
        }
        // Advance past all depth slices of this mip (slice 0 was just loaded).
        src += _max(1u, dd) * xrDDS_LevelSize(lw, lh, blockBytes, bytesPerPixel);
        w /= 2;
        hh /= 2;
        dd /= 2;
        nw /= 2;
        nh /= 2;
    }
    if (argbScratch)
        xr_free(argbScratch);
    if (resampleScratch)
        xr_free(resampleScratch);
    if (FAILED(hr))
    {
        tex->Release();
        return E_FAIL;
    }
    *out = tex;
    return S_OK;
}

inline HRESULT xrDDS_LoadCube(IDirect3DDevice9* dev, const void* fileData, u32 fileSize,
                              D3DPOOL pool, IDirect3DCubeTexture9** out)
{
    XR_DDSInfo info;
    if (FAILED(xrDDS_Parse(fileData, fileSize, &info)) || info.faces != 6 || info.depth != 1)
        return E_FAIL;
    IDirect3DCubeTexture9* tex = NULL;
    if (FAILED(dev->CreateCubeTexture(info.width, info.mips, 0, info.format, pool, &tex, NULL)))
        return E_FAIL;
    u32 blockBytes = 0, bytesPerPixel = 0;
    xrDDS_FormatInfo(info.format, &blockBytes, &bytesPerPixel);
    const u8* src = (const u8*)fileData + info.dataOffset;
    for (u32 f = 0; f < 6; ++f)
    {
        u32 w = info.width, hh = info.height;
        for (u32 m = 0; m < info.mips; ++m)
        {
            u32 lw = _max(1u, w), lh = _max(1u, hh);
            D3DLOCKED_RECT lr;
            if (FAILED(tex->LockRect((D3DCUBEMAP_FACES)f, m, &lr, NULL, 0)))
            {
                tex->Release();
                return E_FAIL;
            }
            xrDDS_CopyLevel((u8*)lr.pBits, lr.Pitch, src, lw, lh, blockBytes, bytesPerPixel);
            tex->UnlockRect((D3DCUBEMAP_FACES)f, m);
            src += xrDDS_LevelSize(lw, lh, blockBytes, bytesPerPixel);
            w /= 2;
            hh /= 2;
        }
    }
    *out = tex;
    return S_OK;
}

//--- BC decode (exact, GPU-matching; for forced-A8R8G8B8 loads) ---------------

inline void xrBC_Expand565(unsigned short v, u8* outBGRA)
{
    const u32 r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    outBGRA[2] = (u8)((r << 3) | (r >> 2));
    outBGRA[1] = (u8)((g << 2) | (g >> 4));
    outBGRA[0] = (u8)((b << 3) | (b >> 2));
    outBGRA[3] = 255;
}

// Decodes one 8-byte color block to 16 BGRA pixels (row-major).
inline void xrBC_DecodeColorBlock(const u8* src, u8* dst /*64 bytes*/, bool dxt1)
{
    unsigned short c0 = (unsigned short)(src[0] | (src[1] << 8));
    unsigned short c1 = (unsigned short)(src[2] | (src[3] << 8));
    u8 pal[4][4];
    xrBC_Expand565(c0, pal[0]);
    xrBC_Expand565(c1, pal[1]);
    if (!dxt1 || c0 > c1)
    {
        for (int c = 0; c < 4; ++c)
        {
            pal[2][c] = (u8)((2 * pal[0][c] + pal[1][c]) / 3);
            pal[3][c] = (u8)((pal[0][c] + 2 * pal[1][c]) / 3);
        }
    }
    else
    {
        for (int c = 0; c < 4; ++c)
        {
            pal[2][c] = (u8)((pal[0][c] + pal[1][c]) / 2);
            pal[3][c] = 0;
        }
        pal[3][3] = 0; // transparent alpha handled per-pixel below
    }
    const bool punch = dxt1 && (c0 <= c1);
    u32 idx = (u32)src[4] | ((u32)src[5] << 8) | ((u32)src[6] << 16) | ((u32)src[7] << 24);
    for (int i = 0; i < 16; ++i)
    {
        const u32 sel = (idx >> (2 * i)) & 3;
        CopyMemory(dst + 4 * i, pal[sel], 4);
        if (punch && sel == 3)
            dst[4 * i + 3] = 0;
        else
            dst[4 * i + 3] = 255;
    }
}

inline void xrBC_DecodeAlphaBC2(const u8* src, u8* dst /*16 alpha bytes*/)
{
    for (int i = 0; i < 16; ++i)
    {
        const u32 shift = (i & 1) ? 4 : 0;
        dst[i] = (u8)(((src[i / 2] >> shift) & 15) * 17);
    }
}

inline void xrBC_DecodeAlphaBC3(const u8* src, u8* dst /*16 alpha bytes*/)
{
    const u8 a0 = src[0], a1 = src[1];
    u8 pal[8];
    pal[0] = a0;
    pal[1] = a1;
    if (a0 > a1)
    {
        for (int i = 0; i < 6; ++i)
            pal[2 + i] = (u8)(((6 - i) * a0 + (1 + i) * a1) / 7);
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            pal[2 + i] = (u8)(((4 - i) * a0 + (1 + i) * a1) / 5);
        pal[6] = 0;
        pal[7] = 255;
    }
    u64 idx = 0;
    for (int i = 0; i < 6; ++i)
        idx |= (u64)src[2 + i] << (8 * i);
    for (int i = 0; i < 16; ++i)
        dst[i] = pal[(idx >> (3 * i)) & 7];
}

// Decodes one level of BC1/2/3 bits to A8R8G8B8 (BGRA bytes).
inline void xrBC_DecodeLevel(D3DFORMAT fmt, const u8* srcBits, u32 srcRowBytes,
                             u8* dstBits, u32 dstPitch, u32 w, u32 h)
{
    const bool dxt1 = (fmt == D3DFMT_DXT1);
    u8 rgba[64];
    u8 alpha[16];
    for (u32 by = 0; by < (h + 3) / 4; ++by)
    {
        for (u32 bx = 0; bx < (w + 3) / 4; ++bx)
        {
            const u8* blk = srcBits + by * srcRowBytes + bx * (dxt1 ? 8 : 16);
            const u8* colorBlk = blk;
            if (!dxt1)
            {
                if (fmt == D3DFMT_DXT2 || fmt == D3DFMT_DXT3)
                    xrBC_DecodeAlphaBC2(blk, alpha);
                else
                    xrBC_DecodeAlphaBC3(blk, alpha);
                colorBlk = blk + 8;
            }
            xrBC_DecodeColorBlock(colorBlk, rgba, dxt1);
            for (u32 y = 0; y < 4; ++y)
            {
                const u32 dy = by * 4 + y;
                if (dy >= h)
                    break;
                u8* row = dstBits + dy * dstPitch + bx * 16;
                for (u32 x = 0; x < 4; ++x)
                {
                    if (bx * 4 + x >= w)
                        break;
                    CopyMemory(row + 4 * x, rgba + 4 * (y * 4 + x), 4);
                    if (!dxt1)
                        row[4 * x + 3] = alpha[y * 4 + x];
                }
            }
        }
    }
}

// Loads any supported DDS as A8R8G8B8 SYSTEMMEM with a FULL mip chain
// (D3DX_DEFAULT semantics: file mips first, box-filtered down to 1x1;
// top dims normalized like Load2D).
inline HRESULT xrDDS_LoadAsARGB(IDirect3DDevice9* dev, const void* fileData, u32 fileSize,
                                IDirect3DTexture9** out)
{
    XR_DDSInfo info;
    if (FAILED(xrDDS_Parse(fileData, fileSize, &info)) || info.faces != 1 || info.depth != 1)
        return E_FAIL;
    if (info.format != D3DFMT_A8R8G8B8 && info.format != D3DFMT_X8R8G8B8
        && info.format != D3DFMT_L8 && info.format != D3DFMT_A8 && info.format != D3DFMT_DXT1
        && info.format != D3DFMT_DXT3 && info.format != D3DFMT_DXT5)
        return E_FAIL;
    u32 nw0 = 0, nh0 = 0;
    xrDDS_NormalizeDims(info.format, info.width, info.height, &nw0, &nh0);
    u32 fullMips = 1;
    for (u32 w = nw0, h = nh0; w > 1 || h > 1; w /= 2, h /= 2)
        ++fullMips;
    IDirect3DTexture9* tex = NULL;
    if (FAILED(dev->CreateTexture(nw0, nh0, fullMips, 0,
                                  D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &tex, NULL)))
        return E_FAIL;
    u32 blockBytes = 0, bytesPerPixel = 0;
    xrDDS_FormatInfo(info.format, &blockBytes, &bytesPerPixel);
    const u8* src = (const u8*)fileData + info.dataOffset;
    u8* argbScratch = NULL;
    u8* resampleScratch = NULL;
    if (nw0 != info.width || nh0 != info.height)
    {
        argbScratch = xr_alloc<u8>(info.width * info.height * 4);
        resampleScratch = xr_alloc<u8>(nw0 * nh0 * 4);
    }
    u32 w = info.width, hh = info.height;
    u32 nw = nw0, nh = nh0;
    u32 m = 0;
    HRESULT hr = S_OK;
    for (; m < info.mips && SUCCEEDED(hr); ++m)
    {
        const u32 lw = _max(1u, w), lh = _max(1u, hh);
        const u32 dw = _max(1u, nw), dh = _max(1u, nh);
        D3DLOCKED_RECT lr;
        hr = tex->LockRect(m, &lr, NULL, 0);
        if (SUCCEEDED(hr))
        {
            if (dw == lw && dh == lh)
            {
                if (!xrDDS_DecodeLevelToARGB(info.format, src,
                                             xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel),
                                             lw, lh, (u8*)lr.pBits, lr.Pitch))
                    hr = E_FAIL;
            }
            else if (xrDDS_DecodeLevelToARGB(info.format, src,
                                             xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel),
                                             lw, lh, argbScratch, lw * 4))
            {
                xrResample_Bilinear(argbScratch, lw * 4, lw, lh,
                                    (u8*)lr.pBits, lr.Pitch, dw, dh);
            }
            else
            {
                hr = E_FAIL;
            }
            tex->UnlockRect(m);
        }
        src += xrDDS_LevelSize(lw, lh, blockBytes, bytesPerPixel);
        w /= 2;
        hh /= 2;
        nw /= 2;
        nh /= 2;
    }
    // Box-filter the remaining levels (matches D3DX_DEFAULT chain completion).
    for (; m < fullMips && SUCCEEDED(hr); ++m)
    {
        D3DLOCKED_RECT lrS, lrD;
        D3DSURFACE_DESC dd, sd;
        tex->GetLevelDesc(m, &dd);
        tex->GetLevelDesc(m - 1, &sd);
        if (FAILED(tex->LockRect(m - 1, &lrS, NULL, D3DLOCK_READONLY))
            || FAILED(tex->LockRect(m, &lrD, NULL, 0)))
        {
            tex->UnlockRect(m - 1);
            tex->Release();
            return E_FAIL;
        }
        for (u32 y = 0; y < dd.Height; ++y)
        {
            u8* dst = (u8*)lrD.pBits + y * lrD.Pitch;
            const u32 sy0 = _min(2 * y, sd.Height - 1), sy1 = _min(2 * y + 1, sd.Height - 1);
            for (u32 x = 0; x < dd.Width; ++x)
            {
                const u32 sx0 = _min(2 * x, sd.Width - 1), sx1 = _min(2 * x + 1, sd.Width - 1);
                for (int c = 0; c < 4; ++c)
                {
                    const u8* s00 = (const u8*)lrS.pBits + sy0 * lrS.Pitch + sx0 * 4 + c;
                    const u8* s10 = (const u8*)lrS.pBits + sy0 * lrS.Pitch + sx1 * 4 + c;
                    const u8* s01 = (const u8*)lrS.pBits + sy1 * lrS.Pitch + sx0 * 4 + c;
                    const u8* s11 = (const u8*)lrS.pBits + sy1 * lrS.Pitch + sx1 * 4 + c;
                    dst[4 * x + c] = (u8)((*s00 + *s10 + *s01 + *s11 + 2) / 4);
                }
            }
        }
        tex->UnlockRect(m - 1);
        tex->UnlockRect(m);
    }
    if (argbScratch)
        xr_free(argbScratch);
    if (resampleScratch)
        xr_free(resampleScratch);
    if (FAILED(hr))
    {
        tex->Release();
        return E_FAIL;
    }
    *out = tex;
    return S_OK;
}

//--- Normal-map generation (replaces D3DXComputeNormalMap) -------------------
// Sobel gradients with wrap; the amplitude/8 convention and orientation are
// proven exact on synthetic ramps. On real content this is a close visual
// match (rms ~10, mostly steep-edge pixels), not bit-exact: the residual is
// below the DXT5 compression noise the pipeline adds right after, and the
// gloss passes overwrite alpha next (so occlusion-in-alpha is skipped).
inline void xrNormalMap_Generate(u32* dst, u32 dstPitchDW, const u32* src, u32 srcPitchDW,
                                 u32 w, u32 h, float amplitude, bool luminance)
{
    const float amp = amplitude / 8.f;
    const u8* s = (const u8*)src;
    u8* d = (u8*)dst;
    for (u32 y = 0; y < h; ++y)
    {
        const u32 ym = (y + h - 1) % h, yp = (y + 1) % h;
        for (u32 x = 0; x < w; ++x)
        {
            const u32 xm = (x + w - 1) % w, xp = (x + 1) % w;
            float hv[3][3];
            for (int j = 0; j < 3; ++j)
            {
                const u32 yy = (j == 0) ? ym : ((j == 1) ? y : yp);
                const u8* row = s + yy * srcPitchDW * 4;
                const u32 xx[3] = { xm, x, xp };
                for (int i = 0; i < 3; ++i)
                {
                    const u8* px = row + xx[i] * 4;
                    hv[j][i] = luminance
                        ? (px[0] + px[1] + px[2]) / (3.f * 255.f)
                        : px[2] / 255.f; // BGRA memory: byte 2 is red
                }
            }
            const float dx = (hv[0][2] + 2.f * hv[1][2] + hv[2][2])
                           - (hv[0][0] + 2.f * hv[1][0] + hv[2][0]);
            const float dy = (hv[2][0] + 2.f * hv[2][1] + hv[2][2])
                           - (hv[0][0] + 2.f * hv[0][1] + hv[0][2]);
            float nx = -dx * amp, ny = -dy * amp, nz = 1.f;
            const float inv = 1.f / _sqrt(nx * nx + ny * ny + nz * nz);
            u8* out = d + y * dstPitchDW * 4 + x * 4;
            out[2] = (u8)clampr(iFloor((nx * inv * 0.5f + 0.5f) * 255.f), 0, 255);
            out[1] = (u8)clampr(iFloor((ny * inv * 0.5f + 0.5f) * 255.f), 0, 255);
            out[0] = (u8)clampr(iFloor((nz * inv * 0.5f + 0.5f) * 255.f), 0, 255);
            out[3] = 255;
        }
    }
}

#ifdef __cplusplus
extern "C" {
#endif
void stb_compress_dxt_block(unsigned char* dest, const unsigned char* src, int alpha, int mode);
#ifdef __cplusplus
}
#endif
#define XR_DXT_HIGHQUAL 2

// Box-downsample one ARGB level to half dims (min 1 per axis).
inline void xrARGB_BoxLevel(const u8* srcBits, u32 srcPitch, u32 sw, u32 sh,
                            u8* dstBits, u32 dstPitch, u32 dw, u32 dh)
{
    for (u32 y = 0; y < dh; ++y)
    {
        u8* dst = dstBits + y * dstPitch;
        const u32 sy0 = _min(2 * y, sh - 1), sy1 = _min(2 * y + 1, sh - 1);
        for (u32 x = 0; x < dw; ++x)
        {
            const u32 sx0 = _min(2 * x, sw - 1), sx1 = _min(2 * x + 1, sw - 1);
            for (int c = 0; c < 4; ++c)
            {
                const u8* s00 = srcBits + sy0 * srcPitch + sx0 * 4 + c;
                const u8* s10 = srcBits + sy0 * srcPitch + sx1 * 4 + c;
                const u8* s01 = srcBits + sy1 * srcPitch + sx0 * 4 + c;
                const u8* s11 = srcBits + sy1 * srcPitch + sx1 * 4 + c;
                dst[4 * x + c] = (u8)((*s00 + *s10 + *s01 + *s11 + 2) / 4);
            }
        }
    }
}

// Collapse an ARGB (BGRA bytes) image to one byte per pixel for L8/A8
// uploads (R/G/B are equal for L8 decodes, alpha holds A8).
inline void xrARGB_Collapse(const u8* srcARGB, u32 srcPitch, u32 w, u32 h,
                            u8* dst, u32 dstPitch, u32 byteOffset)
{
    for (u32 y = 0; y < h; ++y)
    {
        const u8* srow = srcARGB + y * srcPitch;
        u8* drow = dst + y * dstPitch;
        for (u32 x = 0; x < w; ++x)
            drow[x] = srow[4 * x + byteOffset];
    }
}

// Encode one ARGB image (BGRA bytes) to BC blocks. DXT1 is punch-aware
// (transparent texels get index 3); DXT3 carries explicit nibble alpha;
// DXT5 carries interpolated alpha. (DXT2/4 map to 3/5; nothing else encodes.)
inline void xrBC_EncodeLevel(D3DFORMAT fmt, const u8* srcARGB, u32 srcPitch,
                             u32 w, u32 h, u8* dstBlocks, u32 dstPitch)
{
    const bool dxt1 = (fmt == D3DFMT_DXT1);
    const bool dxt3 = (fmt == D3DFMT_DXT2 || fmt == D3DFMT_DXT3);
    u8 rgba[16][4];
    for (u32 by = 0; by < (h + 3) / 4; ++by)
    {
        for (u32 bx = 0; bx < (w + 3) / 4; ++bx)
        {
            for (u32 y = 0; y < 4; ++y)
            {
                const u32 sy = _min(by * 4 + y, h - 1);
                const u8* srow = srcARGB + sy * srcPitch;
                for (u32 x = 0; x < 4; ++x)
                {
                    const u32 sx = _min(bx * 4 + x, w - 1);
                    const u8* sp = srow + sx * 4;
                    u8* dp = rgba[y * 4 + x];
                    dp[0] = sp[2];
                    dp[1] = sp[1];
                    dp[2] = sp[0];
                    dp[3] = sp[3];
                }
            }
            u8* dbl = dstBlocks + by * dstPitch + bx * (dxt1 ? 8 : 16);
            if (!dxt1 && !dxt3)
            {
                stb_compress_dxt_block(dbl, &rgba[0][0], 1, XR_DXT_HIGHQUAL);
                continue;
            }
            if (dxt3)
            {
                // Explicit 4-bit alpha + opaque-style color block.
                for (int i = 0; i < 16; i += 2)
                    dbl[i / 2] = (u8)(((rgba[i][3] >> 4) & 0xF) | (rgba[i + 1][3] & 0xF0));
                stb_compress_dxt_block(dbl + 8, &rgba[0][0], 0, XR_DXT_HIGHQUAL);
                continue;
            }
            bool punch = false;
            for (int i = 0; i < 16; ++i)
                if (rgba[i][3] < 128)
                {
                    punch = true;
                    break;
                }
            if (!punch)
            {
                stb_compress_dxt_block(dbl, &rgba[0][0], 0, XR_DXT_HIGHQUAL);
                continue;
            }
            // Endpoints = most distant opaque pair (565), ordered for 1-bit alpha.
            unsigned short e0 = 0, e1 = 0;
            {
                int best = -1;
                u8 p0[3] = { 0, 0, 0 }, p1[3] = { 0, 0, 0 };
                for (int i = 0; i < 16; ++i)
                {
                    if (rgba[i][3] < 128)
                        continue;
                    for (int j = 0; j < 16; ++j)
                    {
                        if (rgba[j][3] < 128)
                            continue;
                        const int dr = (int)rgba[i][0] - (int)rgba[j][0];
                        const int dg = (int)rgba[i][1] - (int)rgba[j][1];
                        const int db = (int)rgba[i][2] - (int)rgba[j][2];
                        const int d = dr * dr + dg * dg + db * db;
                        if (d > best)
                        {
                            best = d;
                            p0[0] = rgba[i][0];
                            p0[1] = rgba[i][1];
                            p0[2] = rgba[i][2];
                            p1[0] = rgba[j][0];
                            p1[1] = rgba[j][1];
                            p1[2] = rgba[j][2];
                        }
                    }
                }
                e0 = (unsigned short)(((p0[0] >> 3) << 11) | ((p0[1] >> 2) << 5) | (p0[2] >> 3));
                e1 = (unsigned short)(((p1[0] >> 3) << 11) | ((p1[1] >> 2) << 5) | (p1[2] >> 3));
                if (e0 > e1)
                {
                    const unsigned short t = e0;
                    e0 = e1;
                    e1 = t;
                }
            }
            u8 c0[4], c1[4], c2[4];
            xrBC_Expand565(e0, c0);
            xrBC_Expand565(e1, c1);
            for (int c = 0; c < 4; ++c)
                c2[c] = (u8)((c0[c] + c1[c]) / 2);
            u32 idx = 0;
            for (int i = 0; i < 16; ++i)
            {
                u32 sel;
                if (rgba[i][3] < 128)
                {
                    sel = 3;
                }
                else
                {
                    int d0 = 0, d1 = 0, d2 = 0;
                    for (int c = 0; c < 3; ++c)
                    {
                        const int v = rgba[i][c];
                        d0 += (v - c0[c]) * (v - c0[c]);
                        d1 += (v - c1[c]) * (v - c1[c]);
                        d2 += (v - c2[c]) * (v - c2[c]);
                    }
                    sel = (d0 <= d1 && d0 <= d2) ? 0 : ((d1 <= d2) ? 1 : 2);
                }
                idx |= sel << (2 * i);
            }
            dbl[0] = (u8)e0;
            dbl[1] = (u8)(e0 >> 8);
            dbl[2] = (u8)e1;
            dbl[3] = (u8)(e1 >> 8);
            dbl[4] = (u8)idx;
            dbl[5] = (u8)(idx >> 8);
            dbl[6] = (u8)(idx >> 16);
            dbl[7] = (u8)(idx >> 24);
        }
    }
}

//--- Surface copy (same format; replaces D3DXLoadSurfaceFromSurface) ---------
inline HRESULT xrSurface_Copy(IDirect3DSurface9* dst, IDirect3DSurface9* src)
{
    D3DSURFACE_DESC dd, sd;
    if (FAILED(dst->GetDesc(&dd)) || FAILED(src->GetDesc(&sd)))
        return E_FAIL;
    if (dd.Format != sd.Format || dd.Width != sd.Width || dd.Height != sd.Height)
        return E_FAIL; // conversions go through the BC codec, not here
    u32 blockBytes = 0, bytesPerPixel = 0;
    if (!xrDDS_FormatInfo(dd.Format, &blockBytes, &bytesPerPixel))
        return E_FAIL;
    D3DLOCKED_RECT lrD, lrS;
    if (FAILED(dst->LockRect(&lrD, NULL, 0)))
        return E_FAIL;
    if (FAILED(src->LockRect(&lrS, NULL, D3DLOCK_READONLY)))
    {
        dst->UnlockRect();
        return E_FAIL;
    }
    xrDDS_CopyLevel((u8*)lrD.pBits, lrD.Pitch, (const u8*)lrS.pBits,
                    dd.Width, dd.Height, blockBytes, bytesPerPixel);
    src->UnlockRect();
    dst->UnlockRect();
    return S_OK;
}

// A8R8G8B8 -> DXT1/DXT5 (replaces the D3DX compressor inside LoadSurfaceFromSurface).
inline HRESULT xrSurface_CompressBC(IDirect3DSurface9* dst, IDirect3DSurface9* src)
{
    D3DSURFACE_DESC dd, sd;
    if (FAILED(dst->GetDesc(&dd)) || FAILED(src->GetDesc(&sd)))
        return E_FAIL;
    if ((dd.Format != D3DFMT_DXT1 && dd.Format != D3DFMT_DXT5)
        || sd.Format != D3DFMT_A8R8G8B8
        || dd.Width != sd.Width || dd.Height != sd.Height)
        return E_FAIL;
    D3DLOCKED_RECT lrD, lrS;
    if (FAILED(dst->LockRect(&lrD, NULL, 0)))
        return E_FAIL;
    if (FAILED(src->LockRect(&lrS, NULL, D3DLOCK_READONLY)))
    {
        dst->UnlockRect();
        return E_FAIL;
    }
    xrBC_EncodeLevel(dd.Format, (const u8*)lrS.pBits, lrS.Pitch, dd.Width, dd.Height,
                     (u8*)lrD.pBits, lrD.Pitch);
    src->UnlockRect();
    dst->UnlockRect();
    return S_OK;
}

// DXT1/2/3/4/5 -> A8R8G8B8 (replaces the D3DX decompressor inside LoadSurfaceFromSurface).
inline HRESULT xrSurface_DecompressBC(IDirect3DSurface9* dst, IDirect3DSurface9* src)
{
    D3DSURFACE_DESC dd, sd;
    if (FAILED(dst->GetDesc(&dd)) || FAILED(src->GetDesc(&sd)))
        return E_FAIL;
    if (dd.Format != D3DFMT_A8R8G8B8
        || (sd.Format != D3DFMT_DXT1 && sd.Format != D3DFMT_DXT2 && sd.Format != D3DFMT_DXT3
            && sd.Format != D3DFMT_DXT4 && sd.Format != D3DFMT_DXT5)
        || dd.Width != sd.Width || dd.Height != sd.Height)
        return E_FAIL;
    D3DLOCKED_RECT lrD, lrS;
    if (FAILED(dst->LockRect(&lrD, NULL, 0)))
        return E_FAIL;
    if (FAILED(src->LockRect(&lrS, NULL, D3DLOCK_READONLY)))
    {
        dst->UnlockRect();
        return E_FAIL;
    }
    xrBC_DecodeLevel(sd.Format, (const u8*)lrS.pBits, lrS.Pitch,
                     (u8*)lrD.pBits, lrD.Pitch, dd.Width, dd.Height);
    src->UnlockRect();
    dst->UnlockRect();
    return S_OK;
}

// Any supported conversion via an A8R8G8B8 intermediate (BC<->BC included;
// replaces D3DXLoadSurfaceFromSurface for the mismatched-format case).
inline HRESULT xrSurface_ConvertBC(IDirect3DSurface9* dst, IDirect3DSurface9* src)
{
    D3DSURFACE_DESC dd, sd;
    if (FAILED(dst->GetDesc(&dd)) || FAILED(src->GetDesc(&sd)))
        return E_FAIL;
    if (dd.Width != sd.Width || dd.Height != sd.Height)
        return E_FAIL;
    if (dd.Format == sd.Format)
        return xrSurface_Copy(dst, src);
    // ARGB source?
    u32 sBlock = 0, sBpp = 0;
    if (!xrDDS_FormatInfo(sd.Format, &sBlock, &sBpp))
        return E_FAIL;
    u32 dBlock = 0, dBpp = 0;
    if (!xrDDS_FormatInfo(dd.Format, &dBlock, &dBpp))
        return E_FAIL;
    const bool srcARGB = (sBlock == 0), dstARGB = (dBlock == 0);
    if (srcARGB && !dstARGB)
    {
        // Only A8R8G8B8 sources encode (matches the engine's working format).
        if (sd.Format != D3DFMT_A8R8G8B8)
            return E_FAIL;
        return xrSurface_CompressBC(dst, src);
    }
    if (!srcARGB && dstARGB)
    {
        // Only A8R8G8B8 destinations decode.
        if (dd.Format != D3DFMT_A8R8G8B8)
            return E_FAIL;
        return xrSurface_DecompressBC(dst, src);
    }
    if (srcARGB || dstARGB)
        return E_FAIL;
    // BC -> BC through an ARGB scratch surface (device-local, same size).
    IDirect3DDevice9* dev = NULL;
    if (FAILED(src->GetDevice(&dev)) || !dev)
        return E_FAIL;
    IDirect3DSurface9* mid = NULL;
    HRESULT hr = dev->CreateOffscreenPlainSurface(sd.Width, sd.Height,
                                                  D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &mid, NULL);
    dev->Release();
    if (FAILED(hr))
        return hr;
    hr = xrSurface_DecompressBC(mid, src);
    if (SUCCEEDED(hr))
        hr = xrSurface_CompressBC(dst, mid);
    mid->Release();
    return hr;
}

//--- DDS writer (debug TW_Save; screenshots use it too) ----------------------
inline bool xrDDS_WriteHeader(IWriter* W, D3DFORMAT fmt, u32 w, u32 h, u32 mips)
{
    u32 blockBytes = 0, bytesPerPixel = 0;
    if (!xrDDS_FormatInfo(fmt, &blockBytes, &bytesPerPixel))
        return false;
    u32 magic = XR_DDS_MAGIC;
    W->w(&magic, 4);
    XR_DDS_Header hdr;
    ZeroMemory(&hdr, sizeof(hdr));
    hdr.dwSize = 124;
    hdr.dwFlags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000; // CAPS|HEIGHT|WIDTH|PIXELFORMAT|MIPMAPCOUNT
    hdr.dwHeight = h;
    hdr.dwWidth = w;
    hdr.dwMipMapCount = mips;
    hdr.ddspf.dwSize = 32;
    const bool compressed = (blockBytes != 0);
    if (compressed)
    {
        hdr.ddspf.dwFlags = 0x4; // FOURCC
        hdr.ddspf.dwFourCC = (fmt == D3DFMT_DXT1) ? 0x31545844
            : (fmt == D3DFMT_DXT3) ? 0x33545844 : 0x35545844;
    }
    else if (fmt == D3DFMT_L8)
    {
        hdr.ddspf.dwFlags = 0x20000; // DDPF_LUMINANCE
        hdr.ddspf.dwRGBBitCount = 8;
        hdr.ddspf.dwRBitMask = 0xFF;
    }
    else if (fmt == D3DFMT_R8G8B8)
    {
        hdr.ddspf.dwFlags = 0x40; // RGB, no alpha
        hdr.ddspf.dwRGBBitCount = 24;
        hdr.ddspf.dwRBitMask = 0x00FF0000;
        hdr.ddspf.dwGBitMask = 0x0000FF00;
        hdr.ddspf.dwBBitMask = 0x000000FF;
    }
    else
    {
        hdr.ddspf.dwFlags = 0x40 | 0x1; // RGB|ALPHAPIXELS
        hdr.ddspf.dwRGBBitCount = 32;
        hdr.ddspf.dwRBitMask = 0x00FF0000;
        hdr.ddspf.dwGBitMask = 0x0000FF00;
        hdr.ddspf.dwBBitMask = 0x000000FF;
        hdr.ddspf.dwABitMask = 0xFF000000;
    }
    hdr.dwCaps = 0x1000 | 0x400000; // TEXTURE|MIPMAP
    W->w(&hdr, sizeof(hdr));
    return true;
}

// One file-layout level (rows as stored on disk).
struct XR_DDSLevel
{
    const u8* bits;
    u32 pitch;
};

// Writes header + levels (as stored; caller guarantees supported format).
inline bool xrDDS_SaveLevels(IWriter* W, D3DFORMAT fmt, u32 w, u32 h,
                             u32 mips, const XR_DDSLevel* levels)
{
    if (!W || !levels)
        return false;
    u32 blockBytes = 0, bytesPerPixel = 0;
    if (!xrDDS_FormatInfo(fmt, &blockBytes, &bytesPerPixel))
        return false;
    if (!xrDDS_WriteHeader(W, fmt, w, h, mips))
        return false;
    u32 lw = w, lh = h;
    for (u32 m = 0; m < mips; ++m)
    {
        lw = _max(1u, lw);
        lh = _max(1u, lh);
        const u32 rows = blockBytes ? ((lh + 3) / 4) : lh;
        const u32 rowBytes = xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel);
        for (u32 y = 0; y < rows; ++y)
            W->w(levels[m].bits + y * levels[m].pitch, rowBytes);
        lw /= 2;
        lh /= 2;
    }
    return true;
}

inline bool xrDDS_Save2D(const char* path, IDirect3DTexture9* tex)
{
    if (!path || !tex)
        return false;
    D3DSURFACE_DESC desc;
    if (FAILED(tex->GetLevelDesc(0, &desc)))
        return false;
    u32 blockBytes = 0, bytesPerPixel = 0;
    if (!xrDDS_FormatInfo(desc.Format, &blockBytes, &bytesPerPixel))
        return false;
    IWriter* W = FS.w_open(path);
    if (!W)
        return false;
    const u32 mips = tex->GetLevelCount();
    // header
    if (!xrDDS_WriteHeader(W, desc.Format, desc.Width, desc.Height, mips))
    {
        FS.w_close(W);
        return false;
    }
    // levels (as stored; caller guarantees supported format)
    u32 w = desc.Width, hh = desc.Height;
    for (u32 m = 0; m < mips; ++m)
    {
        const u32 lw = _max(1u, w), lh = _max(1u, hh);
        D3DLOCKED_RECT lr;
        if (FAILED(tex->LockRect(m, &lr, NULL, D3DLOCK_READONLY)))
        {
            FS.w_close(W);
            return false;
        }
        const u32 rows = blockBytes ? ((lh + 3) / 4) : lh;
        const u32 rowBytes = xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel);
        for (u32 y = 0; y < rows; ++y)
            W->w((const u8*)lr.pBits + y * lr.Pitch, rowBytes);
        tex->UnlockRect(m);
        w /= 2;
        hh /= 2;
    }
    FS.w_close(W);
    return true;
}
