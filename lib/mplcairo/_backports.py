import functools
import re

from matplotlib import dviread
from matplotlib.dviread import PsfontsMap


@functools.lru_cache()
def _parse_enc(path):
    r"""
    Parses a \*.enc file referenced from a psfonts.map style file.
    The format this class understands is a very limited subset of PostScript.

    Parameters
    ----------
    path : os.PathLike

    Returns
    -------
    encoding : list
        The nth entry of the list is the PostScript glyph name of the nth
        glyph.
    """
    with open(path, encoding="ascii") as file:
        no_comments = "\n".join(line.split("%")[0].rstrip() for line in file)
    array = re.search(r"(?s)\[(.*)\]", no_comments).group(1)
    return re.findall(r"(?<=/)[A-za-z0-9._]+", array)


def get_glyph_name(dvitext):
    tex_font_map = PsfontsMap(dviread.find_tex_file("pdftex.map"))
    ps_font = tex_font_map[dvitext.font.texname]
    return (_parse_enc(ps_font.encoding)[dvitext.glyph]
            if ps_font.encoding is not None else None)
