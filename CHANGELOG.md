# Changelog (legend: `*` major / `~` minor / `=` bugfix / `-` removed / `+` added)

## D3DX removal 7d3: screenshot writers (2026-09-23)

+ Vendored `stb_image_write.h` (public domain; JPG/BMP via `*_to_func`
  into engine writers; CRT calls routed to `Memory.*`, no stdio).
= `r__screenshot.cpp` no longer calls D3DX save/load: DX10/11 thumbs
  (staging readback + bilinear + BC1 + DDS writer), JPG/BMP (stb),
  DX9 thumbs/JPG/TGA (same writers + `tga.h`), half-float via exact
  `xrHalfToFloatArray`; `TW_Save` (DX10) implemented via staging + DDS.
= `TextureDDS.h`: factored `xrDDS_WriteHeader`, added `xrDDS_SaveLevels`
  (memory levels) + 24-bit RGB DDS case.
= Verified: JPG + 128px DDS thumb captured in-level on all four
  renderers (R1/R2 need fullscreen — DX9 windowed screenshots stay
  disabled, vanilla parity); DDS round-trip probe byte-exact (8320 B);
  build 0 errors, 0 warnings.
~ Note: `small` is `#define`d to `char` by `rpcndr.h` — never use it
  as an identifier in Windows TUs (found via preprocessor bisection).

## D3DX removal: R3 10.1 device + shader creation (2026-09-23)

= R3 created `x_4_1` bytecode (whenever the GPU exposes 10.1) but passed
  it to the base 10.0 device, which rejects it with `E_INVALIDARG` — R3
  died at the first shader (`dumb.ps`) on every 10_1-capable GPU.
= Fix: prefer a real 10.1 device (`D3D10CreateDeviceAndSwapChain1`,
  10.0 fallback; links `d3d10_1.lib`) and create VS/PS/GS through
  `HW.pDevice1` when present; `FeatureLevel` now reflects reality.
  (A bare `QueryInterface` for the 10.1 interface is NOT equivalent to
  what `D3DX10GetFeatureLevel1` guaranteed — proven by probe: 4_1
  creation fails on the 10.0 device even when the QI succeeds.)
= Drive-by: renamed shadowing `disasm` local in `create_shader` (C4457).
= Verified: R3 menu boot + in-level screenshot (sun/sky/HUD clean);
  build 0 errors, 0 warnings.

## D3DX removal 7d2: DX10/11 texture pipeline (2026-09-23)

= `dx10Texture.cpp` load (2D/cube/volume, LOD resample + box chain,
  raw copy), `dx10HW.cpp` device create (`D3D10CreateDeviceAndSwapChain` +
  `QueryInterface(ID3D10Device1)`), R3/R4 jitter CPU point-mips.
= Fixed after in-level testing (menu boot was NOT enough): cube
  subresources are face-major (`f*mips+m`, was transposed — banded sky),
  cube chains decode their own face (was face 0 everywhere), volumes pass
  one subresource per mip (was per-slice), L8/A8 generated tails collapse
  to 1 byte/pixel, bump-fallback jumps parse the header (was zeroed IMG),
  `TW_LoadTextureFromTexture` converts any format via ARGB (was 3 cases +
  NULL crash), stb casts + `result` shadowing (0 warnings).
= Verified: R2 in-level pixel-clean; R4 in-level fixed (same spawn,
  windowed screenshots) — sky/terrain/water natural, actors/UI/minimap OK.
~ Known: R3 stops at `dumb.ps` creation (under investigation; R4
  unaffected); short/corrupt DDS handling in R4 still to be hardened
  (DX9 falls back to placeholder).

## D3DX removal 7d1: DX9 texture pipeline (2026-09-22)

+ Added `Layers/xrRender/TextureDDS.h`: DDS parse/load (2D/cube/forced
  ARGB), exact BC1/2/3 decode, bilinear NPOT resample, box-filter chain
  completion, Sobel normal maps, surface copy/convert, DDS writer.
+ Vendored `stb_dxt.h` (public domain) for DXT5 compression.
= `Texture.cpp` load/bump paths, `TW_LoadTextureFromTexture` conversions,
  R2 rendertarget/ material/jitter creation, and the DDI log line are D3DX-free.
= Verified: all 5354 game DDS load byte-identical (except D3DX's own
  sub-4x4 sanitization + 3 resampled files, both visually irrelevant);
  normal orientation/scale proven on ramps; R2 level load OK.
~ Known approximation: normal kernel is Sobel (rms ~10 vs D3DX, below the
  DXT5 noise floor); BC encoder differs from D3DX's (same class).

## D3DX removal 7c: shader path (2026-09-22)

+ Added `Layers/xrRender/ShaderCTAB.h`: CTAB binary mirrors, comment-token
  scanner (`xrFindShaderCTAB`), caps-based profile picker.
= R1/R2 `shader_compile` now uses `D3DCompile` (+ backwards-compat flag,
  the successor of the dropped LEGACY bit), `create_shader` uses the CTAB
  scanner + `D3DDisassemble`; `r_constants` parses the mirrors; `xrD3DDefs`
  takes `ID3DBlob`/`ID3DInclude`/`D3D_SHADER_MACRO` from `d3dcommon.h`.
= Fixed 6 `r2/*.hlsl` using `point` as an identifier (reserved in modern
  HLSL; r3 already used `pnt`) + screenshot blob types per DX branch.
= Verified: probe proves identical constant tables vs D3DX on 202 shaders;
  build 0 errors; R2 menu + full level load OK (153 shaders compiled
  in-game, no failures).
~ Editor-only `_CreateVS/PS` left for the wrapper island (7e).

## D3DX removal 7b2: sun TSM + 3DFluid math (2026-09-22)

+ Added `Layers/xrRender/SunTSM.h`: D3DX-free TSM vocabulary (SunVec2/3/4,
  SunPlane, SunMatrix over Fmatrix) proven equivalent to D3DX9 by probe
  (200 random 4x4 inverses, 50 orthos/transform batches, plane/vector ops).
= Converted all three `r2_R_sun.cpp` (R2/R3/R4) and the 3DFluid renderer/
  grid to it (Fmatrix/Fvector + Sun helpers; projective inverses stay full
  4x4; `Fmatrix::transform(Fvector4,Fvector)` was NOT a substitute for
  `D3DXVec3Transform` since it divides by w).
= Verified: build 0 errors, menu boot OK.
~ Next: shader path (D3DCompile/D3DReflect), textures, dxerr, wrapper purge.

## D3DX removal 7b1: small math sites (2026-09-22)

= `device.cpp` full-view inverse, `R_Backend` clip-plane inverse-transpose,
  and the occlusion ortho fill now use DirectXMath / direct formula instead
  of D3DX (Fmatrix 4x3 invert is not valid for projective matrices).
= Verified: build 0 errors, menu boot OK.
~ Next: sun TSM x3 + 3DFluid math.

## D3DX removal 7a: mesh mender + decl helpers (2026-09-22)

- Dropped `d3dx9.h` from `common/NvMender2003` (self-contained `MenderVec3`
  + inline Dot/Cross/Length/Normalize; only `d3d9.h` FVF codes remain).
= Fixed a latent uninitialized-read in `SetUpFaceVectors` (old hand-rolled
  normalize multiplied garbage) and three for-scope leaks, found while
  probe-compiling the file with `cl.exe`.
+ Added D3DX-free `xrDeclaratorFromFVF`/`xrGetFVFVertexSize`/
  `xrGetDeclLength`/`xrGetDeclVertexSize` (same semantics) to all four
  `_d3d_extensions.h` copies; ~30 call sites switched (VDeclarator, FVisual,
  FSkinned, ResourceManagers, all mesh loaders, fluid grid, fmesh).
= Verified: decl-probe proves byte-identical declarations vs real D3DX9 on
  14 FVFs; full build 0 errors; boots to menu (stride VERIFYs pass).
~ Still on D3DX: math, textures, shaders, dxerr, wrapper (next islands).

## GameSpy removal 6b (2026-09-22)

* Removed the dead online-service layer (service shut down in 2014; CoC is
  single-player only): vendored GameSpy SDK (`src/xrGameSpy/gamespy`, ~1100
  files), the `xrGameSpy` project, X-Ray wrappers (`src/xrGame/gamespy`),
  account/login/profile/stats/atlas managers and stores, cdkey/QR2/server
  heartbeat code, and the orphaned `mp_gpprof_server` tool (731 files, was not
  in the solution).
+ Added `src/xrCore/xr_shared_defs.h`: the few still-needed registry/port/
  version constants salvaged from `xrGameSpy_MainDefs.h`; relocated stock RSA
  `md5c.c/.h` to `src/xrCore/`.
= Hollowed `reward_event_generator` and `player_account` (interfaces kept for
  core/MP-mode callers, service guts removed); hollowed `CServerList` to an
  always-empty list (moved plain `ServerInfo` data into its header);
  `MainMenu` drops all MP managers/patch/download flows, keeps working
  `GetGSVer`/`ValidateCDKey`/`GetPlayerName`/`GetCDKeyFromRegistry` stubs.
= Fixed the transitive-include fallout properly: `script_callback_ex.h` now
  pulls `pch_script.h` directly (same pattern as `mixed_delegate.h`), and the
  9 TUs using `ai().script_engine().functor(...)` include what they use.
= Removed the `M_GAMESPY_CDKEY_VALIDATION_*` net messages (enum + log strings
  kept in sync); `strlwr` -> `_strlwr`.
~ Known standing warning: C4789 in libjpeg `jcapimin.c` (LTCG false positive;
  identical shared headers, runtime structsize guard).
~ Verified: full build 0 errors, game boots to main menu on the new binaries.

## LuaJIT 2.1 + GC64 (2026-09-22)

* Vendor-drop upgrade of the bundled LuaJIT 2.0.4 to upstream v2.1 branch tip;
  x64 builds now default to GC64 (`ffi.abi('gc64') == true`), lifting the Lua
  heap from the 2 GB low-address ceiling to the 128 TB space.
- Deleted the hand-rolled 128 MB low-2GB Lua heap (`xr_alloc.c/.h` plus the
  `lj_alloc.c`/`msvcbuild.bat` hooks) — its entire reason to exist is gone
  with GC64; also refreshed the stale DynASM 1.3.0 tooling to the matching
  1.5.0 set so `buildvm` regenerates cleanly.
= Fixed 5 new upstream warnings locally (2x C5287 enum casts, 3x C4244
  narrowing casts); build stays at 0 errors, 0 warnings.
~ Scripts: `math.mod` -> `math.fmod` and `table.getn(t)` -> `#(t)` (8 lines in
  4 files) — both 5.0 leftovers were dropped by 2.1 and crashed the menu-level
  load (`xr_wounded.script:417`). Verified: main-menu boot on the new runtime.
~ Staging note: the x64 `lua51.dll` now comes out of the v2.1 tree.

## VS2022 build (2026-09-22) — Release x64, 0 errors, 0 warnings

* Built with Visual Studio 2022 (platform toolset v143, was v120/VS2013);
  solution auto-migrated, Release|x64 verified end-to-end up to a running
  main-menu window on a GeForce RTX 3060 Ti.
* Replaced MSVC removed/legacy STL pieces with modern equivalents instead of
  silencing diagnostics: `hash_map`/`hash_set` -> `unordered_*`,
  `bind1st`/`bind2nd` -> lambdas, `binary_function`/`unary_function` dropped,
  `std::_Construct`/`_Destroy` -> placement-new / explicit dtor calls,
  `snprintf` guard for `_MSC_VER < 1900`, `tan`/`fabs`/`sin` -> `tanf`/`fabsf`/`sinf`.
= Fixed every `/Zc:forScope-` D9035 (`<ForceConformanceInForLoopScope>` removed
  project-wide) and `/GS-` vs `SDLCheck` D9025 conflicts.
= Fixed `/GL`+LTCG link gaps where VS2022 inlines harder than VS2013 and drops
  the out-of-line copy: de-inlined `IC` defs that lived in a single .cpp
  (`static_obstacles_avoider::object`, `CAttachableItem::object`,
  `CSpaceRestrictionManager::restriction/restriction_presented`,
  `stalker_movement_params::cover_loophole_id`,
  `CSE_ALifeCreatureAbstract::set_health/set_killer_id`,
  `script_server_object_version`, `play_delayed_callbacks`),
  moved `CScriptGameObject::object()` into `script_game_object.cpp`, and added
  the missing `*_impl.h`/`object_handler_planner_impl.h` includes to 17 TUs.
= Fixed 8x C2027 in luabind bindings by including the defining
  `movement/patrol/detail/sight_manager_space.h` headers.
= Removed bogus `XRCORE_API` from the fully header-defined `_quaternion<T>`
  template (no explicit instantiation exists); sibling math types never had it.
= Fixed theora C4554/C4018, libjpeg LNK4006 (tool mains excluded from the lib),
  NVTT C4595/C4244, and the stray absolute `E:\STALKER\...` include path.
+ Added `src/xrCore/FS_impl_anchor.cpp`: no-PCH TU with the explicit
  instantiation of `IReaderBase<IReader>::find_chunk`, anchoring the symbol
  the PCH+`/GL` build otherwise discards (LNK2001).
+ Fixed 2x C5287 in LuaJIT `lj_ccallback.c` with explicit operand casts.
- Removed `export`-named methods (`light`, `NET_Queue`) clashing with the
  C++20 reserved keyword; removed hand-rolled `_PNH`/`_set_new_handler` decls.
~ Staging note: an x64 install needs the x64 `lua51.dll` from the LuaJIT build;
  a stale x86 copy breaks every render DLL load ("incompatible hardware").
