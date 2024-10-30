There is a significant performance regression on the wire3d_animation example.

Fix needed
==========

test_backend_ps
   test_savefig_to_stringio[ps with distiller=xpdf-landscape-letter]
      xpdf output differences?

test_ft2font
   test_fallback_missing
      Font fallback is not implemented.

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
