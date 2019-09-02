#define XSTR(s) STR(s)
#define STR(s) #s

#define TRUE_CHECK(func, ...) { \
  if (auto const& error_ = func(__VA_ARGS__); !error_) { \
    throw \
      std::runtime_error{ \
        #func " (" __FILE__ " line " + std::to_string(__LINE__) + ") failed "}; \
  } \
} (void)0

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

// Technically could just be a function, but keeping things symmetric...
#define PY_CHECK(func, ...) \
  [&]() { \
    auto const value_ = func(__VA_ARGS__); \
    if (PyErr_Occurred()) { \
      throw pybind11::error_already_set{}; \
    } else { \
      return value_; \
    } \
  }()

// Extension for pybind11: Pythonic macros.

// a1 includes the opening brace and a2 the closing brace.
#define P11X_ENUM_TYPE(a1, a2, ...) decltype(std::pair a1, a2)::second_type

#define P11X_DECLARE_ENUM(py_name, holder, ...) \
  static_assert(std::is_enum_v<P11X_ENUM_TYPE(__VA_ARGS__)>, "Not an enum"); \
  namespace { \
    auto holder = \
      std::make_tuple( \
        py_name, \
        std::vector<std::pair<std::string, P11X_ENUM_TYPE(__VA_ARGS__)>>{__VA_ARGS__}, \
        pybind11::none{}); \
  } \
  namespace pybind11::detail { \
    template<> struct type_caster<P11X_ENUM_TYPE(__VA_ARGS__)> { \
      PYBIND11_TYPE_CASTER(P11X_ENUM_TYPE(__VA_ARGS__), _(py_name)); \
      bool load(handle src, bool) { \
        PyObject* tmp = nullptr; \
        if (pybind11::isinstance(src, std::get<2>(holder)) \
            && (tmp = PyNumber_Index(src.attr("value").ptr()))) { \
          auto ival = PyLong_AsLong(tmp); \
          value = decltype(value)(ival); \
          Py_DECREF(tmp); \
          return !(ival == -1 && !PyErr_Occurred()); \
        } else { \
          return false; \
        } \
      } \
      static handle cast(decltype(value) obj, return_value_policy, handle) { \
        return std::get<2>(holder)(int(obj)).inc_ref(); \
      } \
    }; \
  }

#define P11X_BIND_ENUM(mod, holder, pyenum_class) { \
  auto tmp = std::vector<std::pair<std::string, int>>{}; \
  for (auto& [k, v]: std::get<1>(holder)) { tmp.emplace_back(k, int(v)); } \
  mod.attr(pybind11::cast(std::get<0>(holder))) = \
    std::get<2>(holder) = \
    pybind11::module::import("pydoc").attr("locate")(pyenum_class)( \
      std::get<0>(holder), tmp); \
}
