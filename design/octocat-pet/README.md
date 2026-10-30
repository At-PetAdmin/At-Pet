# Octocat pet artwork

This folder builds the bundled default pet in `assets/default`. The character is
a fan-made flat-vector chibi homage to GitHub's Octocat: black angular cat head,
sharp ears, peach face mask, big eyes, whiskers, four tentacle legs and a curled
sucker-covered tentacle tail.

The artwork is generated with the image model from a canonical base image, then
split into per-animation strips. The deterministic scripts cut the magenta
background, normalize scale and baseline, add flat shadows and props, and pack
the 12 x 7 sheet of 160 x 192 cells.

```powershell
python design/octocat-pet/gen/prepare_ref.py
python design/octocat-pet/gen/extract_strip.py design/octocat-pet/gen/idle-strip.png --count 8 --height 150 --out design/octocat-pet/gen/frames --prefix idle
python design/octocat-pet/gen/build_sheet.py
Copy-Item design/octocat-pet/gen/spritesheet.png assets/default/spritesheet.png
```

`gen/` contains the generated base and animation strips, the extracted frames,
QA strips and the final atlas. The large PNGs are ignored by Git; the scripts
and the committed atlas are the durable artifacts.

Animation layout:

| Frames | Animation |
| --- | --- |
| 0-7 | idle (8 frames) |
| 12-17 | walk (6 frames) |
| 24-27 | sleep (4 frames) |
| 28-31 | interact (4 frames) |
| 36-41 | tool (6 frames) |
| 48-53 | done (6 frames) |
| 60-63 | work (4 frames) |
| 64-67 | review (4 frames) |
| 72-74 | thinking |
| 75-77 | error |
| 78-80 | cancelled |
| 81-83 | permission |
