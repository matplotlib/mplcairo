# Modified from markevery_demo.py.

from matplotlib import pyplot as plt
import numpy as np

delta = 0.11
x = np.linspace(0, 10, 200) + delta
y = np.sin(x) + 1 + delta

ax = plt.figure(figsize=(3, 3)).subplots()
ax.set_xscale('log')
ax.set_yscale('log')
ax.plot(x, y, 'o', ls='-', ms=4)

plt.show()
