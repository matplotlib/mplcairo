"""
mplcairo exposes all of cairo's `blending operators`_, but there is currently
no support to set them from Matplotlib's side.  Thus, it is necessary to write
custom artists to take advantage of this feature.

One can either directly create artists of the given class (the ``OpCircle``
example below), or, more simply, use the `operator_t.patch_artist` method,
which just patches the ``draw`` method of the artist (the ``patch_artist``
example below).

Note that for many operators other than the default OVER, it is necessary to
set the figure's background to fully transparent (alpha=0) to avoid compositing
against it.

.. _blending operators: https://cairographics.org/operators/
"""

import math

from matplotlib import pyplot as plt
from matplotlib.patches import Circle
import numpy as np

from mplcairo import operator_t


ops = [*operator_t.__members__.values()]


# Manually create an Artist subclass.

class OpCircle(Circle):
    def __init__(self, *args, operator, **kwargs):
        super().__init__(*args, **kwargs)
        self._operator = operator

    def draw(self, renderer):
        gc = renderer.new_gc()
        gc.set_mplcairo_operator(self._operator)
        super().draw(renderer)
        gc.restore()


fig = plt.figure(figsize=(8, 4))
axs = fig.subplots(math.ceil(len(ops)**(1/2)), math.ceil(len(ops)**(1/2)))
# The figure patch should be set to fully transparent to avoid compositing the
# patches against it.
fig.patch.set(alpha=0)
for ax in axs.flat:
    ax.set_axis_off()
for op, ax in zip(ops, axs.flat):
    ax.set_title(str(op).split(".")[-1])
    ax.set(xlim=(-1.5, 2.5), ylim=(-1.5, 1.5), aspect="equal")
    ax.add_artist(
        Circle((0, 0), 1, color="tab:blue", alpha=.5))
    ax.add_artist(
        OpCircle((1, 0), 1, color="tab:orange", alpha=.5, operator=op))
fig.tight_layout()


# Rely on `operator_t.patch_artist`.

fig = plt.figure(figsize=(8, 4))
axs = fig.subplots(math.ceil(len(ops)**(1/2)), math.ceil(len(ops)**(1/2)))
# The figure patch should be set to fully transparent to avoid compositing the
# patches against it.
fig.patch.set(alpha=0)
for ax in axs.flat:
    ax.set_axis_off()
for op, ax in zip(ops, axs.flat):
    ax.set_title(str(op).split(".")[-1])
    ax.set(xlim=(0, 5), ylim=(0, 5), aspect="equal")
    im1 = ax.imshow(
        np.arange(4).reshape((2, 2)), alpha=.5, extent=(0, 4, 0, 4))
    im2 = ax.imshow(
        np.arange(4).reshape((2, 2)), alpha=.5, extent=(1, 5, 1, 5))
    op.patch_artist(im2)
fig.tight_layout()


plt.show()
