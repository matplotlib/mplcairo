import functools
import re


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
