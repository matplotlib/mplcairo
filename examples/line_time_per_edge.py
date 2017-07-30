"""Profile line drawing across various conditions.
"""

from argparse import (
    ArgumentParser, ArgumentDefaultsHelpFormatter, RawTextHelpFormatter)
import pprint
import sys
import time

from matplotlib import backends, pyplot as plt
import numpy as np


def get_times(ax, n_edges):
    all_times = []

    for n in n_edges:
        print("{} edges".format(n))
        ax.set(xticks=[], yticks=[])
        for spine in ax.spines.values():
            plt.setp(spine, visible=False)
        data = np.random.RandomState(0).random_sample((2, n + 1))
        ax.plot(*data, solid_joinstyle="miter")

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

$ python %(prog)s \\
    '{{"backend": "agg"}}' \\
    '{{"backend": "agg", "agg.path.chunksize": 1000}}' \\
    '{{"backend": "module://mpl_cairo.base", \\
      "lines.antialiased": __import__("mpl_cairo").antialias_t.FAST}}' \\
    '{{"backend": "module://mpl_cairo.base", \\
      "lines.antialiased": __import__("mpl_cairo").antialias_t.BEST}}'
""".format(sys.argv[0]))

    parser.add_argument(
        "-n", "--n-edges", type=lambda s: [int(n) for n in s.split(",")],
        default=[10, 30, 100, 300, 1000, 3000, 10000],
        help="comma-separated list of number of edges")
    parser.add_argument(
        "rcs", type=eval, nargs="+", metavar="rc",
        help="rc parameters to test (will be eval'd)")
    args = parser.parse_args()

    n_edges = args.n_edges
    rcs = args.rcs
    results = []

    for rc in rcs:
        # Emulate rc_context, but without the validation (to support
        # mpl_cairo's antialiasing enum).
        try:
            orig_rc = dict.copy(plt.rcParams)
            dict.update(plt.rcParams, rc)
            fig, ax = plt.subplots()
            backend_mod, *_ = backends.pylab_setup(plt.rcParams["backend"])
            ax.figure.canvas = backend_mod.FigureCanvas(ax.figure)
            results.append(get_times(ax, n_edges))
            plt.close(fig)
        finally:
            dict.update(plt.rcParams, orig_rc)

    _, main_ax = plt.subplots()
    main_ax.set(xlabel="number of edges", ylabel="time per edge (s)",
                xscale="log", yscale="log")

    for i, (rc, all_times) in enumerate(zip(rcs, results)):

        # Use the minimum time as aggregate.
        main_ax.plot(
            n_edges,
            [np.min(times) / n for n, times in zip(n_edges, all_times)],
            "o", label=pprint.pformat(rc))

        fig, detail_axs = plt.subplots(
            len(n_edges), squeeze=False, sharex=True)
        detail_axs = detail_axs.ravel()
        fig.suptitle(pprint.pformat(rc))
        min_time = np.min(np.concatenate(all_times))
        max_time = np.max(np.concatenate(all_times))
        bins = np.geomspace(.99 * min_time,  1.01 * max_time)
        for ax, n, times in zip(detail_axs, n_edges, all_times):
            ax.hist(times, bins, normed=True, color="C{}".format(i))
            ax.set(xlabel="$t$ (s)", xscale="log")
            ax.text(.95, .95, "{} edges (N={})".format(n, len(times)),
                    ha="right", va="top", transform=ax.transAxes)
            ax.label_outer()

    main_ax.legend(loc="upper center")

    plt.show()


if __name__ == "__main__":
    main()
