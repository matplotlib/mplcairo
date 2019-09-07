# Modified from quadmesh_demo and test_axes.test_pcolormesh.

from matplotlib import pyplot as plt
from matplotlib.colors import ListedColormap
import numpy as np

n = 12
x = np.linspace(-1.5, 1.5, n)
y = np.linspace(-1.5, 1.5, n*2)
X, Y = np.meshgrid(x, y)
Qx = np.cos(Y) - np.cos(X)
Qz = np.sin(Y) + np.sin(X)
Qx = (Qx + 1.1)
Z = np.sqrt(X**2 + Y**2)/5
Z = (Z - Z.min()) / (Z.max() - Z.min())

# The color array can include masked values:
Zm = np.ma.masked_where(np.abs(Qz) < 0.5 * np.max(Qz), Z)

# Set up the colormaps
cmap = plt.get_cmap('viridis')
cmap_data = cmap(np.arange(cmap.N))
# Set a linear ramp in alpha
cmap_data[:,-1] = np.linspace(0.2, 0.8, cmap.N)
# Create new colormap
cmap_alpha = ListedColormap(cmap_data)

fig, axs = plt.subplots(2, 2, constrained_layout=True)
axs[0, 0].pcolormesh(Qx, Qz, Z, cmap=cmap_alpha, shading='gouraud')
axs[0, 1].pcolormesh(Qx, Qz, Zm, cmap=cmap_alpha, shading='gouraud')
axs[1, 0].pcolormesh(Qx, Qz, Z, cmap=cmap_alpha, edgecolors='k')
mesh = axs[1, 1].pcolormesh(Qx, Qz, Zm, cmap=cmap_alpha)
fig.colorbar(mesh, ax=axs[:, 1], orientation='vertical')

for ax in axs.flat:
    ax.set_axis_off()

plt.show()
