# Dark-mode art (#49)

Dark-theme variants of the black line art in `../icons/` (kept here) and `../images/` (kept in `../images-dark/`), under the same filenames as the originals. When Dark is active the wallet loads a file from these folders if it exists and falls back to the original if it does not, so a missing variant degrades gracefully.

Only the line art needs a variant. The colored icons (clocks, locks, eyes, key, connection bars, coin) and the splash screens read fine on dark as they are. `qrcode.png` stays black on white so it still scans.

How each was made (first pass, ImageMagick 6):

- **Inverted** (silhouettes, `overviewLeftTop`, `bgtile`): `convert in.png -alpha on -channel RGB -negate -evaluate multiply 0.91 +channel out.png`. Black becomes `#e8e8e8`, not pure white.
- **Halo** (`tx_input`, `unit_bob`, `unit_mbob`, `unit_ubob`, `dobbscoin_testnet`, `overviewRight`, `overviewLeftBottom`): the original art over a `#d8d8d8` outline from the alpha dilated by 1px (2px at 128px and wider). Inverting these turned Dobbs into a photo negative.

Not wired into `dobbscoin.qrc` yet. That lands with the theme-switch code.
