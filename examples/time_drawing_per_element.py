"""Profile drawing methods as a function of input size and rcparams."""

from argparse import (
    ArgumentParser, ArgumentDefaultsHelpFormatter, RawTextHelpFormatter)
import pprint
import sys
import time

import matplotlib as mpl
from matplotlib import pyplot as plt
import numpy as np


def get_times(ax, method, n_elems):
    all_times = []

    for n in n_elems:
        print(f"{n} elements")
        ax.set(xticks=[], yticks=[])
        for spine in ax.spines.values():
            plt.setp(spine, visible=False)
        data = np.random.RandomState(0).random_sample((2, n + 1))
        getattr(ax, method)(*data)

        def profile(func=ax.figure.canvas.draw,
                    max_time=1,
                    timer=time.perf_counter):
            times = []
            base_start = timer()
            while timer() - base_start < max_time:
                start = timer()
                func()
                elapsed = timer() - start
                times.append(elapsed)
            return times

        times = profile()
        all_times.append(times)

    ax.figure.clear()
    return all_times


def main():

    parser = ArgumentParser(
        formatter_class=type(
            "", (ArgumentDefaultsHelpFormatter, RawTextHelpFormatter), {}),
        epilog="""\
Example usage:

$ python %(prog)s plot \\
    '{{"backend": "agg"}}' \\
    '{{"backend": "agg", "agg.path.chunksize": 1000}}' \\
    '{{"backend": "module://mplcairo.base", \\
      "lines.antialiased": __import__("mplcairo").antialias_t.FAST}}' \\
    '{{"backend": "module://mplcairo.base", \\
      "lines.antialiased": __import__("mplcairo").antialias_t.BEST}}'
""")

    parser.add_argument(
        "-n", "--n-elements", type=lambda s: [int(n) for n in s.split(",")],
        default=[10, 30, 100, 300, 1000, 3000, 10000],
        help="comma-separated list of number of elements")
    parser.add_argument(
        "method", choices=["plot", "fill", "scatter"],
        help="Axes method")
    parser.add_argument(
        "rcs", type=eval, nargs="+", metavar="rc",
        help="rc parameters to test (will be eval'd)")
    args = parser.parse_args()
    n_elems = args.n_elements
    method = args.method
    rcs = args.rcs

    results = []
    for rc in rcs:
        # Emulate rc_context, but without the validation (to support mplcairo's
        # antialiasing enum).
        try:
            orig_rc = dict.copy(plt.rcParams)
            dict.update(plt.rcParams, rc)
            mpl.use(plt.rcParams["backend"])
            fig, ax = plt.subplots()
            results.append(get_times(ax, method, n_elems))
            plt.close(fig)
        finally:
            dict.update(plt.rcParams, orig_rc)

    _, main_ax = plt.subplots()
    main_ax.set(xlabel="number of elements", ylabel="time per edge (s)",
                xscale="log", yscale="log")

    for i, (rc, all_times) in enumerate(zip(rcs, results)):
        normalized = [
            np.array(times) / n for n, times in zip(n_elems, all_times)]
        for norm in normalized:
            norm.sort()

        # Use the minimum time as aggregate: the cdfs show that the
        # distributions are very long tailed.
        main_ax.plot(n_elems, [norm[0] for norm in normalized],
                     "o", label=pprint.pformat(rc))

        fig, detail_ax = plt.subplots()
        fig.suptitle(pprint.pformat(rc))
        for n, norm in zip(n_elems, normalized):
            detail_ax.plot(norm, np.linspace(0, 1, len(norm))[::-1],
                           drawstyle="steps-pre",
                           label=f"{n} elements (N={len(norm)})")
        detail_ax.set(xlabel="time per element (s)", xscale="log",
                      ylabel="CCDF")
        detail_ax.legend(loc="upper right")

    main_ax.legend(loc="upper center")

    plt.show()


if __name__ == "__main__":
    main()
