Known test failures
===================

test_agg.test_repeated_save_with_alpha
   Loss of precision due to roundtrip between unmultiplied and premultiplied
   alpha.

test_artist.test_cull_markers
   Not implemented by cairo, apparently?

test_backend_pdf.test_composite_image, test_multipage_*
   Needs upstream fix #9114.

test_backend_pdf.test_pdf_savefig_when_color_is_none
   Needs upstream fix #9911.

test_backend_ps.savefig_to_stringio[...]
   Some parametrizations test writing to text-mode file, which makes no sense.
