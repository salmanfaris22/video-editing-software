# Lectern brand

**The mark:** a bold "L" for Lectern. Its foot is cut into timeline clips
(editing), and a glowing red record dot with a pulse ring sits in the
corner (recording). Dark theme first, using the app's own colors.

| File | Use |
|---|---|
| `lectern-app-icon.svg` | Master app icon, 1024 × 1024, dark rounded tile (macOS icon grid: 824 px tile, 100 px margin) |
| `lectern-app-icon-1024.png` | Rendered master (store listings, website) |
| `lectern-mark.svg` | The mark alone, transparent background |
| `lectern-wordmark-dark.svg` | Mark + "Lectern" on the dark plate |
| `lectern-wordmark-light.svg` | Mark + "Lectern" for light backgrounds |
| `../../src/app/icons/Lectern.icns` | macOS app icon (16–1024 px), set in the bundle |
| `../../src/app/icons/lectern.ico` | Windows icon (16–256 px), embedded through `lectern-icon.rc.in` |
| `../../src/ui/brand/lectern-icon-256.png` | In-app logo (home screen) |
| `../../src/ui/icons/logo.svg` | One-color 24 px version in the UI icon set |

## Colors

| Token | Hex | Role |
|---|---|---|
| Ink | `#0B0C10` | Background, tile bottom |
| Tile top | `#1C2030` | Tile gradient top |
| Periwinkle light | `#B4BDFF` | L gradient start |
| Periwinkle | `#7C8CFF` | Accent (= `Theme.accent`) |
| Periwinkle deep | `#5566F0` | L gradient end |
| Record red | `#FF4D5E` | Record dot (= `Theme.record`) |
| Text | `#ECEEF3` | Wordmark on dark |

## Rules
- Keep the record dot red and the L periwinkle; on a single-color surface
  use `lectern-mark` in white (or `logo.svg`).
- Clear space around the mark: at least the record dot's diameter.
- Smallest size: 16 px (the icon stays readable; checked at 16/32/128/512).
- The wordmark uses Inter Bold (fallback SF Pro Display / Helvetica). Convert
  the text to outlines before print or external use so the font cannot change.

## Regenerating the icon files
1. Edit `lectern-app-icon.svg`.
2. Render PNGs at 16, 24, 32, 48, 64, 128, 256, 512 and 1024 px (any SVG
   renderer that supports filters and masks, e.g. headless Chrome
   `--screenshot` with `--default-background-color=00000000`).
3. macOS: put them in `Lectern.iconset` with Apple's names
   (`icon_16x16.png`, `icon_16x16@2x.png`, … `icon_512x512@2x.png`) and run
   `iconutil -c icns Lectern.iconset -o src/app/icons/Lectern.icns`.
4. Windows: pack 16–256 px PNGs into `src/app/icons/lectern.ico`
   (PNG-compressed ICO entries).
5. Copy the 256 px PNG to `src/ui/brand/lectern-icon-256.png`.
