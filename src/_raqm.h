#pragma once

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
