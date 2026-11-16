# Need for Speed: Hot Pursuit 2

A native port of the PS2 version of Need for Speed: Hot Pursuit 2 (2002), built from its decompilation. It needs your own PAL PS2 disc (SLES-50731). The package holds no game data.

## Game files

1. Make an `.iso` image of your disc and copy it to `ports/nfshp2/gamedata/`.
2. Launch the port. The first launch installs the game from the image: about 10 minutes, with 3 GB free beside it. Progress is in `ports/nfshp2/patchlog.txt`. If it stops, launch again and it carries on.
3. When the game runs, you can delete the `.iso`.

No `python3` on your device? Install from a computer with Python 3.8 or newer, from the source repository's top folder, after copying `ZDIR.BIN` and `ZZDATA.BIN` from the disc's `NFSHP2` folder into `PS2 Files/Full Game/`:

```
python tools/zdir_extract.py --retail --decompress
python tools/audio_pack.py extracted-retail/SOUND extracted-retail/SOUND
python tools/make_card.py card
```

Copy the `card/NFSHP2` folder into `ports/nfshp2/gamedata/`.

## Controls

| Button | Action |
| --- | --- |
| Left stick, D-pad Left / Right | steer |
| Bottom button (B) | accelerate |
| Left button (Y) | brake and reverse |
| Right button (A) | horn (cop: change target) |
| Top button (X) | change camera |
| L1 / R1 | look back / handbrake |
| L2 / R2 | shift down / up (manual gearbox) |
| Right stick up / down | gas / brake |
| D-pad Up | change HUD |
| Start | pause |
| Select + Start, held a second | quit |
| Select + R2 | change graphics preset |
| Select + R1 | swap D-pad and left stick |

## Settings

Optional `ports/nfshp2/nfshp2.cfg`, one `NAME=value` per line:

- `NFSHP2_SHOW_FPS=1`: frame counter on from the start.
- `NFSHP2_QUALITY=0`, `1` or `2`: graphics preset to start on (0 full, 2 fastest).
- `NFSHP2_ASPECT=0`: the PS2's 4:3 picture with bars, instead of filling the screen.
- `NFSHP2_GLYPHS=ps2` or `xbox`: prompts in the PlayStation symbols, or in an Xbox-layout pad's letters, instead of the handheld's own A/B/X/Y.

## Notes

- Needs PortMaster on an armhf device (32-bit ARM with SDL2 and OpenGL ES). The game can't run in 64-bit.
- Tested on an R36S with dArkOS.
- Logs: `ports/nfshp2/log.txt`.

## Credits

Thanks to Electronic Arts and EA Black Box for the original game, and to [Octoblimp](https://github.com/Octoblimp) and the contributors to the decompilation and New 3DS port. Linux handheld port by manuelpllull. Source (GPL-3.0): https://github.com/manuelpllull/NFSHP2-R36S
