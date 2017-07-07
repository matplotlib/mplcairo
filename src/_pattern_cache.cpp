#include "_pattern_cache.h"

namespace mpl_cairo {

dash_t convert_dash(cairo_t* cr) {
  auto dash_count = cairo_get_dash_count(cr);
  auto dashes = std::unique_ptr<double[]>(new double[dash_count]);
  double offset;
  cairo_get_dash(cr, dashes.get(), &offset);
  return {
    offset,
    std::string{
      reinterpret_cast<char*>(dashes.get()), dash_count * sizeof(dashes[0])}};
}

dash_t convert_dash(
    std::tuple<std::optional<double>, std::optional<py::object>> dash_spec) {
  auto [offset, dash_list] = dash_spec;
  if (dash_list) {
    if (!offset) {
      throw std::invalid_argument("Missing dash offset");
    }
    auto dashes = dash_list->cast<std::vector<double>>();
    return {
      *offset,
      std::string{
        reinterpret_cast<char*>(dashes.data()),
        dashes.size() * sizeof(dashes[0])}};
  } else {
    return {0, ""};
  }
}

void set_dashes(cairo_t* cr, dash_t dash) {
  auto [offset, buf] = dash;
  cairo_set_dash(
      cr,
      std::launder(reinterpret_cast<double*>(buf.data())),
      buf.size() / sizeof(double),
      offset);
}

void PatternCache::CacheKey::draw(cairo_t* cr) {
  load_path(cr, path, &matrix);
  switch (draw_func) {
    case draw_func_t::Fill:
      cairo_fill(cr);
      break;
    case draw_func_t::Stroke:
      cairo_set_line_width(cr, linewidth);
      set_dashes(cr, dash);
      cairo_stroke(cr);
      break;
  }
}

size_t PatternCache::Hash::operator()(py::object const& path) const {
  return std::hash<void*>{}(path.ptr());
}

size_t PatternCache::Hash::operator()(CacheKey const& key) const {
  // std::tuple is not hashable by default.  Reuse boost::hash_combine.
  size_t hashes[] = {
    std::hash<void*>{}(key.path.ptr()),
    std::hash<double>{}(key.matrix.xx), std::hash<double>{}(key.matrix.xy),
    std::hash<double>{}(key.matrix.yx), std::hash<double>{}(key.matrix.yy),
    std::hash<double>{}(key.matrix.x0), std::hash<double>{}(key.matrix.y0),
    std::hash<draw_func_t>{}(key.draw_func),
    std::hash<double>{}(key.linewidth),
    std::hash<double>{}(std::get<0>(key.dash)),
    std::hash<std::string>{}(std::get<1>(key.dash))};
  auto seed = size_t{0};
  for (size_t i = 0; i < sizeof(hashes) / sizeof(hashes[0]); ++i) {
    seed ^= hashes[i] + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }
  return seed;
}

bool PatternCache::EqualTo::operator()(
    CacheKey const& lhs, CacheKey const& rhs) const {
  return (lhs.path == rhs.path)
    && (lhs.matrix.xx == rhs.matrix.xx) && (lhs.matrix.xy == rhs.matrix.xy)
    && (lhs.matrix.yx == rhs.matrix.yx) && (lhs.matrix.yy == rhs.matrix.yy)
    && (lhs.matrix.x0 == rhs.matrix.x0) && (lhs.matrix.y0 == rhs.matrix.y0)
    && (lhs.draw_func == rhs.draw_func)
    && (lhs.linewidth == rhs.linewidth) && (lhs.dash == rhs.dash);
}

PatternCache::PatternCache(double threshold) :
  trivial_cr_{trivial_context()}, threshold_{threshold} {
  if (threshold >= 1. / 16) {  // NOTE: Arbitrary limit.
    n_subpix_ = std::ceil(1 / threshold);
  } else {
    n_subpix_ = 0;
  }
}

PatternCache::~PatternCache() {
  cairo_destroy(trivial_cr_);
  for (auto& [key, entry]: patterns_) {
    for (size_t i = 0; i < n_subpix_ * n_subpix_; ++i) {
      cairo_pattern_destroy(entry.patterns[i]);
    }
  }
}

void PatternCache::mask(cairo_t* cr, CacheKey key, double x, double y) {
  if (!n_subpix_) {
    cairo_save(cr);
    cairo_translate(cr, x, y);
    key.draw(cr);
    cairo_restore(cr);
    return;
  }
  // Get the untransformed path bbox with cairo_path_extents(), so that we
  // know how to quantize the transformation matrix.  Note that this ignores
  // the additional size from linewidths (they will not be scaled anyways).
  // Importantly, cairo_*_extents() ignores surface dimensions and clipping.
  auto it_bboxes = bboxes_.find(key.path);
  if (it_bboxes == bboxes_.end()) {
    auto id = cairo_matrix_t{1, 0, 0, 1, 0, 0};
    load_path(trivial_cr_, key.path, &id);
    double x0, y0, x1, y1;
    cairo_path_extents(trivial_cr_, &x0, &y0, &x1, &y1);
    bool ok;
    std::tie(it_bboxes, ok) =
      bboxes_.emplace(key.path, cairo_rectangle_t{x0, y0, x1 - x0, y1 - y0});
    if (!ok) {
      throw std::runtime_error("Unexpected insertion failure into cache");
    }
  }
  // Approximate ("quantize") the transform matrix, so that the transformed
  // path is within 3 x (thresholds/3) of the path transformed by the original
  // matrix.  1 x threshold will be added by the patterns_ cache.
  auto& bbox = it_bboxes->second;
  auto eps = threshold_ / 3;
  auto xstep = eps / std::max(std::abs(bbox.x), std::abs(bbox.x + bbox.width)),
       ystep = eps / std::max(std::abs(bbox.y), std::abs(bbox.y + bbox.height)),
       xx_q = std::round(key.matrix.xx / xstep) * xstep,
       yx_q = std::round(key.matrix.yx / xstep) * xstep,
       xy_q = std::round(key.matrix.xy / ystep) * ystep,
       yy_q = std::round(key.matrix.yy / ystep) * ystep,
       x0_q = std::round(key.matrix.x0 / eps) * eps,
       y0_q = std::round(key.matrix.y0 / eps) * eps;
  key.matrix = {xx_q, yx_q, xy_q, yy_q, x0_q, y0_q};
  // Get the patterns.
  auto it_patterns = patterns_.find(key);
  if (it_patterns == patterns_.end()) {
    // Get the pattern extents.
    load_path(trivial_cr_, key.path, &key.matrix);
    double x0, y0, x1, y1;
    switch (key.draw_func) {
      case draw_func_t::Fill:
        cairo_fill_extents(trivial_cr_, &x0, &y0, &x1, &y1);
        break;
      case draw_func_t::Stroke:
        cairo_set_line_width(trivial_cr_, key.linewidth);
        set_dashes(trivial_cr_, key.dash);
        cairo_stroke_extents(trivial_cr_, &x0, &y0, &x1, &y1);
        break;
    }
    // Must be nullptr-initialized.
    auto patterns =
      std::make_unique<cairo_pattern_t*[]>(n_subpix_ * n_subpix_);
    bool ok;
    std::tie(it_patterns, ok) =
      patterns_.emplace(
          key, PatternEntry{x0, y0, x1 - x0, y1 - y0, std::move(patterns)});
    if (!ok) {
      throw std::runtime_error("Unexpected insertion failure into cache");
    }
  }
  auto& entry = it_patterns->second;  // Reference, as we have a unique_ptr.
  auto target_x = x + entry.x, target_y = y + entry.y;
  auto i_target_x = std::floor(target_x), i_target_y = std::floor(target_y);
  auto f_target_x = target_x - i_target_x, f_target_y = target_y - i_target_y;
  auto i = int(n_subpix_ * f_target_x), j = int(n_subpix_ * f_target_y);
  auto idx = i * n_subpix_ + j;
  auto& pattern = entry.patterns[idx];
  if (!pattern) {
    auto raster_surface = cairo_image_surface_create(
        CAIRO_FORMAT_A8,
        std::ceil(entry.width + 1), std::ceil(entry.height + 1));
    auto raster_cr = cairo_create(raster_surface);
    cairo_translate(
        raster_cr,
        -entry.x + double(i) / n_subpix_, -entry.y + double(j) / n_subpix_);
    key.draw(raster_cr);
    pattern = cairo_pattern_create_for_surface(raster_surface);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
    cairo_destroy(raster_cr);
    cairo_surface_destroy(raster_surface);
  }
  // Draw using the pattern.
  auto pattern_matrix = cairo_matrix_t{1, 0, 0, 1, -i_target_x, -i_target_y};
  cairo_pattern_set_matrix(pattern, &pattern_matrix);
  cairo_mask(cr, pattern);
}

}
