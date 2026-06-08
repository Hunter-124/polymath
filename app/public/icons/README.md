# PWA app icons

Drop the following PNG files in this folder to complete the installable PWA /
home-screen experience. They are referenced by `vite.config.ts`
(`VitePWA → manifest.icons`) and `includeAssets: ['favicon.svg', 'icons/*.png']`.

| File                      | Size      | Purpose            |
| ------------------------- | --------- | ------------------ |
| `icon-192.png`            | 192 × 192 | any                |
| `icon-512.png`            | 512 × 512 | any                |
| `icon-512-maskable.png`   | 512 × 512 | `maskable` (safe-zone padded) |

Until these are added, the web app still runs and installs using
`/favicon.svg`; the PNGs just give the OS a proper home-screen icon.

Design: the same "P" mark as `public/favicon.svg` — white glyph on the
`#5b8cff → #7c5cff` accent gradient over the `#0f1115` background. For the
maskable variant, keep the glyph inside the central 80% safe zone so Android's
icon mask doesn't clip it.

Generate them from the SVG with any rasterizer, e.g.:

```sh
# requires librsvg (rsvg-convert) or ImageMagick
rsvg-convert -w 192 -h 192 ../favicon.svg -o icon-192.png
rsvg-convert -w 512 -h 512 ../favicon.svg -o icon-512.png
# then create icon-512-maskable.png from a padded version of the mark
```
