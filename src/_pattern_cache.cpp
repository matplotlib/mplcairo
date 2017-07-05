#include "_pattern_cache.h"

#include "_util.h"

namespace mpl_cairo {

size_t PatternCache::Hash::operator()(CacheKey const& key) const {
  auto& [path, matrix, draw_func] = key;
  // std::tuple is not hashable by default.  Reuse boost::hash_combine.
  size_t hashes[] = {
    std::hash<void*>{}(path.ptr()),
    std::hash<double>{}(matrix.xx), std::hash<double>{}(matrix.xy),
    std::hash<double>{}(matrix.yx), std::hash<double>{}(matrix.yy),
    std::hash<double>{}(matrix.x0), std::hash<double>{}(matrix.y0),
    std::hash<void (*)(cairo_t*)>{}(draw_func)};
  auto seed = size_t{0};
  for (size_t i = 0; i < 7; ++i) {
    seed ^= hashes[i] + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }
  return seed;
}

bool PatternCache::EqualTo::operator()(CacheKey const& lhs, CacheKey const& rhs) const {
  auto& [path1, matrix1, draw_func1] = lhs;
  auto& [path2, matrix2, draw_func2] = rhs;
  return (path1 == path2)
    && (matrix1.xx == matrix2.xx) && (matrix1.xy == matrix2.xy)
    && (matrix1.yx == matrix2.yx) && (matrix1.yy == matrix2.yy)
    && (matrix1.x0 == matrix2.x0) && (matrix1.y0 == matrix2.y0)
    && (draw_func1 == draw_func2);
}

PatternCache::PatternCache(double threshold) {
  if (threshold >= 1. / 16) {  // NOTE: Arbitrary limit.
    n_subpix_ = std::ceil(1 / threshold);
  } else {
    n_subpix_ = 0;
  }
}

PatternCache::~PatternCache() {
  for (auto& [key, entry]: cache_) {
    for (size_t i = 0; i < n_subpix_ * n_subpix_; ++i) {
      cairo_pattern_destroy(entry.patterns[i]);
    }
  }
  cache_.clear();
}

void PatternCache::mask(cairo_t* cr, CacheKey key, double x, double y) {
  if (!n_subpix_) {
    cairo_save(cr);
    cairo_translate(cr, x, y);
    load_path(cr, key.path, &key.matrix);
    // TODO: Copy lw, dashes.
    key.draw_func(cr);
    cairo_restore(cr);
    return;
  }
  auto it = cache_.find(key);
  if (it == cache_.end()) {
    auto recording_surface =
      cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, nullptr);
    auto recording_cr = cairo_create(recording_surface);
    load_path(recording_cr, key.path, &key.matrix);
    // TODO: Copy lw, dashes.
    key.draw_func(recording_cr);
    double x0, y0, width, height;
    cairo_recording_surface_ink_extents(recording_surface, &x0, &y0, &width, &height);
    // Must be nullptr-initialized.
    auto patterns = std::make_unique<cairo_pattern_t*[]>(n_subpix_ * n_subpix_);
    bool ok;
    std::tie(it, ok) =
      cache_.emplace(key, CacheEntry{x0, y0, width, height, std::move(patterns)});
    if (!ok) {
      throw std::runtime_error("Unexpected insertion failure into cache");
    }
  }
  auto& entry = it->second;  // Reference, as we have a unique_ptr.
  auto target_x = x + entry.x0, target_y = y + entry.y0;
  auto i_target_x = std::floor(target_x), i_target_y = std::floor(target_y);
  auto f_target_x = target_x - i_target_x, f_target_y = target_y - i_target_y;
  auto i = int(n_subpix_ * f_target_x), j = int(n_subpix_ * f_target_y);
  auto idx = i * n_subpix_ + j;
  if (!entry.patterns[idx]) {
    auto raster_surface = cairo_image_surface_create(
        CAIRO_FORMAT_A8, std::ceil(entry.width + 1), std::ceil(entry.height + 1));
    auto raster_cr = cairo_create(raster_surface);
    cairo_translate(
        raster_cr, -entry.x0 + double(i) / n_subpix_, -entry.y0 + double(j) / n_subpix_);
    load_path(raster_cr, key.path, &key.matrix);
    // TODO: Copy lw, dashes.
    key.draw_func(raster_cr);
    auto pattern = cairo_pattern_create_for_surface(raster_surface);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
    entry.patterns[idx] = pattern;
    cairo_destroy(raster_cr);
    cairo_surface_destroy(raster_surface);
  }
  auto pattern_matrix = cairo_matrix_t{1, 0, 0, 1, -i_target_x, -i_target_y};
  cairo_pattern_set_matrix(entry.patterns[idx], &pattern_matrix);
  cairo_mask(cr, entry.patterns[idx]);
}

}
