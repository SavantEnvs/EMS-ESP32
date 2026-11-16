// glibc >= 2.38 declares strlcpy()/strlcat() (noexcept) in <string.h>, which clashes with the
// differently-specified definitions in lib_standalone/WString.cpp. Force-included (C++ only) so the
// glibc declarations are seen under another name and lib_standalone's own functions stay the ones used.
#pragma once
#define strlcpy glibc_strlcpy
#define strlcat glibc_strlcat
#include <string.h>
#undef strlcpy
#undef strlcat
