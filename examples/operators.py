"""
mplcairo exposes all of cairo's `blending operators`_, but there is currently
no support to set them from Matplotlib's side.  Thus, it is necessary to write
custom artists to take advantage of this feature.

.. _blending operators: https://cairographics.org/operators/
"""

from matplotlib import pyplot as plt
from matplotlib.patches import Circle

from mplcairo import operator_t


class OpCircle(Circle):
    def __init__(self, *args, operator, **kwargs):
        super().__init__(*args, **kwargs)
        self._operator = operator

    def draw(self, renderer):
        gc = renderer.new_gc()
        gc.set_mplcairo_operator(self._operator)
        super().draw(renderer)
        gc.restore()


ops = [operator_t.OVER,
       operator_t.LIGHTEN,
       operator_t.OVERLAY,
       operator_t.SOFT_LIGHT]
fig, axs = plt.subplots(1, len(ops))
for op, ax in zip(ops, axs):
    ax.set_axis_off()
    ax.set_title(str(op).split(".")[-1])
    ax.set(xlim=(-1, 2), ylim=(-1, 1), aspect="equal")
    ax.add_artist(Circle((0, 0), 1, color="tab:blue"))
    ax.add_artist(OpCircle((1, 0), 1, color="tab:orange", operator=op))
plt.show()
