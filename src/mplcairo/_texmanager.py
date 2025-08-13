# Alternative texmanager implementation, modified from Matplotlib's, with added
# support for {xe,lua}latex.

import functools
import hashlib
import logging
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

import numpy as np

import matplotlib as mpl
from matplotlib import cbook, dviread

_log = logging.getLogger(__name__)


def _get_tex_engine():
    from mplcairo import get_options
    return get_options()["tex_engine"]


def _usepackage_if_not_loaded(package, *, option=None):
    """
    Output LaTeX code that loads a package (possibly with an option) if it
    hasn't been loaded yet.

    LaTeX cannot load twice a package with different options, so this helper
    can be used to protect against users loading arbitrary packages/options in
    their custom preamble.
    """
    option = f"[{option}]" if option is not None else ""
    return (
        r"\makeatletter"
        r"\@ifpackageloaded{%(package)s}{}{\usepackage%(option)s{%(package)s}}"
        r"\makeatother"
    ) % {"package": package, "option": option}


class TexManager:
    """
    Convert strings to dvi files using TeX, caching the results to a directory.

    The cache directory is called ``tex.cache`` and is located in the directory
    returned by `.get_cachedir`.

    Repeated calls to this constructor always return the same instance.
    """

    _texcache = os.path.join(mpl.get_cachedir(), 'tex.cache')
    _grey_arrayd = {}

    _font_families = ('serif', 'sans-serif', 'cursive', 'monospace')
    _font_preambles = {
        'new century schoolbook': r'\renewcommand{\rmdefault}{pnc}',
        'bookman': r'\renewcommand{\rmdefault}{pbk}',
        'times': r'\usepackage{mathptmx}',
        'palatino': r'\usepackage{mathpazo}',
        'zapf chancery': r'\usepackage{chancery}',
        'cursive': r'\usepackage{chancery}',
        'charter': r'\usepackage{charter}',
        'serif': '',
        'sans-serif': '',
        'helvetica': r'\usepackage{helvet}',
        'avant garde': r'\usepackage{avant}',
        'courier': r'\usepackage{courier}',
        # Loading the type1ec package ensures that cm-super is installed, which
        # is necessary for Unicode computer modern.  (It also allows the use of
        # computer modern at arbitrary sizes, but that's just a side effect.)
        'monospace': r'\usepackage{type1ec}',
        'computer modern roman': r'\usepackage{type1ec}',
        'computer modern sans serif': r'\usepackage{type1ec}',
        'computer modern typewriter': r'\usepackage{type1ec}',
    }
    _font_types = {
        'new century schoolbook': 'serif',
        'bookman': 'serif',
        'times': 'serif',
        'palatino': 'serif',
        'zapf chancery': 'cursive',
        'charter': 'serif',
        'helvetica': 'sans-serif',
        'avant garde': 'sans-serif',
        'courier': 'monospace',
        'computer modern roman': 'serif',
        'computer modern sans serif': 'sans-serif',
        'computer modern typewriter': 'monospace',
    }

    @functools.lru_cache  # Always return the same instance.
    def __new__(cls):
        Path(cls._texcache).mkdir(parents=True, exist_ok=True)
        return object.__new__(cls)

    @classmethod
    def _get_font_family_and_reduced(cls):
        """Return the font family name and whether the font is reduced."""
        ff = mpl.rcParams['font.family']
        ff_val = ff[0].lower() if len(ff) == 1 else None
        if len(ff) == 1 and ff_val in cls._font_families:
            return ff_val, False
        elif len(ff) == 1 and ff_val in cls._font_preambles:
            return cls._font_types[ff_val], True
        else:
            _log.info('font.family must be one of (%s) when text.usetex is '
                      'True. serif will be used by default.',
                      ', '.join(cls._font_families))
            return 'serif', False

    @classmethod
    def _get_font_preamble_and_command(cls):
        requested_family, is_reduced_font = cls._get_font_family_and_reduced()

        preambles = {}
        for font_family in cls._font_families:
            if is_reduced_font and font_family == requested_family:
                preambles[font_family] = cls._font_preambles[
                    mpl.rcParams['font.family'][0].lower()]
            else:
                rcfonts = mpl.rcParams[f"font.{font_family}"]
                for i, font in enumerate(map(str.lower, rcfonts)):
                    if font in cls._font_preambles:
                        preambles[font_family] = cls._font_preambles[font]
                        _log.debug(
                            'family: %s, package: %s, font: %s, skipped: %s',
                            font_family, cls._font_preambles[font], rcfonts[i],
                            ', '.join(rcfonts[:i]),
                        )
                        break
                else:
                    _log.info('No LaTeX-compatible font found for the %s font'
                              'family in rcParams. Using default.',
                              font_family)
                    preambles[font_family] = cls._font_preambles[font_family]

        # The following packages and commands need to be included in the latex
        # file's preamble:
        cmd = {preambles[family]
               for family in ['serif', 'sans-serif', 'monospace']}
        if requested_family == 'cursive':
            cmd.add(preambles['cursive'])
        cmd.add(r'\usepackage{type1cm}')
        preamble = '\n'.join(sorted(cmd))
        fontcmd = (r'\sffamily' if requested_family == 'sans-serif' else
                   r'\ttfamily' if requested_family == 'monospace' else
                   r'\rmfamily')
        return preamble, fontcmd

    @classmethod
    def get_basefile(cls, tex, fontsize, dpi=None):
        """
        Return a filename based on a hash of the string, fontsize, and dpi.
        """
        src = cls._get_tex_source(tex, fontsize) + str(dpi)
        filehash = hashlib.sha256(
            src.encode('utf-8'),
            usedforsecurity=False
        ).hexdigest()
        filepath = Path(cls._texcache)

        num_letters, num_levels = 2, 2
        for i in range(0, num_letters*num_levels, num_letters):
            filepath = filepath / Path(filehash[i:i+2])

        filepath.mkdir(parents=True, exist_ok=True)
        return os.path.join(filepath, filehash)

    @classmethod
    def get_font_preamble(cls):
        """
        Return a string containing font configuration for the tex preamble.
        """
        font_preamble, command = cls._get_font_preamble_and_command()
        return font_preamble

    @classmethod
    def get_custom_preamble(cls):
        """Return a string containing user additions to the tex preamble."""
        return mpl.rcParams['text.latex.preamble']

    @classmethod
    def _get_tex_source(cls, tex, fontsize):
        """Return the complete TeX source for processing a TeX string."""
        font_preamble, fontcmd = cls._get_font_preamble_and_command()
        baselineskip = 1.25 * fontsize
        return "\n".join([
            rf"% !TeX program = {_get_tex_engine()}",
            r"\documentclass{article}",
            r"% Pass-through \mathdefault, which is used in non-usetex mode",
            r"% to use the default text font but was historically suppressed",
            r"% in usetex mode.",
            r"\newcommand{\mathdefault}[1]{#1}",
            r"\usepackage{iftex}",
            r"\ifpdftex",
            r"\usepackage[utf8]{inputenc}",
            r"\DeclareUnicodeCharacter{2212}{\ensuremath{-}}",
            font_preamble,
            r"\fi",
            r"\ifluatex",
            r"\begingroup\catcode`\%=12\relax\gdef\percent{%}\endgroup",
            r"\directlua{",
            r"  v = luaotfload.version",
            r"  major, minor = string.match(v, '(\percent d+).(\percent d+)')",
            r"  major = tonumber(major)",
            r"  minor = tonumber(minor) - (string.sub(v, -4) == '-dev' and .5 or 0)",
            r"  if major < 3 or major == 3 and minor < 15 then",
            r"    tex.error(string.format(",
            r"      'luaotfload>=3.15 is required; you have \percent s', v))",
            r"  end",
            r"}",
            r"\fi",
            r"% geometry is loaded before the custom preamble as ",
            r"% convert_psfrags relies on a custom preamble to change the ",
            r"% geometry.",
            r"\usepackage[papersize=72in, margin=1in]{geometry}",
            cls.get_custom_preamble(),
            r"% Use `underscore` package to take care of underscores in text.",
            r"% The [strings] option allows to use underscores in file names.",
            _usepackage_if_not_loaded("underscore", option="strings"),
            r"% Custom packages (e.g. newtxtext) may already have loaded ",
            r"% textcomp with different options.",
            _usepackage_if_not_loaded("textcomp"),
            r"\pagestyle{empty}",
            r"\begin{document}",
            r"% The empty hbox ensures that a page is printed even for empty",
            r"% inputs, except when using psfrag which gets confused by it.",
            r"% matplotlibbaselinemarker is used by dviread to detect the",
            r"% last line's baseline.",
            rf"\fontsize{{{fontsize}}}{{{baselineskip}}}%",
            r"\ifdefined\psfrag\else\hbox{}\fi%",
            rf"{{{fontcmd} {tex}}}%",
            r"\end{document}",
        ])

    @classmethod
    def make_tex(cls, tex, fontsize):
        """
        Generate a tex file to render the tex string at a specific font size.

        Return the file name.
        """
        texfile = cls.get_basefile(tex, fontsize) + ".tex"
        Path(texfile).write_text(cls._get_tex_source(tex, fontsize),
                                 encoding='utf-8')
        return texfile

    @classmethod
    def _run_checked_subprocess(cls, command, tex, *, cwd=None):
        _log.debug(cbook._pformat_subprocess(command))
        try:
            report = subprocess.check_output(
                command, cwd=cwd if cwd is not None else cls._texcache,
                stderr=subprocess.STDOUT)
        except FileNotFoundError as exc:
            raise RuntimeError(
                f'Failed to process string with tex because {command[0]} '
                'could not be found') from exc
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(
                '{prog} was not able to process the following string:\n'
                '{tex!r}\n\n'
                'Here is the full command invocation and its output:\n\n'
                '{format_command}\n\n'
                '{exc}\n\n'.format(
                    prog=command[0],
                    format_command=cbook._pformat_subprocess(command),
                    tex=tex.encode('unicode_escape'),
                    exc=exc.output.decode('utf-8', 'backslashreplace'))
                ) from None
        _log.debug(report)
        return report

    @classmethod
    def make_dvi(cls, tex, fontsize):
        """
        Generate a dvi file containing latex's layout of tex string.

        Return the file name.
        """
        basefile = cls.get_basefile(tex, fontsize)
        ext = {"latex": "dvi", "xelatex": "xdv", "lualatex": "dvi"}[
            _get_tex_engine()]
        dvifile = f"{basefile}.{ext}"
        if not os.path.exists(dvifile):
            texfile = Path(cls.make_tex(tex, fontsize))
            # Generate the dvi in a temporary directory to avoid race
            # conditions e.g. if multiple processes try to process the same tex
            # string at the same time.  Having tmpdir be a subdirectory of the
            # final output dir ensures that they are on the same filesystem,
            # and thus replace() works atomically.  It also allows referring to
            # the texfile with a relative path (for pathological MPLCONFIGDIRs,
            # the absolute path may contain characters (e.g. ~) that TeX does
            # not support; n.b. relative paths cannot traverse parents, or it
            # will be blocked when `openin_any = p` in texmf.cnf).
            cwd = Path(dvifile).parent
            with TemporaryDirectory(dir=cwd) as tmpdir:
                tmppath = Path(tmpdir)
                cmd = {
                    "latex": ["latex"],
                    "xelatex": ["xelatex", "-no-pdf"],
                    "lualatex": ["lualatex", "--output-format=dvi"],
                }[_get_tex_engine()]
                cls._run_checked_subprocess(
                    [*cmd, "-interaction=nonstopmode", "--halt-on-error",
                     f"--output-directory={tmppath.name}",
                     f"{texfile.name}"], tex, cwd=cwd)
                (tmppath / Path(dvifile).name).replace(dvifile)
        return dvifile

    @classmethod
    def get_text_width_height_descent(cls, tex, fontsize, renderer=None):
        """Return width, height and descent of the text."""
        if tex.strip() == '':
            return 0, 0, 0
        dvifile = cls.make_dvi(tex, fontsize)
        dpi_fraction = renderer.points_to_pixels(1.) if renderer else 1
        with dviread.Dvi(dvifile, 72 * dpi_fraction) as dvi:
            page, = dvi
        # A total height (including the descent) needs to be returned.
        return page.width, page.height + page.descent, page.descent
