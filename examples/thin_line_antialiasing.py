"""
Plot a series of lines of various thicknesses to test antialiasing.

On the mplcairo backend, the bottom halves of the lines are drawn with FAST
antialiasing and the top with BEST antialiasing.  On other backends, the bottom
halves are drawn with no antialiasing and the top ones with antialiasing.

The goal is to determine a suitable threshold for switching from FAST to BEST
in the mplcairo backend.

Note that antialiasing quality also depends on the angle of the lines, so one
should also resize the window to change the aspect ratio of the axes.
"""


from matplotlib import pyplot as plt
import numpy as np

if plt.rcParams["backend"].startswith("module://mplcairo"):
    from mplcairo import antialias_t as aa
    FAST = aa.FAST
    BEST = aa.BEST
else:
    FAST = False
    BEST = True

dpi = plt.rcParams["figure.dpi"]
fig, ax = plt.subplots()
plt.setp(ax.spines.values(), visible=False)
ax.set(xticks=[], yticks=[])
for i, px in enumerate(np.arange(.05, .5, .05)):
    pt = px * dpi / 72
    ax.plot(.1 * i + np.array([0, 1]), [0, 1], lw=pt, c="k", aa=FAST)
    ax.plot(.1 * i + np.array([1, 2]), [1, 2], lw=pt, c="k", aa=BEST)
    ax.text(.1 * i, 0, f"{px:.2f}px",
            ha="center", va="top", rotation=90)
    ax.text(.1 * i + 2, 2, f"{px:.2f}px",
            ha="center", va="bottom", rotation=90)
ax.axhline(1, c="k")

plt.show()
