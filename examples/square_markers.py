# Modified from wire3d_animation.py.

from matplotlib._pylab_helpers import Gcf
import matplotlib.pyplot as plt
import numpy as np
import time
backend = plt.rcParams["backend"]
plt.rcdefaults()
plt.rcParams["backend"] = backend


def generate(X, Y, phi):
    R = 1 - np.hypot(X, Y)
    return np.cos(2 * np.pi * X + phi) * R


fig = plt.figure()
ax = fig.add_subplot(111)

# Make the X, Y meshgrid.
xs = np.linspace(-1, 1, 50)
ys = np.linspace(-1, 1, 50)
X, Y = np.meshgrid(xs, ys)

# Begin plotting.
tstart = time.process_time()
# Keep track of plotted frames, so that FPS estimate stays reasonable even
# after a ctrl-C's.
for i, phi in enumerate(np.linspace(0, 180. / np.pi, 100)):
    if not Gcf.get_num_fig_managers():
        break
    ax.lines.clear()
    Z = generate(X, Y, phi)
    ax.plot(X.flat, Z.flat, "sk")
    plt.pause(.001)

print("Average FPS: {}".format((i + 1) / (time.process_time() - tstart)))
