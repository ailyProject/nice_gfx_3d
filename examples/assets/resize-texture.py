"""Prepare example textures only; no runtime/compiler tests are invoked."""
from pathlib import Path
import sys
from PIL import Image

source, destination, maximum = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
with Image.open(source) as image:
    image = image.convert("RGBA")
    image.thumbnail((maximum, maximum), Image.Resampling.LANCZOS)
    image.save(destination, format="PNG", optimize=True)
