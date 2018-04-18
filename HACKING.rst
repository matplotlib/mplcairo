C++ style guide
===============

In general, indent by two spaces.

Always break after ``=`` and ``return`` if the rest doesn't fit in one line,
indent the remainder by two spaces.  As an exception, break after the brace in
``auto foo = type{``.

The following vim settings may be useful::

   setlocal cinoptions+=(0,W2,l1,h0
   setlocal shiftwidth=2

Reference style
===============

In comments:

- refer to upstream issues as ``FIXME[upstream-name]: ... (#issue-id)``;
- refer to specific Matplotlib tests as ``:mpltest:`test_foo.test_bar```.
