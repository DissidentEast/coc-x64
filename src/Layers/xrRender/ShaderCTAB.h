// ShaderCTAB.h - D3DX-free shader constant-table support.
//
// Covers the three D3DX shader services the DX9 renderers still need:
//   1. CTAB parsing: the D3DXSHADER_* structs are plain binary layouts, so
//      they are mirrored here (ShaderCTAB_*) instead of pulling d3dx9.h.
//   2. CTAB lookup: xrFindShaderCTAB replaces D3DXFindShaderComment for the
//      CTAB case by walking D3DSIO_COMMENT tokens in the bytecode.
//   3. Compile target: xrShaderProfile replaces D3DXGetVertex/PixelShader-
//      Profile via the device caps (3_0 / 2_0 / 1_1 chain).
// Actual compilation/disassembly goes through D3DCompile/D3DDisassemble
// (d3dcompiler.h, Windows SDK). Flag values are identical by design:
// D3DCOMPILE_DEBUG / PACK_MATRIX_ROW_MAJOR == D3DXSHADER_DEBUG /
// PACKMATRIX_ROWMAJOR, so call sites just rename the symbols.
#pragma once

//--- CTAB binary mirrors (layouts identical to D3DXSHADER_*) ----------------

enum XR_CTAB_RegisterSet
{
    XR_RS_BOOL = 0,
    XR_RS_INT4 = 1,
    XR_RS_FLOAT4 = 2,
    XR_RS_SAMPLER = 3
};

enum XR_CTAB_ParameterClass
{
    XR_PC_SCALAR = 0,
    XR_PC_VECTOR = 1,
    XR_PC_MATRIX_ROWS = 2,
    XR_PC_MATRIX_COLUMNS = 3,
    XR_PC_OBJECT = 4,
    XR_PC_STRUCT = 5
};

enum XR_CTAB_ParameterType
{
    XR_PT_VOID = 0,
    XR_PT_BOOL = 1,
    XR_PT_INT = 2,
    XR_PT_FLOAT = 3,
    XR_PT_STRING = 4,
    XR_PT_TEXTURE = 5,
    XR_PT_TEXTURE1D = 6,
    XR_PT_TEXTURE2D = 7,
    XR_PT_TEXTURE3D = 8,
    XR_PT_TEXTURECUBE = 9,
    XR_PT_SAMPLER = 10,
    XR_PT_SAMPLER1D = 11,
    XR_PT_SAMPLER2D = 12,
    XR_PT_SAMPLER3D = 13,
    XR_PT_SAMPLERCUBE = 14
};

struct ShaderCTAB_ConstantInfo
{
    u32 Name;           // LPCSTR byte offset
    unsigned short RegisterSet;    // XR_CTAB_RegisterSet
    unsigned short RegisterIndex;
    unsigned short RegisterCount;
    unsigned short Reserved;
    u32 TypeInfo;       // ShaderCTAB_TypeInfo byte offset
    u32 DefaultValue;   // default-value byte offset
};

struct ShaderCTAB_TypeInfo
{
    unsigned short Class;          // XR_CTAB_ParameterClass
    unsigned short Type;           // XR_CTAB_ParameterType
    unsigned short Rows;
    unsigned short Columns;
    unsigned short Elements;
    unsigned short StructMembers;
    u32 StructMemberInfo;
};

struct ShaderCTAB
{
    u32 Size;
    u32 Creator;        // LPCSTR byte offset
    u32 Version;
    u32 Constants;
    u32 ConstantInfo;   // ShaderCTAB_ConstantInfo[Constants] byte offset
    u32 Flags;
    u32 Target;         // LPCSTR byte offset
};

//--- CTAB lookup -------------------------------------------------------------

// Finds the CTAB table in SM2/SM3 bytecode (replaces D3DXFindShaderComment
// for the 'CTAB' case). Returns S_OK with *outCTAB pointing at the table.
inline HRESULT xrFindShaderCTAB(const void* bytecode, u32 sizeBytes, const void** outCTAB)
{
    // Comment token is (dwordCount << 16) | 0xFFFE, followed by the FOURCC.
    const u32 COMMENT_OP = 0xFFFE;
    const u32 CTAB = 0x42415443;    // MAKEFOURCC('C','T','A','B')
    const u8* base = (const u8*)bytecode;
    const u8* end = base + sizeBytes;
    const u32* p = (const u32*)base;
    while ((const u8*)(p + 2) <= end)
    {
        if ((*p & 0xFFFF) == COMMENT_OP)
        {
            const u32 count = *p >> 16;
            if (p[1] == CTAB)
            {
                if (outCTAB)
                    *outCTAB = &p[2];
                return S_OK;
            }
            if (count == 0 || (const u8*)(p + 1 + count) > end)
                break; // corrupt blob, stop instead of overrunning
            p += 1 + count;
        }
        else
        {
            ++p;
        }
    }
    return E_FAIL;
}

//--- Compile target ----------------------------------------------------------

#ifdef _D3D9_H_ // DX9 TUs only (needs IDirect3DDevice9/D3DCAPS9)
// Highest fully supported profile, same chain D3DXGet*ShaderProfile uses
// (3_0 / 2_0 / 1_1; x_y_a cards run x_y_0 code, which is a subset).
inline LPCSTR xrShaderProfile(IDirect3DDevice9* device, bool vertex)
{
    D3DCAPS9 caps;
    if (device && SUCCEEDED(device->GetDeviceCaps(&caps)))
    {
        const DWORD v = vertex ? caps.VertexShaderVersion : caps.PixelShaderVersion;
        if (vertex)
        {
            if (v >= D3DVS_VERSION(3, 0))
                return "vs_3_0";
            if (v >= D3DVS_VERSION(2, 0))
                return "vs_2_0";
            return "vs_1_1";
        }
        if (v >= D3DPS_VERSION(3, 0))
            return "ps_3_0";
        if (v >= D3DPS_VERSION(2, 0))
            return "ps_2_0";
        return "ps_1_1";
    }
    return vertex ? "vs_2_0" : "ps_2_0";
}
#endif
