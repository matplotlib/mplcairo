#pragma once

#define TRUE_CHECK(func, ...) { \
  if (auto const& error_ = func(__VA_ARGS__); !error_) { \
    throw \
      std::runtime_error{ \
        #func " (" __FILE__ " line " + std::to_string(__LINE__) + ") failed"}; \
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

#define CAIRO_CHECK_SET_USER_DATA(set_user_data, obj, key, user_data, destroy) { \
  if (auto const& status_ = set_user_data(obj, key, user_data, destroy); \
      status_ != CAIRO_STATUS_SUCCESS) { \
    [&](auto destroy_arg) { \
      if constexpr (!std::is_null_pointer_v<decltype(destroy_arg)>) { \
        destroy_arg(user_data); \
      } \
    }(destroy); \
    throw \
      std::runtime_error{ \
        #set_user_data " (" __FILE__ " line " + std::to_string(__LINE__) + ") failed " \
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
  [&] { \
    auto const value_ = func(__VA_ARGS__); \
    if (PyErr_Occurred()) { \
      throw pybind11::error_already_set{}; \
    } else { \
      return value_; \
    } \
  }()

// Extension for pybind11: Pythonic enums.

// a1 includes the opening brace and a2 the closing brace.
// This definition is compatible with older compiler versions compared to
// #define P11X_ENUM_TYPE(...) decltype(std::map{std::pair __VA_ARGS__})::mapped_type
#define P11X_ENUM_TYPE(a1, a2, ...) decltype(std::pair a1, a2)::second_type

#define P11X_CAT2(a, b) a##b
#define P11X_CAT(a, b) P11X_CAT2(a, b)

namespace p11x {
  namespace {
    namespace py = pybind11;

    // Holder is (py_base_cls, [(name, value), ...]) before module init;
    // converted to the Python class object after init.
    auto enums = std::unordered_map<std::string, py::object>{};

    auto bind_enums(py::module mod) -> void
    {
      for (auto& [py_name, spec]: enums) {
        auto const& [py_base_cls, pairs] =
          spec.cast<std::pair<std::string, py::object>>();
        mod.attr(py::cast(py_name)) = spec =
          py::module::import("pydoc").attr("locate")(py_base_cls)(
            py_name, pairs, py::arg("module") = mod.attr("__name__"));
      }
    }
  }
}

// Immediately converting the args to a vector outside of the lambda avoids
// name collisions.
#define P11X_DECLARE_ENUM(py_name, py_base_cls, ...) \
  namespace p11x { \
    namespace { \
      [[maybe_unused]] auto const P11X_CAT(enum_placeholder_, __COUNTER__) = \
        [](auto args) { \
          py::gil_scoped_acquire gil; \
          using int_t = std::underlying_type_t<decltype(args[0].second)>; \
          auto pairs = std::vector<std::pair<std::string, int_t>>{}; \
          for (auto& [k, v]: args) { \
            pairs.emplace_back(k, int_t(v)); \
          } \
          p11x::enums[py_name] = pybind11::cast(std::pair{py_base_cls, pairs}); \
          return 0; \
        } (std::vector{std::pair __VA_ARGS__}); \
    } \
  } \
  namespace pybind11::detail { \
    template<> struct type_caster<P11X_ENUM_TYPE(__VA_ARGS__)> { \
      using type = P11X_ENUM_TYPE(__VA_ARGS__); \
      static_assert(std::is_enum_v<type>, "Not an enum"); \
      PYBIND11_TYPE_CASTER(type, _(py_name)); \
      bool load(handle src, bool) { \
        auto cls = p11x::enums.at(py_name); \
        PyObject* tmp = nullptr; \
        if (pybind11::isinstance(src, cls) \
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
        auto cls = p11x::enums.at(py_name); \
        return cls(std::underlying_type_t<type>(obj)).inc_ref(); \
      } \
    }; \
  }
