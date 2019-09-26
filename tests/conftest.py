from pathlib import Path


def pytest_make_parametrize_id(config, val, argname):
    if Path(config.rootdir) in Path(__file__).parents:
        return "{}={}".format(argname, getattr(val, "__name__", val))
    # Otherwise, we're running ./run-mpl-test-suite.py, so don't do anything.
