# Built-in default pet

`spritesheet.png` and `pet.json` are embedded through `pet.qrc` and used as the
built-in default pet. The character is a fan-made chibi homage to GitHub's
Octocat mascot: black cat head, cat ears, peach face mask, large eyes, whiskers,
four tentacle legs and one curled sucker-covered tentacle tail.

It is not affiliated with or endorsed by GitHub, Inc. "Octocat" is a trademark
of GitHub, Inc.

## Layout

The sheet is a 12 x 7 grid of 160 x 192 cells (1920 x 1344 pixels):

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

The editable image-generation pipeline and per-frame sources live in
`design/octocat-pet/gen`. After extracting frames into `gen/frames`, rebuild the
atlas with:

```powershell
python design/octocat-pet/gen/build_sheet.py
Copy-Item design/octocat-pet/gen/spritesheet.png assets/default/spritesheet.png
```
