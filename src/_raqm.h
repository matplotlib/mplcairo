#pragma once
#if !__has_include(<raqm.h>)
  #error "Please download raqm.h and place it in the include/ directory.  " \
    "You may want to use tools/download_raqm_header.py."
#else

extern "C" {  // Support raqm<=0.2.
  #include <raqm.h>
}

#include <optional>

#define ITER_RAQM_API(_) \
  _(create) \
  _(destroy) \
  _(get_glyphs) \
  _(layout) \
  _(set_freetype_face) \
  _(set_text_utf8)

namespace mplcairo {

void load_raqm();
void unload_raqm();
bool has_raqm();

namespace raqm {

#define DECLARE_API(name) extern decltype(raqm_##name)* name;
ITER_RAQM_API(DECLARE_API)
#undef DECLARE_API

}

}

#endif
