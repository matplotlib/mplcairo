C++ style guide
---------------

In general, indent by two spaces.

Indent argument lists (whether in parentheses or in braces) by four spaces.

Always break after ``=`` and ``return`` if the rest doesn't fit in one line,
indent the remainder by two spaces... unless it's a braced expression, then
four spaces (see above).

The following vim settings may be useful::

   setlocal cinoptions+=l1,h0
   setlocal shiftwidth=2
