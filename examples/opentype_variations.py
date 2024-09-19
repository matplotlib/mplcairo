from collections import namedtuple
from pathlib import Path
import string
from urllib.parse import quote
from urllib.request import urlretrieve
from zipfile import ZipFile

from matplotlib import font_manager as fm, gridspec, pyplot as plt
from matplotlib.widgets import Slider


VariableAxis = namedtuple("VariableAxis", "min max default name")

DEFAULT_FONT = "Roboto Flex"
DEFAULT_SIZE = 32
VARIABLE_AXES = {
    "Optical Size": VariableAxis(8, 144, DEFAULT_SIZE, "opsz"),
    "Weight": VariableAxis(100, 1000, 400, "wght"),
    "Width": VariableAxis(25, 151, 100, "wdth"),
    "Slant": VariableAxis(-10, 0, 0, "slnt"),
    "Ascender Height": VariableAxis(649, 854, 750, "YTAS"),
    "Counter Width": VariableAxis(323, 603, 468, "XTRA"),
    "Descender Depth": VariableAxis(-305, -98, -203, "YTDE"),
    "Figure Height": VariableAxis(560, 788, 738, "YTFI"),
    "Grade": VariableAxis(-200, 150, 0, "GRAD"),
    "Lowercase Height": VariableAxis(416, 570, 514, "YTLC"),
    "Thin Stroke": VariableAxis(25, 135, 79, "YOPQ"),
    "Uppercase Height": VariableAxis(528, 760, 712, "YTUC"),
}

path = Path(__file__).with_name(f"{DEFAULT_FONT}.ttf")
if not path.exists():
    url = ("https://github.com/googlefonts/roboto-flex/"
           "releases/download/3.200/roboto-flex-fonts.zip")
    member = (
        "roboto-flex-fonts/fonts/variable/"
        "RobotoFlex[GRAD,XOPQ,XTRA,YOPQ,YTAS,YTDE,YTFI,YTLC,YTUC,opsz,slnt,wdth,wght].ttf")
    print(f"Downloading {url} to {path}")
    tmpfile, _ = urlretrieve(url)
    with ZipFile(tmpfile) as zfd:
        path.write_bytes(zfd.read(member))


def generate_font(family, size, axes):
    args = ",".join(f"{VARIABLE_AXES[title].name}={value}"
                    for title, value in axes.items())
    font = Path(__file__).with_name(f"{family}.ttf||{args}")
    return fm.FontProperties(fname=font, size=size)


fig = plt.figure(figsize=(10, 3))
gs = gridspec.GridSpec(2, 1, figure=fig, height_ratios=[2, 1],
                       bottom=0.05, top=1, left=0.14, right=0.95)

default_font = generate_font(
    DEFAULT_FONT, DEFAULT_SIZE,
    {title: axis.default for title, axis in VARIABLE_AXES.items()})
text = fig.text(
    0.5, 2/3,
    f"{string.ascii_uppercase}\n{string.ascii_lowercase}\n{string.digits}",
    font=default_font,
    horizontalalignment="center", verticalalignment="center")

fig.text(0.5, 0.36, "Font Variations", horizontalalignment="center")
option_gs = gs[1].subgridspec(6, 2, wspace=0.6)

sliders = {
    title: Slider(fig.add_subplot(ss), title,
                  axis.min, axis.max, valstep=1, valinit=axis.default)
    for (title, axis), ss in zip(VARIABLE_AXES.items(), option_gs)
}


def update_font(value):
    fp = generate_font(
        DEFAULT_FONT, DEFAULT_SIZE,
        {title: slider.val for title, slider in sliders.items()})
    text.set_font(fp)


for slider in sliders.values():
    slider.on_changed(update_font)

plt.show()
