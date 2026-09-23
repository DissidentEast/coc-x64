#include "stdafx.h"
//#include "../../xrEngine/xr_effgamma.h"
#include "xr_effgamma.h"
#include "dxRenderDeviceRender.h"
#include "../xrRender/tga.h"
#include "../../xrEngine/xrImage_Resampler.h"
#include "TextureDDS.h" // D3DX-free DDS writer + BC codec + resampler

#define STB_IMAGE_WRITE_IMPLEMENTATION
// Engine-tracked memory + no CRT stdio (we only use the *_to_func writers).
#define STBI_WRITE_NO_STDIO
#define STBIW_MALLOC(sz) Memory.mem_alloc(sz)
#define STBIW_REALLOC(p, newsz) Memory.mem_realloc((void*)(p), (size_t)(newsz))
#define STBIW_REALLOC_SIZED(p, oldsz, newsz) Memory.mem_realloc((void*)(p), (size_t)(newsz))
#define STBIW_FREE(p) (Memory.mem_free((void*)(p)))
#define STBIW_MEMMOVE(a, b, sz) CopyMemory((a), (b), (sz))
#include "../../3rd party/stb/stb_image_write.h"

#define	GAMESAVE_SIZE	128

IC u32 convert				(float c)	{
	u32 C=iFloor(c);
	if (C>255) C=255;
	return C;
}
IC void MouseRayFromPoint	( Fvector& direction, int x, int y, Fmatrix& m_CamMat )
{
	int halfwidth		= Device.dwWidth/2;
	int halfheight		= Device.dwHeight/2;

	Ivector2 point2;
	point2.set			(x-halfwidth, halfheight-y);

	float size_y		= VIEWPORT_NEAR * tanf( deg2rad(60.f) * 0.5f );
	float size_x		= size_y / (Device.fHeight_2/Device.fWidth_2);

	float r_pt			= float(point2.x) * size_x / (float) halfwidth;
	float u_pt			= float(point2.y) * size_y / (float) halfheight;

	direction.mul		( m_CamMat.k, VIEWPORT_NEAR );
	direction.mad		( direction, m_CamMat.j, u_pt );
	direction.mad		( direction, m_CamMat.i, r_pt );
	direction.normalize	();
}

#define SM_FOR_SEND_WIDTH 640
#define SM_FOR_SEND_HEIGHT 480

//--- D3DX-free image saving (7d3) -------------------------------------------
// Append-callback target for stb_image_write *_to_func.
struct xrShotBuf
{
	u8* data;
	u32 size;
	u32 cap;
};
static void xrShot_Append(void* ctx, void* data, int size)
{
	xrShotBuf* b = (xrShotBuf*)ctx;
	if (b->size + (u32)size > b->cap)
	{
		const u32 ncap = _max(b->cap * 2, b->size + (u32)size + 4096);
		u8* ndata = (u8*)xr_realloc(b->data, ncap);
		if (!ndata)
			return;
		b->data = ndata;
		b->cap = ncap;
	}
	CopyMemory(b->data + b->size, data, size);
	b->size += (u32)size;
}
// BGRA -> RGB copy (stb writers want RGB top-down).
static void xrShot_BGRAtoRGB(const u8* src, u32 srcPitch, u32 w, u32 h, u8* dst)
{
	for (u32 y = 0; y < h; ++y)
	{
		const u8* srow = src + y * srcPitch;
		u8* drow = dst + y * w * 3;
		for (u32 x = 0; x < w; ++x)
		{
			drow[3 * x + 0] = srow[4 * x + 2];
			drow[3 * x + 1] = srow[4 * x + 1];
			drow[3 * x + 2] = srow[4 * x + 0];
		}
	}
}
// thumb ARGB image to BC1 DDS (replaces D3DXSaveTextureToMemory DDS).
static bool xrShot_SaveBC1DDS(IWriter* W, const u8* argb, u32 w, u32 h)
{
	if (!W)
		return false;
	const u32 rb = xrDDS_LevelRowBytes(w, h, 8, 0);
	u8* enc = (u8*)xr_malloc(rb * ((h + 3) / 4));
	xrBC_EncodeLevel(D3DFMT_DXT1, argb, w * 4, w, h, enc, rb);
	XR_DDSLevel lv;
	lv.bits = enc;
	lv.pitch = rb;
	const bool ok = xrDDS_SaveLevels(W, D3DFMT_DXT1, w, h, 1, &lv);
	xr_free(enc);
	return ok;
}

#if defined(USE_DX10) || defined(USE_DX11)
// Read back a DEFAULT-usage 2D texture to top-down BGRA (w*4 rows).
static HRESULT xrShot_ReadbackBGRA(ID3DResource* src, u8** outBits, u32* outW, u32* outH)
{
	*outBits = NULL;
	ID3DTexture2D* srcTex = NULL;
	HRESULT hr = src->QueryInterface(__uuidof(ID3DTexture2D), (void**)&srcTex);
	if (FAILED(hr))
		return hr;
	D3D_TEXTURE2D_DESC sd;
	srcTex->GetDesc(&sd);
	ID3DTexture2D* staging = NULL;
	D3D_TEXTURE2D_DESC dd;
	ZeroMemory(&dd, sizeof(dd));
	dd.Width = sd.Width;
	dd.Height = sd.Height;
	dd.MipLevels = 1;
	dd.ArraySize = 1;
	dd.Format = sd.Format;
	dd.SampleDesc.Count = 1;
	dd.Usage = D3D_USAGE_STAGING;
	dd.CPUAccessFlags = D3D_CPU_ACCESS_READ;
	hr = HW.pDevice->CreateTexture2D(&dd, NULL, &staging);
	if (SUCCEEDED(hr))
	{
#ifdef USE_DX11
		HW.pContext->CopyResource(staging, srcTex);
#else
		HW.pDevice->CopyResource(staging, srcTex);
#endif
		D3D_MAPPED_TEXTURE2D mr;
#ifdef USE_DX11
		hr = HW.pContext->Map(staging, 0, D3D_MAP_READ, 0, &mr);
#else
		hr = staging->Map(0, D3D_MAP_READ, 0, &mr);
#endif
		if (SUCCEEDED(hr))
		{
			u8* bits = (u8*)xr_malloc(sd.Width * sd.Height * 4);
			for (u32 y = 0; y < sd.Height; ++y)
				CopyMemory(bits + y * sd.Width * 4, (const u8*)mr.pData + y * mr.RowPitch, sd.Width * 4);
#ifdef USE_DX11
			HW.pContext->Unmap(staging, 0);
#else
			staging->Unmap(0);
#endif
			*outBits = bits;
			*outW = sd.Width;
			*outH = sd.Height;
		}
		staging->Release();
	}
	srcTex->Release();
	return hr;
}
// Downscale BGRA to a thumb ARGB image (replaces D3DXLoadTextureFromTexture).
static void xrShot_Thumbnail(const u8* bgra, u32 sw, u32 sh, u8* thumb, u32 dw, u32 dh)
{
	xrResample_Bilinear(bgra, sw * 4, sw, sh, thumb, dw * 4, dw, dh);
}
#endif	//	USE_DX10

#if defined(USE_DX10) || defined(USE_DX11)
void CRender::ScreenshotImpl	(ScreenshotMode mode, LPCSTR name, CMemoryWriter* memory_writer)
{
	ID3DResource		*pSrcTexture;
	HW.pBaseRT->GetResource(&pSrcTexture);

	VERIFY(pSrcTexture);

	// Save
	switch (mode)	
	{
		case IRender_interface::SM_FOR_GAMESAVE:
		{
			// 128x128 BC1 DDS thumb (was D3DXLoadTextureFromTexture +
			// D3DXSaveTextureToMemory). Written by game saves (autosave).
			u8* bgra = NULL;
			u32 sw = 0, sh = 0;
			if (SUCCEEDED(xrShot_ReadbackBGRA(pSrcTexture, &bgra, &sw, &sh)))
			{
				u8* thumb = (u8*)xr_malloc(GAMESAVE_SIZE * GAMESAVE_SIZE * 4);
				xrShot_Thumbnail(bgra, sw, sh, thumb, GAMESAVE_SIZE, GAMESAVE_SIZE);
				IWriter* fs = FS.w_open(name);
				if (fs)
				{
					xrShot_SaveBC1DDS(fs, thumb, GAMESAVE_SIZE, GAMESAVE_SIZE);
					FS.w_close(fs);
				}
				xr_free(thumb);
				xr_free(bgra);
			}
		}
		break;
		case IRender_interface::SM_FOR_MPSENDING:
		{
			// 640x480 BC1 DDS (was D3DXLoadTextureFromTexture +
			// D3DXSaveTextureToMemory). MP-only path.
			u8* bgra = NULL;
			u32 sw = 0, sh = 0;
			if (SUCCEEDED(xrShot_ReadbackBGRA(pSrcTexture, &bgra, &sw, &sh)))
			{
				u8* thumb = (u8*)xr_malloc(SM_FOR_SEND_WIDTH * SM_FOR_SEND_HEIGHT * 4);
				xrShot_Thumbnail(bgra, sw, sh, thumb, SM_FOR_SEND_WIDTH, SM_FOR_SEND_HEIGHT);
				if (!memory_writer)
				{
					IWriter* fs = FS.w_open(name);
					if (fs)
					{
						xrShot_SaveBC1DDS(fs, thumb, SM_FOR_SEND_WIDTH, SM_FOR_SEND_HEIGHT);
						FS.w_close(fs);
					}
				}
				else
				{
					xrShot_SaveBC1DDS(memory_writer, thumb, SM_FOR_SEND_WIDTH, SM_FOR_SEND_HEIGHT);
				}
				xr_free(thumb);
				xr_free(bgra);
			}
		}
		break;
		case IRender_interface::SM_NORMAL:
		{
			// Full-res JPG (was D3DXSaveTextureToMemory JPG).
			string64			t_stemp;
			string_path			buf;
			xr_sprintf			(buf,sizeof(buf),"ss_%s_%s_(%s).jpg",Core.UserName,timestamp(t_stemp),(g_pGameLevel)?g_pGameLevel->name().c_str():"mainmenu");
			u8* bgra = NULL;
			u32 sw = 0, sh = 0;
			if (SUCCEEDED(xrShot_ReadbackBGRA(pSrcTexture, &bgra, &sw, &sh)))
			{
				u8* rgb = (u8*)xr_malloc(sw * sh * 3);
				xrShot_BGRAtoRGB(bgra, sw * 4, sw, sh, rgb);
				xrShotBuf shot = {0};
				if (stbi_write_jpg_to_func(xrShot_Append, &shot, sw, sh, 3, rgb, 85))
				{
					IWriter* fs = FS.w_open("$screenshots$",buf); R_ASSERT(fs);
					fs->w(shot.data, shot.size);
					FS.w_close(fs);
				}
				xr_free(shot.data);
				xr_free(rgb);
				xr_free(bgra);
			}

			if (strstr(Core.Params,"-ss_tga"))
			{ // hq
				xr_sprintf			(buf,sizeof(buf),"ssq_%s_%s_(%s).tga",Core.UserName,timestamp(t_stemp),(g_pGameLevel)?g_pGameLevel->name().c_str():"mainmenu");
				u8* bgraHQ = NULL;
				u32 hw2 = 0, hh2 = 0;
				if (SUCCEEDED(xrShot_ReadbackBGRA(pSrcTexture, &bgraHQ, &hw2, &hh2)))
				{
					u8* rgbHQ = (u8*)xr_malloc(hw2 * hh2 * 3);
					xrShot_BGRAtoRGB(bgraHQ, hw2 * 4, hw2, hh2, rgbHQ);
					xrShotBuf shotHQ = {0};
					if (stbi_write_bmp_to_func(xrShot_Append, &shotHQ, hw2, hh2, 3, rgbHQ))
					{
						IWriter* fs = FS.w_open("$screenshots$",buf); R_ASSERT(fs);
						fs->w(shotHQ.data, shotHQ.size);
						FS.w_close(fs);
					}
					xr_free(shotHQ.data);
					xr_free(rgbHQ);
					xr_free(bgraHQ);
				}
			}
		}
		break;
		case IRender_interface::SM_FOR_LEVELMAP:
		case IRender_interface::SM_FOR_CUBEMAP:
			{
				VERIFY(!"CRender::Screenshot. This screenshot type is not supported for DX10.");
				/*
				string64			t_stemp;
				string_path			buf;
				VERIFY				(name);
				strconcat			(sizeof(buf),buf,"ss_",Core.UserName,"_",timestamp(t_stemp),"_#",name);
				xr_strcat				(buf,".tga");
				IWriter*		fs	= FS.w_open	("$screenshots$",buf); R_ASSERT(fs);
				TGAdesc				p;
				p.format			= IMG_24B;

				//	TODO: DX10: This is totally incorrect but mimics 
				//	original behaviour. Fix later.
				hr					= pFB->LockRect(&D,0,D3DLOCK_NOSYSLOCK);
				if(hr!=D3D_OK)		return;
				hr					= pFB->UnlockRect();
				if(hr!=D3D_OK)		goto _end_;

				// save
				u32* data			= (u32*)xr_malloc(Device.dwHeight*Device.dwHeight*4);
				imf_Process			(data,Device.dwHeight,Device.dwHeight,(u32*)D.pBits,Device.dwWidth,Device.dwHeight,imf_lanczos3);
				p.scanlenght		= Device.dwHeight*4;
				p.width				= Device.dwHeight;
				p.height			= Device.dwHeight;
				p.data				= data;
				p.maketga			(*fs);
				xr_free				(data);

				FS.w_close			(fs);
				*/
			}
			break;
	}

	_RELEASE(pSrcTexture);
}

#else	//	USE_DX10

// BGRA -> BGR copy (24-bit DDS thumbs; DX9 screenshot path only).
static void xrShot_BGRAtoBGR(const u8* src, u32 srcPitch, u32 w, u32 h, u8* dst)
{
	for (u32 y = 0; y < h; ++y)
	{
		const u8* srow = src + y * srcPitch;
		u8* drow = dst + y * w * 3;
		for (u32 x = 0; x < w; ++x)
		{
			drow[3 * x + 0] = srow[4 * x + 0];
			drow[3 * x + 1] = srow[4 * x + 1];
			drow[3 * x + 2] = srow[4 * x + 2];
		}
	}
}
// IEEE-754 half -> float (replaces D3DXFloat16To32Array; DX9 R2 path only:
// R1 never samples float rendertargets here).
#if RENDER != R_R1
static float xrHalfToFloat(u16 h)
{
	const u32 sign = (u32)(h & 0x8000) << 16;
	const u32 exp = ((u32)h >> 10) & 0x1F;
	const u32 mant = (u32)h & 0x3FF;
	u32 f;
	if (exp == 0)
	{
		if (mant == 0)
			f = sign; // signed zero
		else
		{
			u32 m = mant, e = 0;
			while ((m & 0x400) == 0)
			{
				m <<= 1;
				++e;
			}
			m &= 0x3FF;
			f = sign | ((127 - 14 - e) << 23) | (m << 13);
		}
	}
	else if (exp == 31)
		f = sign | 0x7F800000 | (mant << 13); // inf/nan
	else
		f = sign | ((exp + 112) << 23) | (mant << 13);
	float r;
	CopyMemory(&r, &f, 4);
	return r;
}
static void xrHalfToFloatArray(float* dst, const u16* src, int count)
{
	for (int i = 0; i < count; ++i)
		dst[i] = xrHalfToFloat(src[i]);
}
#endif	//	RENDER != R_R1

void CRender::ScreenshotImpl	(ScreenshotMode mode, LPCSTR name, CMemoryWriter* memory_writer)
{
	if (!Device.b_is_Ready)			return;
	if ((psDeviceFlags.test(rsFullscreen)) == 0) {
		if(name && FS.exist(name))
			FS.file_delete(0,name);

		Log("~ Can't capture screen while in windowed mode...");
		return;
	}

	// Create temp-surface
	IDirect3DSurface9*	pFB;
	D3DLOCKED_RECT		D;
	HRESULT				hr;
	hr					= HW.pDevice->CreateOffscreenPlainSurface(Device.dwWidth,Device.dwHeight,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&pFB,NULL);
	if(hr!=D3D_OK)		return;

	hr					= HW.pDevice->GetFrontBufferData(0,pFB);
	if(hr!=D3D_OK)		return;

	
	hr					= pFB->LockRect(&D,0,D3DLOCK_NOSYSLOCK);
	if(hr!=D3D_OK)		return;

	// Image processing (gamma-correct)
	u32* pPixel		= (u32*)D.pBits;
	u32* pEnd		= pPixel+(Device.dwWidth*Device.dwHeight);
	//	IGOR: Remove inverse color correction and kill alpha
	/*
	D3DGAMMARAMP	G;
	dxRenderDeviceRender::Instance().gammaGenLUT(G);
	for (int i=0; i<256; i++) {
		G.red	[i]	/= 256;
		G.green	[i]	/= 256;
		G.blue	[i]	/= 256;
	}
	for (;pPixel!=pEnd; pPixel++)	{
		u32 p = *pPixel;
		*pPixel = color_xrgb	(
			G.red	[color_get_R(p)],
			G.green	[color_get_G(p)],
			G.blue	[color_get_B(p)]
			);
	}
	*/

	//	Kill alpha
	for (;pPixel!=pEnd; pPixel++)	
	{
		u32 p = *pPixel;
		*pPixel = color_xrgb	(
			color_get_R(p),
			color_get_G(p),
			color_get_B(p)
		);
	}

	hr					= pFB->UnlockRect();
	if(hr!=D3D_OK)		goto _end_;
	

	// Save
	switch (mode)	{
		case IRender_interface::SM_FOR_GAMESAVE:
			{
				// 128x128 BC1 DDS thumb (was D3DXCreateTexture DXT1 +
				// D3DXLoadSurfaceFromSurface + D3DXSaveTextureToFileInMemory).
				u8* thumb = (u8*)xr_malloc(GAMESAVE_SIZE * GAMESAVE_SIZE * 4);
				xrResample_Bilinear((const u8*)D.pBits, D.Pitch, Device.dwWidth, Device.dwHeight,
					thumb, GAMESAVE_SIZE * 4, GAMESAVE_SIZE, GAMESAVE_SIZE);
				IWriter* fs = FS.w_open(name);
				if (fs)
				{
					xrShot_SaveBC1DDS(fs, thumb, GAMESAVE_SIZE, GAMESAVE_SIZE);
					FS.w_close(fs);
				}
				xr_free(thumb);
			}
			break;
		case IRender_interface::SM_FOR_MPSENDING:
			{
				// 640x480 24-bit DDS (was D3DXCreateTexture R8G8B8 +
				// D3DXLoadSurfaceFromSurface + D3DXSaveTextureToFileInMemory).
				u8* thumb = (u8*)xr_malloc(SM_FOR_SEND_WIDTH * SM_FOR_SEND_HEIGHT * 4);
				xrResample_Bilinear((const u8*)D.pBits, D.Pitch, Device.dwWidth, Device.dwHeight,
					thumb, SM_FOR_SEND_WIDTH * 4, SM_FOR_SEND_WIDTH, SM_FOR_SEND_HEIGHT);
				u8* bgr = (u8*)xr_malloc(SM_FOR_SEND_WIDTH * SM_FOR_SEND_HEIGHT * 3);
				xrShot_BGRAtoBGR(thumb, SM_FOR_SEND_WIDTH * 4,
					SM_FOR_SEND_WIDTH, SM_FOR_SEND_HEIGHT, bgr);
				XR_DDSLevel lv;
				lv.bits = bgr;
				lv.pitch = SM_FOR_SEND_WIDTH * 3;
				if (!memory_writer)
				{
					IWriter* fs = FS.w_open(name);
					if (fs)
					{
						xrDDS_SaveLevels(fs, D3DFMT_R8G8B8,
							SM_FOR_SEND_WIDTH, SM_FOR_SEND_HEIGHT, 1, &lv);
						FS.w_close(fs);
					}
				}
				else
				{
					xrDDS_SaveLevels(memory_writer, D3DFMT_R8G8B8,
						SM_FOR_SEND_WIDTH, SM_FOR_SEND_HEIGHT, 1, &lv);
				}
				xr_free(bgr);
				xr_free(thumb);
			}break;
		case IRender_interface::SM_NORMAL:
			{
				// Full-res JPG (was D3DXSaveSurfaceToFileInMemory JPG).
				string64			t_stemp;
				string_path			buf;
				xr_sprintf			(buf,sizeof(buf),"ss_%s_%s_(%s).jpg",Core.UserName,timestamp(t_stemp),(g_pGameLevel)?g_pGameLevel->name().c_str():"mainmenu");
				u8* rgb = (u8*)xr_malloc(Device.dwWidth * Device.dwHeight * 3);
				xrShot_BGRAtoRGB((const u8*)D.pBits, D.Pitch, Device.dwWidth, Device.dwHeight, rgb);
				xrShotBuf shot = {0};
				if (stbi_write_jpg_to_func(xrShot_Append, &shot,
					Device.dwWidth, Device.dwHeight, 3, rgb, 85))
				{
					IWriter* fs = FS.w_open("$screenshots$",buf); R_ASSERT(fs);
					fs->w(shot.data, shot.size);
					FS.w_close(fs);
				}
				xr_free(shot.data);
				xr_free(rgb);
				if (strstr(Core.Params,"-ss_tga"))	{ // hq
					xr_sprintf			(buf,sizeof(buf),"ssq_%s_%s_(%s).tga",Core.UserName,timestamp(t_stemp),(g_pGameLevel)?g_pGameLevel->name().c_str():"mainmenu");
					TGAdesc				p;
					p.format			= IMG_24B;
					p.scanlenght		= Device.dwWidth*4;
					p.width				= Device.dwWidth;
					p.height			= Device.dwHeight;
					p.data				= D.pBits;
					IWriter*		fs	= FS.w_open	("$screenshots$",buf); R_ASSERT(fs);
					p.maketga			(*fs);
					FS.w_close			(fs);
				}
			}
			break;
		case IRender_interface::SM_FOR_LEVELMAP:
		case IRender_interface::SM_FOR_CUBEMAP:
			{
//				string64			t_stemp;
				string_path			buf;
				VERIFY				(name);
				strconcat			(sizeof(buf), buf, name, ".tga");
				IWriter*		fs	= FS.w_open	("$screenshots$",buf); R_ASSERT(fs);
				TGAdesc				p;
				p.format			= IMG_24B;

				//	TODO: DX10: This is totally incorrect but mimics 
				//	original behavior. Fix later.
				hr					= pFB->LockRect(&D,0,D3DLOCK_NOSYSLOCK);
				if(hr!=D3D_OK)		return;
				hr					= pFB->UnlockRect();
				if(hr!=D3D_OK)		goto _end_;

				// save
				u32* data			= (u32*)xr_malloc(Device.dwHeight*Device.dwHeight*4);
				imf_Process			(data,Device.dwHeight,Device.dwHeight,(u32*)D.pBits,Device.dwWidth,Device.dwHeight,imf_lanczos3);
				p.scanlenght		= Device.dwHeight*4;
				p.width				= Device.dwHeight;
				p.height			= Device.dwHeight;
				p.data				= data;
				p.maketga			(*fs);
				xr_free				(data);

				FS.w_close			(fs);
			}
			break;
	}

_end_:
	_RELEASE		(pFB);
}

#endif	//	USE_DX10

void CRender::Screenshot(ScreenshotMode mode, LPCSTR name)
{
	ScreenshotImpl(mode, name, NULL);
}

void CRender::Screenshot(ScreenshotMode mode, CMemoryWriter& memory_writer)
{
	if (mode != SM_FOR_MPSENDING)
	{
		Log("~ Not implemented screenshot mode...");
		return;
	} 
	ScreenshotImpl(mode, NULL, &memory_writer);
}

void CRender::ScreenshotAsyncBegin()
{
	VERIFY(!m_bMakeAsyncSS);
	m_bMakeAsyncSS = true;
}

#if defined(USE_DX10) || defined(USE_DX11)

void CRender::ScreenshotAsyncEnd(CMemoryWriter &memory_writer)
{
	VERIFY(!m_bMakeAsyncSS);

	//	Don't own. No need to release.
	ID3DTexture2D*	pTex = Target->t_ss_async;

	D3D_MAPPED_TEXTURE2D	MappedData;

#ifdef USE_DX11
	HW.pContext->Map(pTex, 0, D3D_MAP_READ, 0, &MappedData);
#else
	pTex->Map(0, D3D_MAP_READ, 0, &MappedData);
#endif

	{

		u32* pPixel		= (u32*)MappedData.pData;
		u32* pEnd		= pPixel+(Device.dwWidth*Device.dwHeight);

		//	Kill alpha and swap r and b.
		for (;pPixel!=pEnd; pPixel++)	
		{
			u32 p = *pPixel;
			*pPixel = color_xrgb	(
				color_get_B(p),
				color_get_G(p),
				color_get_R(p)
				);
		}

		memory_writer.w( &Device.dwWidth, sizeof(Device.dwWidth) );
		memory_writer.w( &Device.dwHeight, sizeof(Device.dwHeight) );
		memory_writer.w( MappedData.pData, (Device.dwWidth*Device.dwHeight)*4 );
	}

#ifdef USE_DX11
	HW.pContext->Unmap(pTex, 0);
#else
	pTex->Unmap(0);
#endif
}

#else	//	USE_DX10

void CRender::ScreenshotAsyncEnd(CMemoryWriter &memory_writer)
{
	if (!Device.b_is_Ready)			return;

	VERIFY(!m_bMakeAsyncSS);

	D3DLOCKED_RECT		D;
	HRESULT				hr;
	IDirect3DSurface9*	pFB;

	pFB = Target->pFB;

	hr					= pFB->LockRect(&D,0,D3DLOCK_NOSYSLOCK);
	if(hr!=D3D_OK)		return;

#if	RENDER == R_R1
	u32 rtWidth = Target->get_rtwidth();
	u32 rtHeight = Target->get_rtheight();
#else	//	RENDER != R_R1
	u32 rtWidth = Device.dwWidth;
	u32 rtHeight = Device.dwHeight;
#endif	//	RENDER != R_R1

	// Image processing (gamma-correct)
	u32* pPixel		= (u32*)D.pBits;
	u32* pOrigin	= pPixel;
	u32* pEnd		= pPixel+(rtWidth*rtHeight);
	
	//	Kill alpha
#if	RENDER != R_R1
	if (Target->rt_Color->fmt == D3DFMT_A16B16G16R16F)
	{
		static const int iMaxPixelsInARow = 1024;
		u16*	pPixelElement16 = (u16*) pPixel;

		FLOAT	tmpArray[4*iMaxPixelsInARow];
		while(pPixel!=pEnd)
		{
			const int iProcessPixels = _min(iMaxPixelsInARow, (s32)(pEnd-pPixel));

			xrHalfToFloatArray( tmpArray, pPixelElement16, iProcessPixels*4);			

			for ( int i=0; i<iProcessPixels; ++i)
			{
				*pPixel = color_argb_f	(
					1.0f,
					tmpArray[i*4],
					tmpArray[i*4+1],
					tmpArray[i*4+2]
					);

				++pPixel;
			}

			pPixelElement16 += iProcessPixels * 4;
		}
	}
	else
#endif	//	RENDER != R_R1
	{
		for (;pPixel!=pEnd; pPixel++)	
		{
			u32 p = *pPixel;
			*pPixel = color_xrgb	(
				color_get_R(p),
				color_get_G(p),
				color_get_B(p)
				);
		}
	}

	{
		memory_writer.w( &rtWidth, sizeof(rtWidth) );
		memory_writer.w( &rtHeight, sizeof(rtHeight) );
		memory_writer.w( pOrigin, (rtWidth*rtHeight)*4 );
	}

	hr					= pFB->UnlockRect();
}

#endif	//	USE_DX10

void DoAsyncScreenshot()
{
	RImplementation.Target->DoAsyncScreenshot();
}
