#define XSTR(s) STR(s)
#define STR(s) #s

#define CAIRO_CHECK(func, ...) { \
  if (auto const& status_ = func(__VA_ARGS__); status_ != CAIRO_STATUS_SUCCESS) { \
    throw \
      std::runtime_error{ \
        #func " (" __FILE__ " line " + std::to_string(__LINE__) + ") failed " \
        "with error: " + std::string{cairo_status_to_string(status_)}}; \
  } \
} (void)0

#define CAIRO_CLEANUP_CHECK(cleanup, func, ...) { \
  if (auto const& status_ = func(__VA_ARGS__); status_ != CAIRO_STATUS_SUCCESS) { \
    cleanup \
    throw \
      std::runtime_error{ \
        #func " (" __FILE__ " line " + std::to_string(__LINE__) + ") failed " \
        "with error: " + std::string{cairo_status_to_string(status_)}}; \
  } \
} (void)0

#define FT_CHECK(func, ...) { \
  if (auto const& error_ = func(__VA_ARGS__)) { \
    throw \
      std::runtime_error{ \
        #func " (" __FILE__ " line " + std::to_string(__LINE__) + ") failed " \
        "with error: " + mplcairo::detail::ft_errors.at(error_)}; \
  } \
} (void)0
