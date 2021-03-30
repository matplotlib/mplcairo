There is a significant performance regression on the wire3d_animation example.

Crashes
=======

::

   gca(); savefig("/tmp/test.cairoscript")

Seems to be due to cairo trying to call ``write`` during shutdown when the
interpreter state is seriously messed up (even though ``_finish`` has been
correctly called first).  See `cairo issue #277 <cairo-277_>`_.

.. _cairo-277: https://gitlab.freedesktop.org/cairo/cairo/issues/277

Fix needed
==========

test_image
   test_figimage[pdf], test_figimage0[pdf], test_figimage1[pdf], test_interp_nearest_vs_none[pdf,svg], test_rasterize_dpi[pdf,svg]
      Invalid dpi manipulations in vector output.

   test_jpeg_alpha
      Not respecting savefig.facecolor.

We appear to be leaking memory per memleak.py.

Upstream issues
===============

The main list of xfailed tests is in ``run-mpl-test-suite.py``.  Other
"interesting" tests are listed below.

Issues with Matplotlib
----------------------

Matplotlib's hatching is inconsistent across backends (#10034). ::

   test_artist::test_clipping

Matplotlib's SVG backend does not implement Gouraud shading. ::

   test_axes::test_pcolormesh[svg]

Matplotlib's partially transparent markers with edges are inconsistent across
backends (#10035). ::

   test_axes::test_rgba_markers[pdf]

Matplotlib does not write SVGs with ``image-rendering: pixelated`` (#10112). ::

   test_backend_svg::test_noscale[svg]
   test_image::test_rotate_image[svg]
   test_tightlayout::test_tight_layout5[svg]

Matplotlib's software alpha compositor is incorrect (#8847). ::

   test_image::test_image_composite_alpha[pdf,svg]

Matplotlib's draw_path_collection has inconsistent semantics across backends
(#12021).  (No test.)

Issues with Ghostscript
-----------------------

Ghostscript's grayscale conversion is inaccurate in certain cases
(https://bugs.ghostscript.com/show_bug.cgi?id=698828). ::

   test_axes::test_mixed_collection[pdf]
   test_backend_pdf::test_grayscale_alpha
   test_backend_svg::test_noscale[pdf]
   test_mplot3d::test_bar3d,test_contour3d,etc.[pdf]
   test_offsetbox::test_offsetbox_clipping[pdf]

Ghostscript misrenders consecutive dashed lines (Matplotlib #10036). ::

   test_lines::test_lw_scaling[pdf]

Issues with Inkscape
--------------------

Matplotlib's SVG backend writes fills as hex (``#bf8040``) whereas cairo writes
them using ``rgb`` (``rgb(75%,50%,25%)``).  The latter seems rendered less
precisely when combined with transparency. ::

   test_patches::test_clip_to_bbox[svg]
   test_skew::test_skew_rectangle[svg]

Other relevant Matplotlib issues
================================

#9963 (behavior with fontsize < 1pt)
