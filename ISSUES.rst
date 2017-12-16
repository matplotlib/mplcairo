Needs fix
=========

test_image
   test_jpeg_alpha
      Not respecting savefig.facecolor...

Known test failures
===================

test_agg
   test_repeated_save_with_alpha
      Loss of precision due to roundtrip between unmultiplied and premultiplied
      alpha.

test_artist
   test_cull_markers
      Not implemented by cairo, apparently?

test_backend_pdf
   test_composite_image, test_multipage_*
      Needs upstream fix #9114.

   test_pdf_savefig_when_color_is_none
      Needs upstream fix #9911.

   test_source_date_epoch
      Not supported by cairo.

test_backend_ps
   test_savefig_to_stringio
      Some parametrizations test writing to text-mode file, which makes no
      sense.

   test_source_date_epoch
      Not supported by cairo.

test_backend_svg
   test_text_urls
      Not supported by cairo.

test_bbox_tight
   test_bbox_inches_tight_suptile_legend
      Tight bboxes are different.

test_compare_image
   ... testing Matplotlib's own test interface, which we subvert via
   --infinite-tolerance.

test_image
   test_composite_image[..., ps, ...]
      cairo uses a different representation for images (but compositing is
      correct, see e.g. svg test).

test_simplification
   test_throw_rendering_complexity_exceeded
      cairo does not have the same limitation.

Relevant Matplotlib issues
==========================

#9963
