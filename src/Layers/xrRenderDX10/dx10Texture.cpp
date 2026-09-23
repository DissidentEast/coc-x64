// Texture.cpp: implementation of the CTexture class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/TextureDDS.h" // D3DX-free DDS parsing (was <d3dx9.h>, <D3DX10Tex.h>)

// DXT compressor implementation (stb_dxt; R1/R2 get theirs from Texture.cpp).
#define STB_DXT_IMPLEMENTATION
#define STBD_FABS(x) ((x) < 0 ? -(x) : (x))
#include "../../3rd party/stb/stb_dxt.h"

#include "../xrRender/dxRenderDeviceRender.h"

// #include "std_classes.h"
// #include "xr_avi.h"

void fix_texture_name(LPSTR fn)
{
	LPSTR _ext = strext(fn);
	if(  _ext					&&
		(0==stricmp(_ext,".tga")	||
		0==stricmp(_ext,".dds")	||
		0==stricmp(_ext,".bmp")	||
		0==stricmp(_ext,".ogm")	) )
		*_ext = 0;
}

int get_texture_load_lod(LPCSTR fn)
{
	CInifile::Sect& sect	= pSettings->r_section("reduce_lod_texture_list");
	CInifile::SectCIt it_	= sect.Data.begin();
	CInifile::SectCIt it_e_	= sect.Data.end();

	ENGINE_API bool is_enough_address_space_available();
	static bool enough_address_space_available = is_enough_address_space_available();

	CInifile::SectCIt it	= it_;
	CInifile::SectCIt it_e	= it_e_;

	for(;it!=it_e;++it)
	{
		if( strstr(fn, it->first.c_str()) )
		{
			if(psTextureLOD<1) {
				if ( enough_address_space_available )
					return 0;
				else
					return 1;
			}
			else
				if(psTextureLOD<3)
					return 1;
				else
					return 2;
		}
	}

	if(psTextureLOD<2) {
//		if ( enough_address_space_available )
			return 0;
//		else
//			return 1;
	}
	else
		if(psTextureLOD<4)
			return 1;
		else
			return 2;
}

u32 calc_texture_size(int lod, u32 mip_cnt, u32 orig_size)
{
	if(1==mip_cnt)
		return orig_size;

	int _lod		= lod;
	float res		= float(orig_size);

	while(_lod>0){
		--_lod;
		res		-= res/1.333f;
	}
	return iFloor	(res);
}

const float		_BUMPHEIGH = 8.f;
//////////////////////////////////////////////////////////////////////
// Utility pack
//////////////////////////////////////////////////////////////////////
IC u32 GetPowerOf2Plus1	(u32 v)
{
	u32 cnt=0;
	while (v) {v>>=1; cnt++; };
	return cnt;
}
IC void	Reduce				(int& w, int& h, int& l, int& skip)
{
	while ((l>1) && skip)
	{
		w /= 2;
		h /= 2;
		l -= 1;

		skip--;
	}
	if (w<1)	w=1;
	if (h<1)	h=1;
}

IC void	Reduce(UINT& w, UINT& h, int l, int skip)
{
	while ((l>1) && skip)
	{
		w /= 2;
		h /= 2;
		l -= 1;

		skip--;
	}
	if (w<1)	w=1;
	if (h<1)	h=1;
}

void				TW_Save	(ID3DTexture2D* T, LPCSTR name, LPCSTR prefix, LPCSTR postfix)
{
	// Debug-only helper, no live callers. Dumps all mips via staging.
	string256		fn;		strconcat	(sizeof(fn),fn,name,"_",prefix,"-",postfix);
	for (int it = 0; it < int(xr_strlen(fn)); it++)
		if ('\\' == fn[it])	fn[it] = '_';
	string256		fn2;	strconcat	(sizeof(fn2),fn2,"debug_",fn,".dds");
	string_path		full;
	FS.update_path	(full,"$logs$",fn2);
	Log						("* debug texture save: ",full);
	if (!T)
	{
		Msg					("! TW_Save: null texture %s", fn);
		return;
	}
	D3D_TEXTURE2D_DESC desc;
	T->GetDesc(&desc);
	if (desc.ArraySize != 1)
	{
		Msg					("! TW_Save: arrays unsupported %s", fn);
		return;
	}
	D3DFORMAT fileFmt = D3DFMT_UNKNOWN;
	switch (desc.Format)
	{
	case DXGI_FORMAT_BC1_UNORM: fileFmt = D3DFMT_DXT1; break;
	case DXGI_FORMAT_BC2_UNORM: fileFmt = D3DFMT_DXT3; break;
	case DXGI_FORMAT_BC3_UNORM: fileFmt = D3DFMT_DXT5; break;
	case DXGI_FORMAT_R8G8B8A8_UNORM: fileFmt = D3DFMT_A8R8G8B8; break;
	case DXGI_FORMAT_B8G8R8X8_UNORM: fileFmt = D3DFMT_X8R8G8B8; break;
	case DXGI_FORMAT_R8_UNORM: fileFmt = D3DFMT_L8; break;
	case DXGI_FORMAT_A8_UNORM: fileFmt = D3DFMT_A8; break;
	default: break;
	}
	u32 blockBytes = 0, bytesPerPixel = 0;
	if (fileFmt == D3DFMT_UNKNOWN || !xrDDS_FormatInfo(fileFmt, &blockBytes, &bytesPerPixel))
	{
		Msg					("! TW_Save: unsupported format %s", fn);
		return;
	}
	IWriter* W = FS.w_open(full);
	if (!W)
	{
		Msg					("! TW_Save: cannot open %s", full);
		return;
	}
	if (!xrDDS_WriteHeader(W, fileFmt, desc.Width, desc.Height, desc.MipLevels))
	{
		FS.w_close(W);
		return;
	}
	for (u32 m = 0; m < desc.MipLevels; ++m)
	{
		u32 lw = _max(1u, desc.Width >> m), lh = _max(1u, desc.Height >> m);
		D3D_TEXTURE2D_DESC sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.Width = lw;
		sd.Height = lh;
		sd.MipLevels = 1;
		sd.ArraySize = 1;
		sd.Format = desc.Format;
		sd.SampleDesc.Count = 1;
		sd.Usage = D3D_USAGE_STAGING;
		sd.CPUAccessFlags = D3D_CPU_ACCESS_READ;
		ID3DTexture2D* staging = NULL;
		if (FAILED(HW.pDevice->CreateTexture2D(&sd, NULL, &staging)))
			break;
		// Single-slice textures only (checked above): subresource == mip.
#ifdef USE_DX11
		HW.pContext->CopySubresourceRegion(staging, 0, 0, 0, 0, T, m, NULL);
#else
		HW.pDevice->CopySubresourceRegion(staging, 0, 0, 0, 0, T, m, NULL);
#endif
		D3D_MAPPED_TEXTURE2D mr;
#ifdef USE_DX11
		HRESULT hrMap = HW.pContext->Map(staging, 0, D3D_MAP_READ, 0, &mr);
#else
		HRESULT hrMap = staging->Map(0, D3D_MAP_READ, 0, &mr);
#endif
		if (SUCCEEDED(hrMap))
		{
			const u32 rows = blockBytes ? ((lh + 3) / 4) : lh;
			const u32 rowBytes = xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel);
			for (u32 y = 0; y < rows; ++y)
				W->w((const u8*)mr.pData + y * mr.RowPitch, rowBytes);
#ifdef USE_DX11
			HW.pContext->Unmap(staging, 0);
#else
			staging->Unmap(0);
#endif
		}
		staging->Release();
		if (FAILED(hrMap))
			break;
	}
	FS.w_close(W);
}
/*
ID3DTexture2D*	TW_LoadTextureFromTexture
(
 ID3DTexture2D*		t_from,
 D3DFORMAT&				t_dest_fmt,
 int						levels_2_skip,
 u32&					w,
 u32&					h
 )
{
	// Calculate levels & dimensions
	ID3DTexture2D*		t_dest			= NULL;
	D3DSURFACE_DESC			t_from_desc0	;
	R_CHK					(t_from->GetLevelDesc	(0,&t_from_desc0));
	int levels_exist		= t_from->GetLevelCount();
	int top_width			= t_from_desc0.Width;
	int top_height			= t_from_desc0.Height;
	Reduce					(top_width,top_height,levels_exist,levels_2_skip);

	// Create HW-surface
	if (D3DX_DEFAULT==t_dest_fmt)	t_dest_fmt = t_from_desc0.Format;
	R_CHK					(D3DXCreateTexture(
		HW.pDevice,
		top_width,top_height,
		levels_exist,0,t_dest_fmt,
		D3DPOOL_MANAGED,&t_dest
		));

	// Copy surfaces & destroy temporary
	ID3DTexture2D* T_src= t_from;
	ID3DTexture2D* T_dst= t_dest;

	int		L_src			= T_src->GetLevelCount	()-1;
	int		L_dst			= T_dst->GetLevelCount	()-1;
	for (; L_dst>=0; L_src--,L_dst--)
	{
		// Get surfaces
		IDirect3DSurface9		*S_src, *S_dst;
		R_CHK	(T_src->GetSurfaceLevel	(L_src,&S_src));
		R_CHK	(T_dst->GetSurfaceLevel	(L_dst,&S_dst));

		// Copy
		R_CHK	(D3DXLoadSurfaceFromSurface(S_dst,NULL,NULL,S_src,NULL,NULL,D3DX_FILTER_NONE,0));

		// Release surfaces
		_RELEASE				(S_src);
		_RELEASE				(S_dst);
	}

	// OK
	w						= top_width;
	h						= top_height;
	return					t_dest;
}

template	<class _It>
IC	void	TW_Iterate_1OP
(
 ID3DTexture2D*		t_dst,
 ID3DTexture2D*		t_src,
 const _It				pred
 )
{
	DWORD mips							= t_dst->GetLevelCount();
	R_ASSERT							(mips == t_src->GetLevelCount());
	for (DWORD i = 0; i < mips; i++)	{
		D3DLOCKED_RECT				Rsrc,Rdst;
		D3DSURFACE_DESC				desc,descS;

		t_dst->GetLevelDesc			(i, &desc);
		t_src->GetLevelDesc			(i, &descS);
		VERIFY						(desc.Format==descS.Format);
		VERIFY						(desc.Format==D3DFMT_A8R8G8B8);
		t_src->LockRect				(i,&Rsrc,0,0);
		t_dst->LockRect				(i,&Rdst,0,0);
		for (u32 y = 0; y < desc.Height; y++)	{
			for (u32 x = 0; x < desc.Width; x++)	{
				DWORD&	pSrc	= *(((DWORD*)((BYTE*)Rsrc.pBits + (y * Rsrc.Pitch)))+x);
				DWORD&	pDst	= *(((DWORD*)((BYTE*)Rdst.pBits + (y * Rdst.Pitch)))+x);
				pDst			= pred(pDst,pSrc);
			}
		}
		t_dst->UnlockRect			(i);
		t_src->UnlockRect			(i);
	}
}
template	<class _It>
IC	void	TW_Iterate_2OP
(
 ID3DTexture2D*		t_dst,
 ID3DTexture2D*		t_src0,
 ID3DTexture2D*		t_src1,
 const _It				pred
 )
{
	DWORD mips							= t_dst->GetLevelCount();
	R_ASSERT							(mips == t_src0->GetLevelCount());
	R_ASSERT							(mips == t_src1->GetLevelCount());
	for (DWORD i = 0; i < mips; i++)	{
		D3DLOCKED_RECT				Rsrc0,Rsrc1,Rdst;
		D3DSURFACE_DESC				desc,descS0,descS1;

		t_dst->GetLevelDesc			(i, &desc);
		t_src0->GetLevelDesc		(i, &descS0);
		t_src1->GetLevelDesc		(i, &descS1);
		VERIFY						(desc.Format==descS0.Format);
		VERIFY						(desc.Format==descS1.Format);
		VERIFY						(desc.Format==D3DFMT_A8R8G8B8);
		t_src0->LockRect			(i,&Rsrc0,	0,0);
		t_src1->LockRect			(i,&Rsrc1,	0,0);
		t_dst->LockRect				(i,&Rdst,	0,0);
		for (u32 y = 0; y < desc.Height; y++)	{
			for (u32 x = 0; x < desc.Width; x++)	{
				DWORD&	pSrc0	= *(((DWORD*)((BYTE*)Rsrc0.pBits + (y * Rsrc0.Pitch)))+x);
				DWORD&	pSrc1	= *(((DWORD*)((BYTE*)Rsrc1.pBits + (y * Rsrc1.Pitch)))+x);
				DWORD&	pDst	= *(((DWORD*)((BYTE*)Rdst.pBits  + (y * Rdst.Pitch)))+x);
				pDst			= pred(pDst,pSrc0,pSrc1);
			}
		}
		t_dst->UnlockRect			(i);
		t_src0->UnlockRect			(i);
		t_src1->UnlockRect			(i);
	}
}

IC u32 it_gloss_rev		(u32 d, u32 s)	{	return	color_rgba	(
	color_get_A(s),		// gloss
	color_get_B(d),
	color_get_G(d),
	color_get_R(d)		);
}
IC u32 it_gloss_rev_base(u32 d, u32 s)	{	
	u32		occ		= color_get_A(d)/3;
	u32		def		= 8;
	u32		gloss	= (occ*1+def*3)/4;
	return	color_rgba	(
		gloss,			// gloss
		color_get_B(d),
		color_get_G(d),
		color_get_R(d)
		);
}
IC u32 it_difference	(u32 d, u32 orig, u32 ucomp)	{	return	color_rgba(
	128+(int(color_get_R(orig))-int(color_get_R(ucomp)))*2,		// R-error
	128+(int(color_get_G(orig))-int(color_get_G(ucomp)))*2,		// G-error
	128+(int(color_get_B(orig))-int(color_get_B(ucomp)))*2,		// B-error
	128+(int(color_get_A(orig))-int(color_get_A(ucomp)))*2	);	// A-error	
}
IC u32 it_height_rev	(u32 d, u32 s)	{	return	color_rgba	(
	color_get_A(d),					// diff x
	color_get_B(d),					// diff y
	color_get_G(d),					// diff z
	color_get_R(s)	);				// height
}
IC u32 it_height_rev_base(u32 d, u32 s)	{	return	color_rgba	(
	color_get_A(d),					// diff x
	color_get_B(d),					// diff y
	color_get_G(d),					// diff z
	(color_get_R(s)+color_get_G(s)+color_get_B(s))/3	);	// height
}
*/
ID3DBaseTexture*	CRender::texture_load(LPCSTR fRName, u32& ret_msize, bool bStaging)
{
	// D3DFORMAT -> DXGI_FORMAT for the file formats the engine loads.
	auto xrDXGI_Format = [](D3DFORMAT f) -> DXGI_FORMAT
	{
		switch (f)
		{
		case D3DFMT_DXT1: return DXGI_FORMAT_BC1_UNORM;
		case D3DFMT_DXT2:
		case D3DFMT_DXT3: return DXGI_FORMAT_BC2_UNORM;
		case D3DFMT_DXT4:
		case D3DFMT_DXT5: return DXGI_FORMAT_BC3_UNORM;
		case D3DFMT_A8R8G8B8: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case D3DFMT_X8R8G8B8: return DXGI_FORMAT_B8G8R8X8_UNORM;
		case D3DFMT_L8: return DXGI_FORMAT_R8_UNORM;
		case D3DFMT_A8: return DXGI_FORMAT_A8_UNORM;
		default: return DXGI_FORMAT_UNKNOWN;
		}
	};

	//	Moved here just to avoid warning
	XR_DDSInfo					IMG;
	ZeroMemory(&IMG, sizeof(IMG));

	//	Staging control
	static bool bAllowStaging = !strstr(Core.Params,"-no_staging");
	bStaging &= bAllowStaging;

	ID3DBaseTexture*		pTexture2D		= NULL;
	//IDirect3DCubeTexture9*	pTextureCUBE	= NULL;
	string_path				fn;
	//u32						dwWidth,dwHeight;
	u32						img_size		= 0;
	int						img_loaded_lod	= 0;
	//D3DFORMAT				fmt;
	u32						mip_cnt=u32(-1);
	// validation
	R_ASSERT				(fRName);
	R_ASSERT				(fRName[0]);

	// make file name
	string_path				fname;
	xr_strcpy(fname,fRName); //. andy if (strext(fname)) *strext(fname)=0;
	fix_texture_name		(fname);
	IReader* S				= NULL;
	//if (!FS.exist(fn,"$game_textures$",	fname,	".dds")	&& strstr(fname,"_bump"))	goto _BUMP_from_base;
	if (strstr(fname,"_bump")) 
	{
		if (!FS.exist(fn,"$game_textures$",	fname,	".dds"))
			goto _BUMP_from_base;
		else if (strstr(Core.Params,"-no_bump_mode2"))
		{
			if (strstr(fname,"_bump#"))
			{
			R_ASSERT2	(FS.exist(fn,"$game_textures$",	"ed\\ed_dummy_bump#",	".dds"), "ed_dummy_bump#");
			S						= FS.r_open	(fn);
			R_ASSERT2				(S, fn);
			img_size				= S->length	();
			goto		_DDS_2D;
			}
		
			R_ASSERT2	(FS.exist(fn,"$game_textures$",	"ed\\ed_dummy_bump",	".dds"),"ed_dummy_bump");
			S						= FS.r_open	(fn);

			R_ASSERT2	(S, fn);

			img_size				= S->length	();
			goto		_DDS_2D;
		}
		else if (strstr(Core.Params,"-no_bump_mode1") && strstr(fname,"_bump#"))
		{
			R_ASSERT2	(FS.exist(fn,"$game_textures$",	"ed\\ed_dummy_bump#",	".dds"), "ed_dummy_bump#");
			S						= FS.r_open	(fn);
			R_ASSERT2				(S, fn);
			img_size				= S->length	();
			goto		_DDS_2D;
		}
	} 	
	if (FS.exist(fn,"$level$",			fname,	".dds"))							goto _DDS;
	if (FS.exist(fn,"$game_saves$",		fname,	".dds"))							goto _DDS;
	if (FS.exist(fn,"$game_textures$",	fname,	".dds"))							goto _DDS;


#ifdef _EDITOR
	ELog.Msg(mtError,"Can't find texture '%s'",fname);
	return 0;
#else

	Msg("! Can't find texture '%s'",fname);
	R_ASSERT(FS.exist(fn,"$game_textures$",	"ed\\ed_not_existing_texture",".dds"));
	goto _DDS;

	//	Debug.fatal(DEBUG_INFO,"Can't find texture '%s'",fname);

#endif

_DDS:
	{
		// Load and get header

		S						= FS.r_open	(fn);
#ifdef DEBUG
		Msg						("* Loaded: %s[%d]",fn,S->length());
#endif // DEBUG
		img_size				= S->length	();
		R_ASSERT				(S);
		R_CHK2					(xrDDS_Parse(S->pointer(), S->length(), &IMG), fn);
		if (IMG.faces == 6)												goto _DDS_CUBE;
		else															goto _DDS_2D;

_DDS_CUBE:
		{
			DXGI_FORMAT fmt = xrDXGI_Format(IMG.format);
			if (fmt == DXGI_FORMAT_UNKNOWN)
				R_CHK2(E_FAIL, fn);

			D3D_USAGE usage;
			UINT bindFlags;
			UINT cpuAccess;
			if (bStaging)
			{
				usage = D3D_USAGE_STAGING;
				bindFlags = 0;
				cpuAccess = D3D_CPU_ACCESS_WRITE;
			}
			else
			{
				usage = D3D_USAGE_DEFAULT;
				bindFlags = D3D_BIND_SHADER_RESOURCE;
				cpuAccess = 0;
			}

			// Subresources are face-major (D3D11CalcSubresource: mip + face * mips).
			// Top level is file-verbatim (raw copy, D3DX does not swizzle);
			// the rest of the full chain is box-generated (all corpus cubes
			// are single-mip).
			u32 fullMips = 1;
			for (u32 w = IMG.width, h = IMG.height; w > 1 || h > 1; w /= 2, h /= 2)
				++fullMips;
			const u32 subCount = fullMips * 6;
			D3D_SUBRESOURCE_DATA* initData = xr_alloc<D3D_SUBRESOURCE_DATA>(subCount);
			u8** owned = xr_alloc<u8*>(subCount);
			for (u32 i = 0; i < subCount; ++i)
				owned[i] = NULL;
			u8** chains = xr_alloc<u8*>(6 * fullMips);
			for (u32 i = 0; i < 6 * fullMips; ++i)
				chains[i] = NULL;
			const u8* fileBase = (const u8*)S->pointer() + IMG.dataOffset;
			const u8* src = fileBase;
			u32 blockBytes = 0, bytesPerPixel = 0;
			xrDDS_FormatInfo(IMG.format, &blockBytes, &bytesPerPixel);
			// One face's worth of file bytes (faces are stored face-major).
			u32 faceBytes = 0;
			for (u32 m = 0, w = IMG.width, h = IMG.height; m < IMG.mips; ++m)
			{
				faceBytes += xrDDS_LevelSize(_max(1u, w), _max(1u, h), blockBytes, bytesPerPixel);
				w /= 2;
				h /= 2;
			}
			HRESULT hrCube = S_OK;
			for (u32 f = 0; f < 6 && SUCCEEDED(hrCube); ++f)
			{
				// ARGB chain for this face (decoded top + box rest).
				const u8* faceSrc = fileBase + f * faceBytes;
				u32 cw = IMG.width, ch = IMG.height;
				u32 pw = cw, ph = ch;
				for (u32 m = 0; m < fullMips && SUCCEEDED(hrCube); ++m)
				{
					u8* level = xr_alloc<u8>(cw * ch * 4);
					chains[f * fullMips + m] = level;
					if (m == 0)
					{
						if (!xrDDS_DecodeLevelToARGB(IMG.format, faceSrc,
								xrDDS_LevelRowBytes(cw, ch, blockBytes, bytesPerPixel),
								cw, ch, level, cw * 4))
							hrCube = E_FAIL;
					}
					else
					{
						xrARGB_BoxLevel(chains[f * fullMips + m - 1], pw * 4, pw, ph,
							level, cw * 4, cw, ch);
					}
					pw = cw;
					ph = ch;
					cw = _max(1u, cw / 2);
					ch = _max(1u, ch / 2);
				}
				// Emit file mips verbatim, generate the rest.
				u32 w = IMG.width, hh = IMG.height;
				for (u32 m = 0; m < fullMips && SUCCEEDED(hrCube); ++m)
				{
					const u32 lw = _max(1u, w), lh = _max(1u, hh);
					const u32 subIdx = f * fullMips + m;
					D3D_SUBRESOURCE_DATA* sub = &initData[subIdx];
					if (m < IMG.mips)
					{
						sub->pSysMem = src;
						sub->SysMemPitch = xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel);
						sub->SysMemSlicePitch = 0;
						src += xrDDS_LevelSize(lw, lh, blockBytes, bytesPerPixel);
					}
					else if (blockBytes)
					{
						u8* enc = xr_alloc<u8>(xrDDS_LevelSize(lw, lh, blockBytes, bytesPerPixel));
						owned[subIdx] = enc;
						xrBC_EncodeLevel(IMG.format, chains[f * fullMips + m], lw * 4,
							lw, lh, enc, xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel));
						sub->pSysMem = enc;
						sub->SysMemPitch = xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel);
						sub->SysMemSlicePitch = 0;
					}
				else if (!blockBytes && bytesPerPixel == 1)
				{
					// L8/A8 generated tail: collapse the ARGB chain level.
					u8* flat = xr_alloc<u8>(lw * lh);
					owned[subIdx] = flat;
					xrARGB_Collapse(chains[f * fullMips + m], lw * 4, lw, lh, flat, lw,
						(IMG.format == D3DFMT_A8) ? 3u : 0u);
					sub->pSysMem = flat;
					sub->SysMemPitch = lw;
					sub->SysMemSlicePitch = 0;
				}
				else
				{
					sub->pSysMem = chains[f * fullMips + m];
					sub->SysMemPitch = lw * 4;
					sub->SysMemSlicePitch = 0;
				}
					w /= 2;
					hh /= 2;
				}
			}
			ID3DTexture2D* texCube = NULL;
			if (SUCCEEDED(hrCube))
			{
				D3D_TEXTURE2D_DESC desc;
				ZeroMemory(&desc, sizeof(desc));
				desc.Width = IMG.width;
				desc.Height = IMG.height;
				desc.MipLevels = fullMips;
				desc.ArraySize = 6;
				desc.Format = fmt;
				desc.SampleDesc.Count = 1;
				desc.Usage = usage;
				desc.BindFlags = bindFlags;
				desc.CPUAccessFlags = cpuAccess;
				desc.MiscFlags = D3D_RESOURCE_MISC_TEXTURECUBE;
				hrCube = HW.pDevice->CreateTexture2D(&desc, initData, &texCube);
			}
			for (u32 i = 0; i < subCount; ++i)
				if (owned[i])
					xr_free(owned[i]);
			for (u32 i = 0; i < 6 * fullMips; ++i)
				if (chains[i])
					xr_free(chains[i]);
			xr_free(chains);
			xr_free(owned);
			xr_free(initData);
			R_CHK(hrCube);
			pTexture2D = texCube;

			FS.r_close				(S);

			// OK
			mip_cnt					= IMG.mips;
			ret_msize				= calc_texture_size(img_loaded_lod, mip_cnt, img_size);
			return					pTexture2D;
		}
_DDS_2D:
	{
		// Check for LMAP and compress if needed
		strlwr					(fn);

		img_loaded_lod			= get_texture_load_lod(fn);

		// The bump-fallback jumps above land here with a fresh S; (re)parse
		// the header — idempotent for the _DDS path, required for them.
		R_CHK2					(xrDDS_Parse(S->pointer(), S->length(), &IMG), fn);

		DXGI_FORMAT fmt = xrDXGI_Format(IMG.format);
			if (fmt == DXGI_FORMAT_UNKNOWN)
				R_CHK2(E_FAIL, fn);

			D3D_USAGE usage;
			UINT bindFlags;
			UINT cpuAccess;
			if (bStaging)
			{
				usage = D3D_USAGE_STAGING;
				bindFlags = 0;
				cpuAccess = D3D_CPU_ACCESS_WRITE;
			}
			else
			{
				usage = D3D_USAGE_DEFAULT;
				bindFlags = D3D_BIND_SHADER_RESOURCE;
				cpuAccess = 0;
			}

			u32 blockBytes = 0, bytesPerPixel = 0;
			xrDDS_FormatInfo(IMG.format, &blockBytes, &bytesPerPixel);

		if (IMG.depth > 1)
		{
			// Volume texture (e.g. water SBumpVolume). D3D11 wants one
			// subresource per mip (all slices contiguous); file layout
			// already matches, so this is a raw copy (D3DX does not swizzle).
			D3D_SUBRESOURCE_DATA* initData = xr_alloc<D3D_SUBRESOURCE_DATA>(IMG.mips);
			const u8* src = (const u8*)S->pointer() + IMG.dataOffset;
			u32 w = IMG.width, hh = IMG.height, dd = IMG.depth;
			HRESULT hrVol = S_OK;
			for (u32 m = 0; m < IMG.mips && SUCCEEDED(hrVol); ++m)
			{
				const u32 lw = _max(1u, w), lh = _max(1u, hh), ld = _max(1u, dd);
				const u32 levelSize = xrDDS_LevelSize(lw, lh, blockBytes, bytesPerPixel);
				D3D_SUBRESOURCE_DATA* dst = &initData[m];
				dst->pSysMem = src;
				dst->SysMemPitch = xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel);
				dst->SysMemSlicePitch = levelSize;
				src += ld * levelSize;
				w /= 2;
				hh /= 2;
				dd /= 2;
			}
				ID3DTexture3D* texVolume = NULL;
				if (SUCCEEDED(hrVol))
				{
					D3D_TEXTURE3D_DESC desc;
					ZeroMemory(&desc, sizeof(desc));
					desc.Width = IMG.width;
					desc.Height = IMG.height;
					desc.Depth = IMG.depth;
					desc.MipLevels = IMG.mips;
					desc.Format = fmt;
					desc.Usage = usage;
					desc.BindFlags = bindFlags;
					desc.CPUAccessFlags = cpuAccess;
					desc.MiscFlags = 0;
				hrVol = HW.pDevice->CreateTexture3D(&desc, initData, &texVolume);
			}
			xr_free(initData);
			R_CHK(hrVol);
				pTexture2D = texVolume;

				FS.r_close				(S);
				mip_cnt					= IMG.mips;
				// OK
				ret_msize				= calc_texture_size(img_loaded_lod, mip_cnt, img_size);
				return					pTexture2D;
			}

			// Requested size (LOD). D3DX10/11 behavior, proven by probe:
			// - requested == file dims: file chain verbatim (+box completion
			//   to a full chain when the file is short);
			// - requested smaller: resample the top + box-generate the chain.
			u32 reqW = IMG.width, reqH = IMG.height;
			if (img_loaded_lod)
				Reduce(reqW, reqH, IMG.mips, img_loaded_lod);
			const bool resample = (reqW != IMG.width || reqH != IMG.height);

			// Full chain length of the requested top.
			u32 fullMips = 1;
			for (u32 w = reqW, h = reqH; w > 1 || h > 1; w /= 2, h /= 2)
				++fullMips;

			D3D_SUBRESOURCE_DATA* initData = xr_alloc<D3D_SUBRESOURCE_DATA>(fullMips);
			u8** owned = xr_alloc<u8*>(fullMips);
			for (u32 i = 0; i < fullMips; ++i)
				owned[i] = NULL;
			const u8* src = (const u8*)S->pointer() + IMG.dataOffset;
			// Full ARGB working chain (decoded file top, resampled if needed,
			// then box-filtered). Backs every generated level.
			u8** chain = xr_alloc<u8*>(fullMips);
			for (u32 i = 0; i < fullMips; ++i)
				chain[i] = NULL;
			HRESULT hrTex = S_OK;
			{
				u8* fileTop = xr_alloc<u8>(IMG.width * IMG.height * 4);
				if (!xrDDS_DecodeLevelToARGB(IMG.format, src,
						xrDDS_LevelRowBytes(IMG.width, IMG.height, blockBytes, bytesPerPixel),
						IMG.width, IMG.height, fileTop, IMG.width * 4))
					hrTex = E_FAIL;
				else
				{
					chain[0] = xr_alloc<u8>(reqW * reqH * 4);
					if (resample)
						xrResample_Bilinear(fileTop, IMG.width * 4, IMG.width, IMG.height,
							chain[0], reqW * 4, reqW, reqH);
					else
						CopyMemory(chain[0], fileTop, reqW * reqH * 4);
					u32 pw = reqW, ph = reqH;
					for (u32 m = 1; m < fullMips && SUCCEEDED(hrTex); ++m)
					{
						const u32 cw = _max(1u, pw / 2), ch = _max(1u, ph / 2);
						chain[m] = xr_alloc<u8>(cw * ch * 4);
						xrARGB_BoxLevel(chain[m - 1], pw * 4, pw, ph, chain[m], cw * 4, cw, ch);
						pw = cw;
						ph = ch;
					}
				}
				xr_free(fileTop);
			}
			for (u32 m = 0; m < fullMips && SUCCEEDED(hrTex); ++m)
			{
				const u32 lw = _max(1u, reqW >> m), lh = _max(1u, reqH >> m);
				D3D_SUBRESOURCE_DATA* dst = &initData[m];
				const bool verbatim = !resample && m < IMG.mips;
				if (verbatim)
				{
					// File bytes (raw copy, including D3DX's own R/B quirk).
					dst->pSysMem = src;
					dst->SysMemPitch = xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel);
					dst->SysMemSlicePitch = 0;
					src += xrDDS_LevelSize(lw, lh, blockBytes, bytesPerPixel);
				}
				else if (blockBytes)
				{
					// Encode the ARGB chain level (punch-aware DXT1 included).
					u8* enc = xr_alloc<u8>(xrDDS_LevelSize(lw, lh, blockBytes, bytesPerPixel));
					owned[m] = enc;
					xrBC_EncodeLevel(IMG.format, chain[m], lw * 4, lw, lh, enc,
						xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel));
					dst->pSysMem = enc;
					dst->SysMemPitch = xrDDS_LevelRowBytes(lw, lh, blockBytes, bytesPerPixel);
					dst->SysMemSlicePitch = 0;
				}
			else if (!blockBytes && bytesPerPixel == 1)
			{
				// L8/A8 generated (LOD resample or short-chain tail): the
				// chain is ARGB, the texture is 1 byte/pixel.
				u8* flat = xr_alloc<u8>(lw * lh);
				owned[m] = flat;
				xrARGB_Collapse(chain[m], lw * 4, lw, lh, flat, lw,
					(IMG.format == D3DFMT_A8) ? 3u : 0u);
				dst->pSysMem = flat;
				dst->SysMemPitch = lw;
				dst->SysMemSlicePitch = 0;
			}
			else
			{
				// Uncompressed: raw ARGB bytes (D3DX does not swizzle).
				dst->pSysMem = chain[m];
				dst->SysMemPitch = lw * 4;
				dst->SysMemSlicePitch = 0;
			}
		}
		ID3DTexture2D* tex2D = NULL;
			if (SUCCEEDED(hrTex))
			{
				D3D_TEXTURE2D_DESC desc;
				ZeroMemory(&desc, sizeof(desc));
				desc.Width = reqW;
				desc.Height = reqH;
				desc.MipLevels = fullMips;
				desc.ArraySize = 1;
				desc.Format = fmt;
				desc.SampleDesc.Count = 1;
				desc.Usage = usage;
				desc.BindFlags = bindFlags;
				desc.CPUAccessFlags = cpuAccess;
				desc.MiscFlags = 0;
				hrTex = HW.pDevice->CreateTexture2D(&desc, initData, &tex2D);
			}
			for (u32 i = 0; i < fullMips; ++i)
			{
				if (owned[i])
					xr_free(owned[i]);
				if (chain[i])
					xr_free(chain[i]);
			}
			xr_free(chain);
			xr_free(owned);
			xr_free(initData);
			R_CHK(hrTex);
			pTexture2D = tex2D;

			FS.r_close				(S);
			mip_cnt					= IMG.mips;
			// OK
			ret_msize				= calc_texture_size(img_loaded_lod, mip_cnt, img_size);
			return					pTexture2D;
		}
	}

_BUMP_from_base:
	{
		//Msg			("! auto-generated bump map: %s",fname);
		Msg			("! Fallback to default bump map: %s",fname);
		//////////////////
		if (strstr(fname,"_bump#"))			
		{
			R_ASSERT2	(FS.exist(fn,"$game_textures$",	"ed\\ed_dummy_bump#",	".dds"), "ed_dummy_bump#");
			S						= FS.r_open	(fn);
			R_ASSERT2				(S, fn);
			img_size				= S->length	();
			goto		_DDS_2D;
		}
		if (strstr(fname,"_bump"))			
		{
			R_ASSERT2	(FS.exist(fn,"$game_textures$",	"ed\\ed_dummy_bump",	".dds"),"ed_dummy_bump");
			S						= FS.r_open	(fn);

			R_ASSERT2	(S, fn);

			img_size				= S->length	();
			goto		_DDS_2D;
		}
		//////////////////
	}

	return 0;
}
