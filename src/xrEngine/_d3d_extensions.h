#ifndef _D3D_EXT_internal
#define _D3D_EXT_internal

#ifndef NO_XR_LIGHT
struct Flight
{
public:
    u32 type; /* Type of light source */
    Fcolor diffuse; /* Diffuse color of light */
    Fcolor specular; /* Specular color of light */
    Fcolor ambient; /* Ambient color of light */
    Fvector position; /* Position in world space */
    Fvector direction; /* Direction in world space */
    float range; /* Cutoff range */
    float falloff; /* Falloff */
    float attenuation0; /* Constant attenuation */
    float attenuation1; /* Linear attenuation */
    float attenuation2; /* Quadratic attenuation */
    float theta; /* Inner angle of spotlight cone */
    float phi; /* Outer angle of spotlight cone */

    IC void set(u32 ltType, float x, float y, float z)
    {
        ZeroMemory(this, sizeof(Flight));
        type = ltType;
        diffuse.set(1.0f, 1.0f, 1.0f, 1.0f);
        specular.set(diffuse);
        position.set(x, y, z);
        direction.set(x, y, z);
        direction.normalize_safe();
        range = _sqrt(flt_max);
    }
    IC void mul(float brightness)
    {
        diffuse.mul_rgb(brightness);
        ambient.mul_rgb(brightness);
        specular.mul_rgb(brightness);
    }
};

/*
#if sizeof(Flight)!=sizeof(D3DLIGHT9)
#error Different structure size
#endif
*/

#endif

#ifndef NO_XR_MATERIAL
struct Fmaterial
{
public:
    Fcolor diffuse; /* Diffuse color RGBA */
    Fcolor ambient; /* Ambient color RGB */
    Fcolor specular; /* Specular 'shininess' */
    Fcolor emissive; /* Emissive color RGB */
    float power; /* Sharpness if specular highlight */

    IC void set(float r, float g, float b)
    {
        ZeroMemory(this, sizeof(Fmaterial));
        diffuse.r = ambient.r = r;
        diffuse.g = ambient.g = g;
        diffuse.b = ambient.b = b;
        diffuse.a = ambient.a = 1.0f;
        power = 0;
    }
    IC void set(float r, float g, float b, float a)
    {
        ZeroMemory(this, sizeof(Fmaterial));
        diffuse.r = ambient.r = r;
        diffuse.g = ambient.g = g;
        diffuse.b = ambient.b = b;
        diffuse.a = ambient.a = a;
        power = 0;
    }
    IC void set(Fcolor& c)
    {
        ZeroMemory(this, sizeof(Fmaterial));
        diffuse.r = ambient.r = c.r;
        diffuse.g = ambient.g = c.g;
        diffuse.b = ambient.b = c.b;
        diffuse.a = ambient.a = c.a;
        power = 0;
    }
};

/*
#if sizeof(Fmaterial)!=sizeof(D3DMATERIAL9)
#error Different structure size
#endif
*/

#endif

#ifndef NO_XR_VDECLARATOR
// D3DX-free vertex declaration helpers (same semantics as the D3DX9 originals,
// so no d3dx9 link dependency for FVF/decl queries).
inline u32 xrDeclTypeSize(u8 type)
{
    switch (type)
    {
    case D3DDECLTYPE_FLOAT1: return 4;
    case D3DDECLTYPE_FLOAT2: return 8;
    case D3DDECLTYPE_FLOAT3: return 12;
    case D3DDECLTYPE_FLOAT4: return 16;
    case D3DDECLTYPE_D3DCOLOR: return 4;
    case D3DDECLTYPE_UBYTE4: return 4;
    case D3DDECLTYPE_SHORT2: return 4;
    case D3DDECLTYPE_SHORT4: return 8;
    default: return 0;
    }
}
// Number of elements before D3DDECL_END (== D3DXGetDeclLength).
inline u32 xrGetDeclLength(const D3DVERTEXELEMENT9* dcl)
{
    u32 n = 0;
    while (dcl[n].Stream != 0xFF)
        ++n;
    return n;
}
// Packed stride of one stream (== D3DXGetDeclVertexSize).
inline u32 xrGetDeclVertexSize(const D3DVERTEXELEMENT9* dcl, u32 stream)
{
    u32 size = 0;
    for (u32 i = 0; dcl[i].Stream != 0xFF; ++i)
        if (dcl[i].Stream == stream)
            size += xrDeclTypeSize(dcl[i].Type);
    return size;
}
// Byte size of an FVF vertex (== D3DXGetFVFVertexSize).
inline u32 xrGetFVFVertexSize(u32 fvf)
{
    u32 size = 0;
    switch (fvf & D3DFVF_POSITION_MASK)
    {
    case D3DFVF_XYZ: size += 12; break;
    case D3DFVF_XYZRHW: size += 16; break;
    case D3DFVF_XYZB1: size += 16; break;
    case D3DFVF_XYZB2: size += 20; break;
    case D3DFVF_XYZB3: size += 24; break;
    case D3DFVF_XYZB4: size += 28; break;
    case D3DFVF_XYZB5: size += 32; break;
    default: break;
    }
    if (fvf & D3DFVF_NORMAL) size += 12;
    if (fvf & D3DFVF_PSIZE) size += 4;
    if (fvf & D3DFVF_DIFFUSE) size += 4;
    if (fvf & D3DFVF_SPECULAR) size += 4;
    // 2-bit format per stage: 0->SIZE2, 1->SIZE3, 2->SIZE4, 3->SIZE1
    static const u32 texSize[4] = { 8, 12, 16, 4 };
    const u32 texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (u32 t = 0; t < texCount; ++t)
        size += texSize[(fvf >> (16 + 2 * t)) & 3];
    return size;
}
// FVF -> declaration (== D3DXDeclaratorFromFVF). Only unblended positions are
// used by the engine; anything else fails loudly instead of silently.
// Array size for D3DVERTEXELEMENT9 declaration buffers
// (== MAX_FVF_DECL_SIZE == MAXD3DDECLLENGTH + 1 for END).
enum { XR_MAX_FVF_DECL_SIZE = 64 + 1 };
inline HRESULT xrDeclaratorFromFVF(u32 fvf, D3DVERTEXELEMENT9* dcl)
{
    u32 offset = 0;
    u32 idx = 0;
    auto push = [&](u8 type, u8 usage, u8 usageIndex)
    {
        dcl[idx].Stream = 0;
        dcl[idx].Offset = (u16)offset;
        dcl[idx].Type = type;
        dcl[idx].Method = D3DDECLMETHOD_DEFAULT;
        dcl[idx].Usage = usage;
        dcl[idx].UsageIndex = usageIndex;
        offset += xrDeclTypeSize(type);
        ++idx;
    };
    switch (fvf & D3DFVF_POSITION_MASK)
    {
    case D3DFVF_XYZ:
        push(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0);
        break;
    case D3DFVF_XYZRHW:
        push(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0);
        break;
    default:
        return D3DERR_INVALIDCALL;
    }
    if (fvf & D3DFVF_NORMAL)
        push(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0);
    if (fvf & D3DFVF_PSIZE)
        push(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0);
    if (fvf & D3DFVF_DIFFUSE)
        push(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0);
    if (fvf & D3DFVF_SPECULAR)
        push(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1);
    static const u8 texTypes[4] = { D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4, D3DDECLTYPE_FLOAT1 };
    const u32 texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (u32 t = 0; t < texCount; ++t)
        push(texTypes[(fvf >> (16 + 2 * t)) & 3], D3DDECLUSAGE_TEXCOORD, (u8)t);
    dcl[idx].Stream = 0xFF;
    dcl[idx].Offset = 0;
    dcl[idx].Type = D3DDECLTYPE_UNUSED;
    dcl[idx].Method = 0;
    dcl[idx].Usage = 0;
    dcl[idx].UsageIndex = 0;
    return S_OK;
}
struct VDeclarator : public svector < D3DVERTEXELEMENT9, MAXD3DDECLLENGTH + 1 >
{
    void set(u32 FVF)
    {
        xrDeclaratorFromFVF(FVF, begin());
        resize(xrGetDeclLength(begin()) + 1);
    }
    void set(D3DVERTEXELEMENT9* dcl)
    {
        resize(xrGetDeclLength(dcl) + 1);
        CopyMemory(begin(), dcl, size()*sizeof(D3DVERTEXELEMENT9));
    }
    void set(const VDeclarator& d)
    {
        *this = d;
    }
    u32 vertex() { return xrGetDeclVertexSize(begin(), 0); }
    BOOL equal(VDeclarator& d)
    {
        if (size() != d.size()) return false;
        else return 0 == memcmp(begin(), d.begin(), size()*sizeof(D3DVERTEXELEMENT9));
    }
};
#endif

#endif
