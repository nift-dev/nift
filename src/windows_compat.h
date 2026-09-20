#pragma once
// Contain windows.h macro damage. On MinGW with GCC 15/16, libstdc++ headers
// such as <locale> and <filesystem> break when windows.h macros are still
// defined (the gthread/atomicity bridge fails). Include this immediately
// after every <windows.h> include to undef the damaging macros.
#ifdef _WIN32
#undef interface
#undef near
#undef far
#undef small
#undef IN
#undef OUT
#undef PASCAL
#endif
