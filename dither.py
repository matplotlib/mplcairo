"""
Similarly to support for blending operators, mplcairo also exposes support for
dithering_ control.

The example below uses 1-bit rendering (A1, monochrome) to demonstrate the
effect of the dithering algorithms.  Note that A1 buffers are currently only
supported with the ``mplcairo.qt`` backend.

.. _dithering: https://www.cairographics.org/manual/cairo-cairo-pattern-t.html#cairo-dither-t
"""

import matplotlib as mpl
from matplotlib import pyplot as plt
import numpy as np

import mplcairo
from mplcairo import dither_t


mplcairo.set_options(image_format="A1")

rgba = mpl.image.imread(mpl.cbook.get_sample_data("grace_hopper.jpg"))
alpha = rgba[:, :, :3].mean(-1).round().astype("u1")  # To transparency mask.
rgba = np.dstack([np.full(alpha.shape + (3,), 0xff), alpha])

# Figure and axes are made transparent, otherwise their patches would cover
# everything else.
axs = (plt.figure(figsize=(12, 4), facecolor="none")
       .subplots(1, len(dither_t),
                 subplot_kw=dict(xticks=[], yticks=[], facecolor="none")))
for dither, ax in zip(dither_t, axs):
    im = ax.imshow(rgba)
    ax.set(title=dither.name)
    dither.patch_artist(im)
plt.show()
