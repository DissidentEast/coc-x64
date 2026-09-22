// Purpose: hosts the explicit instantiation of
// IReaderBase<IReader>::find_chunk so the linker always finds an
// out-of-line copy.
//
// Background: the template is defined inline (IC) in FS_impl.h. Every
// translation unit that uses the precompiled header ends up referencing
// the instantiation without emitting it (MSVC LNK2001 under /GL+LTCG),
// so xrCore.dll fails to link. A TU compiled WITHOUT the PCH emits the
// symbol normally; this file is that TU (see xrCore.vcxproj:
// PrecompiledHeader=NotUsing for this file).

#include "xrCore.h"
#include "FS.h"
#include "FS_impl.h"

template u32 IReaderBase<IReader>::find_chunk(u32 ID, BOOL* bCompressed);
