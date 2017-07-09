from matplotlib import pyplot as plt
backend = plt.rcParams["backend"]
plt.rcdefaults()
plt.rcParams["backend"] = backend
fig, ax = plt.subplots()

ax.text(.5, .5, "line")
plt.show()
