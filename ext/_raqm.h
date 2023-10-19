#pragma once

extern "C" {  // Support raqm<=0.2.
  #include <raqm.h>
}

#include <optional>

#define ITER_RAQM_API(_) \
  _(add_font_feature) \
  _(create) \
  _(destroy) \
  _(get_glyphs) \
  _(layout) \
  _(set_freetype_face) \
  _(set_language) \
  _(set_text_utf8) \
  _(version_string) \
  _(version_atleast)

namespace mplcairo {

void load_raqm();
void unload_raqm();
bool has_raqm();

namespace raqm {

#define DECLARE_API(name) extern decltype(raqm_##name)* name;
ITER_RAQM_API(DECLARE_API)
#undef DECLARE_API

bool bad_color_glyph_spacing;

}

namespace hb {

extern char const* (*version_string)();

}

}
