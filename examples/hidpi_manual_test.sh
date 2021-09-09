#!/bin/sh
(
    export GDK_SCALE=2 QT_SCALE_FACTOR=2
    MPLBACKEND=module://mplcairo.gtk python -c 'import gi; gi.require_version("Gtk", "3.0"); from pylab import *; figure("gtk3", figsize=(2, 2)).add_subplot(); show()'&
    MPLBACKEND=module://mplcairo.gtk python -c 'import gi; gi.require_version("Gtk", "4.0"); from pylab import *; figure("gtk4", figsize=(2, 2)).add_subplot(); show()'&
    MPLBACKEND=module://mplcairo.gtk_native python -c 'import gi; gi.require_version("Gtk", "3.0"); from pylab import *; figure("gtk3_native", figsize=(2, 2)).add_subplot(); show()'&
    MPLBACKEND=module://mplcairo.gtk_native python -c 'import gi; gi.require_version("Gtk", "4.0"); from pylab import *; figure("gtk4_native", figsize=(2, 2)).add_subplot(); show()'&
    MPLBACKEND=module://mplcairo.qt python -c 'from pylab import *; figure("qt", figsize=(2, 2)).add_subplot(); show()'&
    MPLBACKEND=module://mplcairo.wx python -c 'from pylab import *; figure("wx", figsize=(2, 2)).add_subplot(); show()'&
    MPLBACKEND=module://mplcairo.tk python -c 'from pylab import *; figure("tk", figsize=(2, 2)).add_subplot(); show()'&
)
