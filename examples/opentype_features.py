from matplotlib import font_manager as fm, pyplot as plt


path = fm.findfont("DejaVu Sans")
fig = plt.figure()
fig.text(.2, .7, "style (default)",
         fontproperties=fm.FontProperties(fname=path))
fig.text(.2, .6, "style (discretionary ligature)",
         fontproperties=fm.FontProperties(fname=path + "|dlig"))
fig.text(.2, .5, "style (stylistic alternate)",
         fontproperties=fm.FontProperties(fname=path + "|aalt[:5]=1"))
fig.text(.2, .4, "style (discretionary ligature & stylistic alternate)",
         fontproperties=fm.FontProperties(fname=path + "|dlig[:5]=1,aalt[:5]=1"))
plt.show()
