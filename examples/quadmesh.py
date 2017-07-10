# Combination of quadmesh_demo and test_axes.test_pcolormesh.

from matplotlib import pyplot as plt
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

fig, axs = plt.subplots(2, 2)
axs[0, 0].pcolormesh(Qx, Qz, Z, shading='gouraud')
axs[0, 1].pcolormesh(Qx, Qz, Zm, shading='gouraud')
axs[1, 0].pcolormesh(Qx, Qz, Z, lw=0.5, edgecolors='k')
axs[1, 1].pcolormesh(Qx, Qz, Z, lw=2, edgecolors=['b', 'w'])

plt.show()
