#include "_pattern_cache.h"

#include "_mplcairo.h"

#include "_macros.h"

namespace mplcairo {

dash_t convert_dash(cairo_t* cr)
{
  auto const& dash_count = cairo_get_dash_count(cr);
  auto const& dashes = std::unique_ptr<double[]>{new double[dash_count]};
  double offset;
  cairo_get_dash(cr, dashes.get(), &offset);
  return {
    offset,
    std::string{
      reinterpret_cast<char*>(dashes.get()),
      dash_count * sizeof(dashes[0])}};
}

void set_dashes(cairo_t* cr, dash_t dash)
{
  auto& [offset, buf] = dash;
  cairo_set_dash(
    cr,
    reinterpret_cast<double*>(buf.data()),
    buf.size() / sizeof(double),
    offset);
}

void PatternCache::CacheKey::draw(
  cairo_t* cr, double x, double y, rgba_t color)
{
  auto const& m = cairo_matrix_t{
    matrix.xx, matrix.yx,
    matrix.xy, matrix.yy,
    matrix.x0 + x, matrix.y0 + y};
  switch (draw_func) {
    case draw_func_t::Fill:
      fill_and_stroke_exact(cr, path, &m, color, {});
      break;
    case draw_func_t::Stroke:
      cairo_save(cr);
      cairo_set_line_width(cr, linewidth);
      cairo_set_miter_limit(
        cr, detail::MITER_LIMIT >= 0 ? detail::MITER_LIMIT : linewidth);
      set_dashes(cr, dash);
      cairo_set_line_cap(cr, capstyle);
      cairo_set_line_join(cr, joinstyle);
      fill_and_stroke_exact(cr, path, &m, {}, color);
      cairo_restore(cr);
      break;
  }
}

size_t PatternCache::Hash::operator()(py::handle const& path) const
{
  return std::hash<void*>{}(path.ptr());
}

size_t PatternCache::Hash::operator()(CacheKey const& key) const
{
  // std::tuple is not hashable by default.  Reuse boost::hash_combine.
  size_t hashes[] = {
    std::hash<void*>{}(key.path.ptr()),
    std::hash<double>{}(key.matrix.xx), std::hash<double>{}(key.matrix.xy),
    std::hash<double>{}(key.matrix.yx), std::hash<double>{}(key.matrix.yy),
    std::hash<double>{}(key.matrix.x0), std::hash<double>{}(key.matrix.y0),
    std::hash<draw_func_t>{}(key.draw_func),
    std::hash<double>{}(key.linewidth),
    std::hash<double>{}(std::get<0>(key.dash)),
    std::hash<std::string>{}(std::get<1>(key.dash)),
    std::hash<cairo_line_cap_t>{}(key.capstyle),
    std::hash<cairo_line_join_t>{}(key.joinstyle)};
  auto seed = size_t{0};
  for (size_t i = 0; i < sizeof(hashes) / sizeof(hashes[0]); ++i) {
    seed ^= hashes[i] + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }
  return seed;
}

bool PatternCache::EqualTo::operator()(
  CacheKey const& lhs, CacheKey const& rhs) const
{
  return
    lhs.path.is(rhs.path)
    && lhs.matrix.xx == rhs.matrix.xx && lhs.matrix.xy == rhs.matrix.xy
    && lhs.matrix.yx == rhs.matrix.yx && lhs.matrix.yy == rhs.matrix.yy
    && lhs.matrix.x0 == rhs.matrix.x0 && lhs.matrix.y0 == rhs.matrix.y0
    && lhs.draw_func == rhs.draw_func
    && lhs.linewidth == rhs.linewidth && lhs.dash == rhs.dash
    && lhs.capstyle == rhs.capstyle && lhs.joinstyle == rhs.joinstyle;
}

PatternCache::PatternCache(double threshold) : threshold_{threshold}
{
  if (threshold >= 1. / 16) {  // NOTE: Arbitrary limit.
    n_subpix_ = std::ceil(1 / threshold);
  } else {
    n_subpix_ = 0;
  }
}

PatternCache::~PatternCache()
{
  for (auto const& [key, entry]: patterns_) {
    (void)key;
    for (size_t i = 0; i < n_subpix_ * n_subpix_; ++i) {
      cairo_pattern_destroy(entry.patterns[i]);
    }
  }
}

void PatternCache::mask(
  cairo_t* cr,
  py::handle path,
  cairo_matrix_t matrix,
  draw_func_t draw_func,
  double linewidth,
  dash_t dash,
  double x, double y)
{
  // The matrix gets cached, so we may as well take it by value instead of by
  // pointer.
  auto key =
    draw_func == draw_func_t::Fill
    ? CacheKey{
      path, matrix, draw_func, 0, {},
      static_cast<cairo_line_cap_t>(-1), static_cast<cairo_line_join_t>(-1)}
    : CacheKey{
      path, matrix, draw_func, linewidth, dash,
      cairo_get_line_cap(cr), cairo_get_line_join(cr)};
  auto const& draw_direct = [&] {
    double r, g, b, a;
    CAIRO_CHECK(cairo_pattern_get_rgba, cairo_get_source(cr), &r, &g, &b, &a);
    key.draw(cr, x, y, {r, g, b, a});
  };
  if (!n_subpix_) {
    draw_direct();
    return;
  }
  // Get the untransformed path bbox with cairo_path_extents(), so that we
  // know how to quantize the transformation matrix.  Note that this ignores
  // the additional size from linewidths, including miters (they will only
  // contribute a constant offset).
  // Importantly, cairo_*_extents() ignores surface dimensions and clipping.
  auto it_bboxes = bboxes_.find(key.path);
  if (it_bboxes == bboxes_.end()) {
    auto const& id = cairo_matrix_t{1, 0, 0, 1, 0, 0};
    load_path_exact(cr, key.path, &id);
    double x0, y0, x1, y1;
    cairo_path_extents(cr, &x0, &y0, &x1, &y1);
    bool ok;
    std::tie(it_bboxes, ok) =
      bboxes_.emplace(key.path, cairo_rectangle_t{x0, y0, x1 - x0, y1 - y0});
    if (!ok) {
      throw std::runtime_error{"unexpected insertion failure into cache"};
    }
  }
  // Approximate ("quantize") the transform matrix, so that the transformed
  // path is within 3x(threshold/3) of the path transformed by the original
  // matrix.  1x threshold will be added by the patterns_ cache.
  // If the entire object is within the threshold of the origin in either
  // direction, then draw it directly, as doing otherwise would be highly
  // inaccurate (see e.g. :mpltest:`test_mplot3d.test_quiver3d`).
  auto const& bbox = it_bboxes->second;
  // Binding by reference results in dangling reference.
  auto const x_max = std::max(std::abs(bbox.x), std::abs(bbox.x + bbox.width)),
             y_max = std::max(std::abs(bbox.y), std::abs(bbox.y + bbox.height));
  if (x_max < threshold_ || y_max < threshold_) {
    double r, g, b, a;
    CAIRO_CHECK(cairo_pattern_get_rgba, cairo_get_source(cr), &r, &g, &b, &a);
    key.draw(cr, x, y, {r, g, b, a});
    return;
  }
  auto const& eps = threshold_ / 3,
            & x_q = eps / x_max, y_q = eps / y_max,
            & xx_q = std::round(key.matrix.xx / x_q) * x_q,
            & yx_q = std::round(key.matrix.yx / x_q) * x_q,
            & xy_q = std::round(key.matrix.xy / y_q) * y_q,
            & yy_q = std::round(key.matrix.yy / y_q) * y_q,
            & x0_q = std::round(key.matrix.x0 / eps) * eps,
            & y0_q = std::round(key.matrix.y0 / eps) * eps;
  key.matrix = {xx_q, yx_q, xy_q, yy_q, x0_q, y0_q};
  // Get the patterns.
  auto it_patterns = patterns_.find(key);
  if (it_patterns == patterns_.end()) {
    // Get the pattern extents.
    load_path_exact(cr, key.path, &key.matrix);
    double x0, y0, x1, y1;
    switch (key.draw_func) {
      case draw_func_t::Fill:
        cairo_fill_extents(cr, &x0, &y0, &x1, &y1);
        break;
      case draw_func_t::Stroke:
        cairo_save(cr);
        cairo_set_line_width(cr, key.linewidth);
        cairo_set_miter_limit(
          cr, detail::MITER_LIMIT >= 0 ? detail::MITER_LIMIT : linewidth);
        set_dashes(cr, key.dash);
        cairo_stroke_extents(cr, &x0, &y0, &x1, &y1);
        cairo_restore(cr);
        break;
    }
    // If the pattern is huge, caching it can blow up the memory.
    if (x1 - x0 > get_additional_state(cr).width
        || y1 - y0 > get_additional_state(cr).height) {
      draw_direct();
      return;
    }
    auto patterns = std::unique_ptr<cairo_pattern_t*[]>{
      new cairo_pattern_t*[n_subpix_ * n_subpix_]()};  // () for nullptr-init!
    bool ok;
    std::tie(it_patterns, ok) =
      patterns_.emplace(
        key, PatternEntry{x0, y0, x1 - x0, y1 - y0, std::move(patterns)});
    if (!ok) {
      throw std::runtime_error{"unexpected insertion failure into cache"};
    }
  }
  auto const& entry = it_patterns->second;
  auto const& target_x = x + entry.x,
            & target_y = y + entry.y;
  auto const& i_target_x = std::floor(target_x),
            & i_target_y = std::floor(target_y);
  auto const& f_target_x = target_x - i_target_x,
            & f_target_y = target_y - i_target_y;
  auto const& i = int(n_subpix_ * f_target_x),
            & j = int(n_subpix_ * f_target_y);
  auto const& idx = i * n_subpix_ + j;
  auto& pattern = entry.patterns[idx];
  if (!pattern) {
    auto const& width = std::ceil(entry.width + 1),
              & height = std::ceil(entry.height + 1);
    auto const& raster_surface =
      cairo_image_surface_create(CAIRO_FORMAT_A8, width, height);
    auto const& raster_gcr =
      GraphicsContextRenderer::make_pattern_gcr(raster_surface);
    key.draw(
      raster_gcr.cr_,
      -entry.x + double(i) / n_subpix_, -entry.y + double(j) / n_subpix_);
    pattern = cairo_pattern_create_for_surface(raster_surface);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
  }
  // Draw using the pattern.
  auto const& pattern_matrix =
    cairo_matrix_t{1, 0, 0, 1, -i_target_x, -i_target_y};
  cairo_pattern_set_matrix(pattern, &pattern_matrix);
  cairo_mask(cr, pattern);
}

}
