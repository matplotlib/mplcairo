Crashes
=======

::
   gca(); savefig("/tmp/test.cairoscript")

Seems to be due to cairo trying to call ``write`` during shutdown when the
interpreter state is seriously messed up (even though ``_finish`` has been
correctly called first).  See cairo bug #104410.

Fix needed
==========

test_image
   test_jpeg_alpha
      Not respecting savefig.facecolor.

   test_interp_nearest_vs_none[pdf,svg], test_rasterize_dpi[pdf,svg]
      Not respecting dpi in vector output.

test_text
   test_multiline
      Bad multiline rendering.

   test_text_alignment
      Bad alignment.

Upstream issues
===============

Issues with Matplotlib
----------------------

Matplotlib's hatching is inconsistent across backends (#10034). ::

   test_artist::test_clipping

Matplotlib's partially transparent markers with edges are inconsistent across
backends (#10035). ::

   test_axes::test_rgba_markers[pdf]

Matplotlib's SVG backend does not implement Gouraud shading. ::

   test_axes::test_pcolormesh[svg]

Matplotlib's PDFPages is coupled too tightly with the PDF backend (#9114). ::

   test_backend_pdf::test_composite_image, test_multipage_*

Matplotlib's software alpha compositor is incorrect (#8847). ::

   test_image::test_image_composite_alpha[pdf,svg]

Issues with cairo
-----------------

Precision is lost when roundtripping between unmultiplied and premultiplied
alpha. ::

   test_agg::test_repeated_save_with_alpha

cairo does not cull out-of-bounds markers. ::

   test_artist::test_cull_markers

cairo PS does not support SOURCE_DATE_EPOCH. ::

   test_backend_ps::test_source_date_epoch

cairo does not support URLs in SVG output. ::

   test_backend_svg::test_text_urls

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
   test_skew::test_skew_rectange[svg]

Non-issues
==========

We do not implement the deprecated ``svg.image_noscale`` rcparam. ::

   test_backend_svg::test_noscale[svg]
   test_image::test_rotate_image[svg]
   test_tightlayout::test_tight_layout5[svg]

We do not support writing PS to text-mode streams. ::

   test_backend_ps::test_savefig_to_stringio

Tight bboxes are different. ::

   test_bbox_tight::test_bbox_inches_tight_suptile_legend

``--infinite-tolerance`` subverts Matplotlib's test interface. ::

   test_compare_image::*

cairo uses a different representation for ps images (but we perform compositing
correctly, see e.g. SVG output). ::

   test_image::test_composite_image[ps]

cairo does not have an explicit rendering complexity limit. ::

   test_simplification::test_throw_rendering_complexity_exceeded

Other relevant Matplotlib issues
================================

#9963 (behavior with fontsize < 1pt)
