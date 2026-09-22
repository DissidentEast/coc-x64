// SunTSM.h - D3DX-free math vocabulary for the sun-shadow TSM code
// (r2_R_sun.cpp in R1/R2/R3/R4). Layout-compatible with the D3DX types it
// replaces, so the ported algorithms are unchanged. No d3dx9 dependency.
#pragma once

struct SunVec2
{
    float x, y;
    SunVec2() {}
    SunVec2(float _x, float _y) : x(_x), y(_y) {}
    SunVec2 operator+(const SunVec2& o) const { return SunVec2(x + o.x, y + o.y); }
    SunVec2 operator-(const SunVec2& o) const { return SunVec2(x - o.x, y - o.y); }
    SunVec2 operator*(float s) const { return SunVec2(x * s, y * s); }
};

inline SunVec2 operator*(float s, const SunVec2& v) { return v * s; }

struct SunVec3
{
    float x, y, z;
    SunVec3() {}
    SunVec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    SunVec3 operator+(const SunVec3& o) const { return SunVec3(x + o.x, y + o.y, z + o.z); }
    SunVec3 operator-(const SunVec3& o) const { return SunVec3(x - o.x, y - o.y, z - o.z); }
    SunVec3 operator-() const { return SunVec3(-x, -y, -z); }
    SunVec3 operator*(float s) const { return SunVec3(x * s, y * s, z * s); }
    SunVec3 operator/(float s) const { return SunVec3(x / s, y / s, z / s); }
    SunVec3& operator+=(const SunVec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    bool operator==(const SunVec3& o) const { return x == o.x && y == o.y && z == o.z; }
};

inline SunVec3 operator*(float s, const SunVec3& v) { return v * s; }

struct SunVec4
{
    float x, y, z, w;
    SunVec4() {}
    SunVec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
    SunVec4 operator+(const SunVec4& o) const { return SunVec4(x + o.x, y + o.y, z + o.z, w + o.w); }
    SunVec4 operator-(const SunVec4& o) const { return SunVec4(x - o.x, y - o.y, z - o.z, w - o.w); }
    SunVec4 operator*(float s) const { return SunVec4(x * s, y * s, z * s, w * s); }
};

struct SunPlane
{
    float a, b, c, d;
    SunPlane() {}
    SunPlane(float _a, float _b, float _c, float _d) : a(_a), b(_b), c(_c), d(_d) {}
};

// Fmatrix with the extra constructors the TSM code needs (16-float literal
// matrices and copies from engine matrices). Same layout, no vtable.
struct SunMatrix : public Fmatrix
{
    SunMatrix() {}
    SunMatrix(const Fmatrix& o) { *(Fmatrix*)this = o; }
    SunMatrix(float _11_, float _12_, float _13_, float _14_,
              float _21_, float _22_, float _23_, float _24_,
              float _31_, float _32_, float _33_, float _34_,
              float _41_, float _42_, float _43_, float _44_)
    {
        _11 = _11_; _12 = _12_; _13 = _13_; _14 = _14_;
        _21 = _21_; _22 = _22_; _23 = _23_; _24 = _24_;
        _31 = _31_; _32 = _32_; _33 = _33_; _34 = _34_;
        _41 = _41_; _42 = _42_; _43 = _43_; _44 = _44_;
    }
};

inline float SunVec2Dot(const SunVec2* a, const SunVec2* b)
{
    return a->x * b->x + a->y * b->y;
}

inline float SunVec2Length(const SunVec2* v)
{
    return _sqrt(v->x * v->x + v->y * v->y);
}

inline float SunVec3Dot(const SunVec3* a, const SunVec3* b)
{
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

inline void SunVec3Cross(SunVec3* out, const SunVec3* a, const SunVec3* b)
{
    out->x = a->y * b->z - a->z * b->y;
    out->y = a->z * b->x - a->x * b->z;
    out->z = a->x * b->y - a->y * b->x;
}

inline float SunVec3Length(const SunVec3* v)
{
    return _sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
}

inline SunVec3* SunVec3Normalize(SunVec3* out, const SunVec3* v)
{
    const float len = SunVec3Length(v);
    if (len > 0.f)
    {
        const float inv = 1.f / len;
        out->x = v->x * inv;
        out->y = v->y * inv;
        out->z = v->z * inv;
    }
    else
    {
        out->x = out->y = out->z = 0.f;
    }
    return out;
}

// Row-vector transform without perspective divide (== D3DXVec3TransformNormal).
inline void SunVec3TransformNormal(SunVec3* out, const SunVec3* v, const Fmatrix* m)
{
    out->x = v->x * m->_11 + v->y * m->_21 + v->z * m->_31;
    out->y = v->x * m->_12 + v->y * m->_22 + v->z * m->_32;
    out->z = v->x * m->_13 + v->y * m->_23 + v->z * m->_33;
}

// Row-vector transform with perspective divide (== D3DXVec3TransformCoordArray).
inline void SunVec3TransformCoordArray(SunVec3* out, u32 outStride, const SunVec3* v, u32 vStride,
                                       const Fmatrix* m, u32 n)
{
    for (u32 i = 0; i < n; ++i)
    {
        const SunVec3* cur = (const SunVec3*)((const u8*)v + vStride * i);
        SunVec3* dst = (SunVec3*)((u8*)out + outStride * i);
        const float w = cur->x * m->_14 + cur->y * m->_24 + cur->z * m->_34 + m->_44;
        const float inv = 1.f / w;
        dst->x = (cur->x * m->_11 + cur->y * m->_21 + cur->z * m->_31 + m->_41) * inv;
        dst->y = (cur->x * m->_12 + cur->y * m->_22 + cur->z * m->_32 + m->_42) * inv;
        dst->z = (cur->x * m->_13 + cur->y * m->_23 + cur->z * m->_33 + m->_43) * inv;
    }
}

inline float SunPlaneDotCoord(const SunPlane* p, const SunVec3* v)
{
    return p->a * v->x + p->b * v->y + p->c * v->z + p->d;
}

inline float SunPlaneDotNormal(const SunPlane* p, const SunVec3* v)
{
    return p->a * v->x + p->b * v->y + p->c * v->z;
}

// Full 4x4 cofactor inverse (== D3DXMatrixInverse; Fmatrix::invert is 4x3-only).
inline void SunMatrixInverse(Fmatrix* out, const Fmatrix* m)
{
    const float* a = &m->_11;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15]
           + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15]
           - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15]
           + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14]
            - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15]
           - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15]
           + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15]
           - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14]
            + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15]
           + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15]
           - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15]
            + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14]
            - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11]
           - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11]
           + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11]
            - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10]
            + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    const float invDet = 1.f / det;
    float* o = &out->_11;
    for (int i = 0; i < 16; ++i)
        o[i] = inv[i] * invDet;
}

// Left-handed off-center ortho (== D3DXMatrixOrthoOffCenterLH).
inline void SunMatrixOrthoOffCenterLH(Fmatrix* out, float l, float r, float b, float t, float zn, float zf)
{
    out->identity();
    out->_11 = 2.f / (r - l);
    out->_22 = 2.f / (t - b);
    out->_33 = 1.f / (zf - zn);
    out->_41 = (l + r) / (l - r);
    out->_42 = (t + b) / (b - t);
    out->_43 = zn / (zn - zf);
}
