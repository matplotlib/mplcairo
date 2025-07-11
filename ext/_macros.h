#pragma once

#define TRUE_CHECK(func, ...) { \
  if (auto const& error_ = func(__VA_ARGS__); !error_) { \
    throw std::runtime_error{ \
      #func " (" __FILE__ " line " + std::to_string(__LINE__) + ") failed"}; \
  } \
} (void)0

#define THROW_ERROR(name, err) { \
  throw std::runtime_error{ \
    name " (" __FILE__ " line " + std::to_string(__LINE__) + ") failed " \
    "with error: " + std::string{err}}; \
} (void)0

#define CAIRO_CHECK(func, ...) { \
  if (auto const& status_ = func(__VA_ARGS__); status_ != CAIRO_STATUS_SUCCESS) { \
    THROW_ERROR(#func, cairo_status_to_string(status_)); \
  } \
} (void)0

#define CAIRO_CHECK_SET_USER_DATA(set_user_data, obj, key, user_data, destroy) { \
  if (auto const& status_ = set_user_data(obj, key, user_data, destroy); \
      status_ != CAIRO_STATUS_SUCCESS) { \
    [&](auto destroy_arg) { \
      if constexpr (!std::is_null_pointer_v<decltype(destroy_arg)>) { \
        destroy_arg(user_data); \
      } \
    }(destroy); \
    THROW_ERROR(#set_user_data, cairo_status_to_string(status_)); \
  } \
} (void)0

#define CAIRO_CHECK_SET_USER_DATA_NEW(set_user_data, obj, key, user_data) \
  CAIRO_CHECK_SET_USER_DATA( \
    set_user_data, obj, key, \
    new std::remove_reference_t<decltype(user_data)>(user_data), \
    [](void* ptr) { delete static_cast<decltype(user_data)*>(ptr); })

#define FT_CHECK(func, ...) { \
  if (auto const& error_ = func(__VA_ARGS__)) { \
    THROW_ERROR(#func, mplcairo::detail::ft_errors.at(error_)); \
  } \
} (void)0

// Technically could just be a function, but keeping things symmetric...
#define PY_CHECK(func, ...) \
  [&] { \
    auto const value_ = func(__VA_ARGS__); \
    if (PyErr_Occurred()) { \
      throw pybind11::error_already_set{}; \
    } else { \
      return value_; \
    } \
  }()
