# Changelog (legend: `*` major / `~` minor / `=` bugfix / `-` removed / `+` added)

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
