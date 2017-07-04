def pytest_make_parametrize_id(config, val, argname):
    return "{}={}".format(argname, getattr(val, "__name__", val))
