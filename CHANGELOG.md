# Changelog (legend: `*` major / `~` minor / `=` bugfix / `-` removed / `+` added)

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
