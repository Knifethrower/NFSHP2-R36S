# R36S speed backlog

What stands between the R36S port and its frame-rate target, with each
item's estimated saving. The estimates come from one device run, so treat
them as estimates until each item is measured on the device. Newest
evidence first. See `docs/R36S_PORT.md` for the port and `HANDOFF.md`
(Session 158) for the history.

## The evidence: device run 1 (2026-11-07)

**Run:** an R36S on dArkOSRE (Mali-G31, blob r13p0), PAL data installed on
the device from the ISO. One race with traffic and cops, about 130 s of
racing. Clocks at their top step, with the profiler and the frame counter
on.

**Files:** the logs are kept in `r36s/build/device/run1/` (not in git):
- `log.txt`: the `perf:` lines;
- `nfshp2_debug.txt`: the game's own frameprof lines;
- `prof_*.txt`: the profile, read with
  `python r36s/scripts/prof_report.py r36s/build/armhf/nfshp2.armhf r36s/build/device/run1`.

**Frames over the race (64 two-second windows):**

| | p10 | median | p90 |
| --- | ---: | ---: | ---: |
| fps | 10.1 | 16.8 | 21.6 |
| frame (ms) | 46.4 | 59.7 | 99.2 |
| game thread outside the swap (ms) | 35.0 | 46.7 | 75.5 |
| swap (ms) | 10.6 | 13.9 | 24.1 |
| draws a frame | 641 | 1,022 | 1,929 |
| texture binds | 511 | 810 | 1,730 |
| uniform uploads | 587 | 1,039 | 1,715 |
| program changes | 104 | 129 | 231 |

**The cost per draw:** frame = 18.8 ms + 42 µs per draw (r² 0.93). The
part outside the swap is 14.8 ms + 32 µs per draw; the swap is 4.1 ms + 10
µs per draw. At the median, 43 of the 60 ms scale with the draw count.

**The game's own stages:**
- The world simulation is small: 2.4 ms median, 3.2 at p90.
- The draw takes the rest. These are medians (p90):

| Stage | median (p90), ms |
| --- | ---: |
| envmap | 13.1 (19.4) |
| models | 8.6 (31.1) |
| cars | 5.6 (11.0) |
| mirror | 6.0 (9.4) |
| scenery | 1.3 (7.5) |
| hud | 1.2 |
| sky | 0.9 |
| cull | 0.4 |
| submit (the swap) | 14.2 (23.8) |

The envmap is six 64-pixel cube faces, drawn every frame.

**The profile:** 29,723 samples, with the game thread taking 85% of them:
- **Mali driver: 49.8%.**
- **libc: 19.0%.** It's concentrated in a few spots. The device's libc
  isn't Debian 12's build, so the spots aren't named yet.
- **The game and the GL layer: 30%.** The largest pieces:
  - `prepare_draw` 3.6%
  - `upload_image` 2.1%
  - `geocull_box_in` 1.5%
  - `row_times_matrix` 1.1%
  - `draw_model` 1.1%
  - `r36sgl_begin_draw` 0.7%
- **Other threads:** the driver's `mali-cmar-backend` thread 5.8%, and
  everything else under 1%.

**What it means:** the frame is bound by the cost of each draw on the game
thread, about 42 µs. The 3DS writes PICA command lists directly, at a few
µs a draw. Here each draw goes through:
- the GL layer's state translation;
- the Mali driver;
- client-side vertex and index arrays (`setup_vertices`), which make the
  driver scan the indices and copy the vertex range on every draw.

The libc share is most likely those copies, but that's inferred. Item 1
tests it.

**The New 3DS for comparison:** the 3DS build runs at 30 FPS, and its
heavy stretches drop to 20 FPS on hardware (HANDOFF, Sessions 120 to 128).
It was never 60.

**The panel:** the menus lock at exactly 12.9 ms a frame with vsync on,
which points to a 77.4 Hz panel rather than 60 Hz. M1 confirms it. On such
a panel, the steady rates are 77.4, 38.7 (two vblanks) and 25.8 fps (three).

## Pending items

The ms columns are savings on the median frame (59.7 ms, 1,022 draws) and
on the p90 frame (99.2 ms, 1,929 draws). They're estimates from run 1
unless marked otherwise.

| # | Item | Median | p90 | Trade-off | Effort |
| --- | --- | ---: | ---: | --- | --- |
| M1 | **Measure first:** log the display's refresh rate, the swap's thread CPU time against its wall time (driver work or GPU wait), the GL extensions (timer queries), and the per-draw time inside GL calls against the GL layer | 0 | 0 | none | low |
| 1 | **GPU buffers for the baked meshes** (built; **measured in run 2: about −3 µs a draw, −1 to −3 ms**, not the estimate): each `geobake` block uploaded once, re-uploaded by `georender_refresh` and freed with its mesh | ~~−10 to −18~~ −1.3 | ~~−20 to −35~~ about −6 at 2,000 draws | none | done |
| 2 | **A VAO per mesh block:** the attribute setup is done once instead of four `glVertexAttribPointer` calls and enable checks per draw | −3 to −5 | −6 to −10 | none | low (after 1) |
| 3 | **Trim the GL layer's per-draw work:** build and compare the program key only when the TexEnv changed, `apply_fixed` only on dirty state, and the 64-bit tick divisions out of the draw path | −1.5 to −2 | −2.5 to −3.5 | none | low |
| 4 | **A GL render thread:** the game thread records the layer's GL work and a second core replays it, one frame behind. The frame then costs the larger of the two sides instead of their sum | −12 to −16 (after 1–3) | −15 to −25 (after 1–3) | one frame of latency | high |
| 5 | **The texture path:** `glTexSubImage2D` for same-size re-uploads, and decoding straight to linear instead of tiling for the 3DS and untiling again | −0.5 to −1.5 | −0.5 to −1.5 | none | low–medium |
| 6 | **Build:** LTO and `-O3`, then PGO, for the game and the layer | −1 to −2 | −1.5 to −3 | none | low |
| 7 | **Tile-friendly passes:** clear or invalidate depth on the envmap, fold and mirror targets (`glInvalidateFramebuffer`) | 0 now (GPU only) | 0 now | none | low |
| 8 | **The envmap a face a frame** (or three faces a frame) | −11 now, −4 to −6 after 1–4 | −16 now, −6 to −9 after 1–4 | reflections lag (the user's call; it was raised on the 3DS and never decided) | low |
| 9 | **The mirror every second frame** (off: double) | −3 now, −1.5 to −2 after 1–4 | −4.7 now, −2 to −3 after 1–4 | the mirror updates at half rate (the user's call) | low |
| 10 | **Fewer draws for the static track:** its geometry merged offline per cell and material (a native world renderer, route-level) | −4 to −6 (after 1–4) | −8 to −15 (after 1–4) | must match the look; culling becomes coarser | very high |
| 11 | **Pacing:** ask KMSDRM for a 60 Hz mode if the panel lists one, otherwise a cap that divides the panel's rate (38.7 at 77.4 Hz) | evenness | evenness | target choice | low |

**Where they land (estimates):**

| After | Median | p90 |
| --- | ---: | ---: |
| now | 59.7 | 99.2 |
| 1, 2, 3, 5 and 6 | about 38 | about 58 |
| plus 4 | about 24 | about 38 |
| plus the trade-offs 8 and 9 | about 18 | about 30 |
| plus 10 | about 13 | about 18 |

- **A steady 30 FPS** (or 38.7 on a 77 Hz panel) looks reachable with
  items 1–4, plus 8 and 9 for the p90.
- **A steady 60** needs everything, including item 10. It also needs the
  GPU, which hasn't been measured, to fit in 16.7 ms.

**Order:**
1. M1, so the device can confirm the split.
2. Items 1, 2 and 3, each with an off switch, then A/B on the device.
3. Item 4.
4. Then the trade-offs, which are the user's to choose.
5. Item 10 is a route decision.

## Device run 2 (2026-11-07): item 1 on, same race as run 1

**Run:** track 11 forwards, 3 AI and 4 cops, 2 laps, as in run 1. The logs
are in `r36s/build/device/run2/`.

**Item 1 saved about 3 ms, not the 10–18 estimated:**

| | Run 1 | Run 2 |
| --- | ---: | ---: |
| Cost per draw | 42.4 µs | 39.4 µs |
| Per draw, outside the swap | 32.3 µs | 28.5 µs |
| Median frame | 59.7 ms | 58.4 ms |
| p90 frame | 99.2 ms | 100.8 ms (more draws: p90 2,089 against 1,929) |
| fps p10 / median / p90 | 10.1 / 16.8 / 21.6 | 9.9 / 17.2 / 24.3 |

- **Coverage:** the buffers carried the draws (median 917 of 1,056).
- **Profile, game thread:**
  - libc fell from 19.0% to 14.3%: one routine at libc+0x76300..0x76500
    went from 3.6% to 0.9%, and +0x1f600 from 5.2% to 3.2%.
  - The driver stayed at 51% (one page, +0x391000, from 2.2% to 0.5%).
- **So** the copies were about 6% of the thread. What's left is the Mali
  blob's own cost per draw: about 28 µs on the game thread, plus about
  11 µs in the swap. That's within the NFS ports' 22–34 µs.

**The dense vegetation seconds** (about 9 fps, 108 ms):

| Pass | Models | Batches | Indices | Time (ms) |
| --- | ---: | ---: | ---: | ---: |
| Main view (models) | ~410 | ~1,400 | ~150k | 31–54 |
| Envmap | ~170 | ~800 | ~150k | 17–20 |
| Mirror | ~120 | ~450 | ~65k | 8–11 |
| Swap | | | | 28–34 |
| World | | | | 3 |

The fastest seconds (27 fps) draw about 180, 110 and 93 batches in those
three passes. The envmap and the mirror are about 47% of the vegetation's
draws.

**The estimates, corrected after run 2:**

| # | Item | Corrected estimate |
| --- | --- | --- |
| 2 | VAOs | 2–4 µs a draw, or 2–4 ms median and 4–8 ms at 2,000 draws. Unproven: the blob's cost may sit in the draw itself |
| 3 | GL layer trims | unchanged, about 1.5–3 ms |
| 4 | Render thread | the driver side is about 65% of the game thread, so it saves the other 35% at most: 58 → about 38 ms median, 108 → about 70 ms in the vegetation |
| 8 | Envmap a face a frame | about 670 fewer draws in the vegetation, about −26 ms there; about −9 ms median |
| 9 | Mirror at half rate | about −9 ms in the vegetation (−18 with the mirror off); about −3 ms median |
| 12 | **New: instancing of repeated meshes** | trees drawn many times with the same mesh, texture and state, as one instanced draw each. Order-free for alpha-tested opaque draws only. The share of repeats is unknown, so measure it first |

**Toward 30 fps in the vegetation** (33 ms at 39 µs a draw): about 850
draws, against about 2,400 now. Only the trade-offs 8 and 9, item 4 and
item 12 together come near that.

## Device run 7 (2026-11-14): the GPU and the GL thread take turns

**Run:** dArkOSRE on the R36S, 640x480 at 77 Hz, the ARMv7 build with the
screen fitting, quality 0, frame counter and profiler on. The log and
profile are in `r36s/build/device/run7/` (the device's clock said 2025-09-03).

**3D windows (2 s each with over 100 draws, 142 of them):**

| | median | p10–p90 |
| --- | ---: | ---: |
| fps | 27.3 | 23.7–32.2 |
| frame (ms) | 36.6 | 31.1–42.2 |
| draws | 744 | 586–868 |
| GL thread busy (ms, all CPU) | 19.6 | 16.2–22.6 |
| swap (ms, 0.4 of it CPU) | 16.9 | 13.5–20.0 |
| game thread (ms) | 19.5 | 15.7–23.5 |
| GPU load (devfreq, 520 MHz) | 54% | 51–60% |

- **The frame is the GL thread's two parts added:** about 20 ms handing
  the driver the frame (82% of its samples in libmali, 15% libc), then 15
  to 17 ms in the swap doing nothing. The game thread's 20 ms runs beside
  them and waits the rest.
- **The GPU is idle almost half the time**, so it isn't the limit in
  these windows. With the GPU drawing a frame while the GL thread makes the
  next, the frame would be the larger of the two (about 20 to 24 ms), which
  is 2 vblanks (25.8 ms, 38.7 fps) instead of 3 (38.7 ms, 25.8 fps).
- **Read from the numbers, not measured directly:** the swap returning
  only once the GPU has drawn the frame. The A/B below checks it.
- **Slow stretches are still the GPU's:** windows of 55 to 68 ms with the
  swap at 33 to 45 ms and the GPU 64–70% busy (at the start, smoke and
  traffic). Those need fewer pixels or less overdraw, not this.

**Built from it: the present thread** (`NFSHP2_PRESENT_THREAD`, on; 0 is
off). A second GL context on its own thread draws each finished frame to
the screen and swaps, while the GL thread goes on with the next frame;
GLES 3 fences order the two on the GPU (glthread.c). One more frame of
latency. `NFSHP2_PRESENT_AB=10` switches to the old order and back every
10 seconds and labels each log window, so one race measures both.
**Estimate:** median frame 37 → 26 to 30 ms (27 → 33 to 38 fps) where the
GPU isn't the limit; no change in the GPU-bound stretches. Not measured
on the device yet.

## Device run 6 (2026-11-09): no crash; the GPU limits the slow stretches

**Run:** track 11, 7 AI, no cops; 490 s of logging. The logs are in
`r36s/build/device/run6/`. The user: "lots of particles and vegetation
drops to 16 fps or so". The slowest window was 14.9 fps; the median 26.0.

- **The leak's fix holds on the device:** RSS levelled at 275 MB, no swap
  used, a normal quit.
- **The steering's balanced:** 135 samples at zero, 62 one way, 71 the other.
  The rest measured was exactly 0,0, so the offset suspected for run 5 isn't
  confirmed as its cause.
- **The GPU bounds the slow windows.** There the GL thread works 23–30 ms
  (CPU), then blocks in the swap for 26–44 ms using 0.4 ms of CPU, and the
  game thread waits 25–47 ms. Frames aren't on vblank multiples, and the same
  load isn't slower late in the run, so neither pacing nor heat explains it.
- **Fit over 264 race seconds (frame, r² 0.75):** about 10 ms, plus 0.23 ms
  per 1,000 main-view indices (29 ms at p90), 2.8 ms per 100 envmap batches
  (18 ms at p90), 0.11 ms per 1,000 mirror indices (7 ms at p90), and 1.4 ms
  per 100 particles (3 ms at p90). Batches and indices move together, so
  these are approximate.
- **Reading it:** the envmap's indices cost nothing while they draw as many
  as the main view, on 64-pixel faces. So the main view's cost is most
  likely pixels filled, not vertices. The R36S fills 640×480 (307,200) where
  the 3DS fills 240×400 (96,000), 3.2 times as many. Not measured directly:
  the log now has the GPU's own load.

**Built:** presets that change the picture, cycled live with Select + R2 or
set with `NFSHP2_QUALITY`, 0 by default: 1 (FAST) is the envmap one face a
frame and the mirror every second frame; 2 (FASTEST) also draws the top
target at 75% (`NFSHP2_RENDER_SCALE`) and stretches it, 44% fewer pixels.

**Estimated in the slowest seconds (about 55 ms), to measure on the device:**
FAST about −15 to −20 ms, FASTEST about −10 to −13 ms more.

## Device run 4 (2026-11-08): killed, a leak found

**Run:** track 31 forwards, 3 AI and 4 cops. The logs are in
`r36s/build/device/run4/`. The game ended with **status 137 (SIGKILL)**
about 380 s after launch, with normal frame times to the last line, no crash
report and no quit combo. The log doesn't name the killer: nothing recorded
memory or the kernel's messages yet.

**The leak (found under emulation, and in the code since the first build):**
- `follow_flipbooks` shows a frame with `t->tex = f->tex`, a struct never
  bound and so with no GL name. Each flip made a new GL texture, uploaded
  it, and lost the old name.
- An emulated soak with the draws skipped (`NFSHP2_GL_NODRAW=1`): RSS rose
  in a straight line from 302 to 3,604 MB over 80,000 frames, about 41 KB
  a frame. GL textures made reached 4,367 within minutes.
- That run's uploads add up to 465 MB of texture data, on a device with
  1 GB of memory. So memory is the likely killer, though not proven.

**The fix:** a GL texture now goes with its pixel memory, not its struct
(`NFSHP2_TEXSHARE=0` goes back). A fresh copy of the same pixels takes the
same texture. `C3D_TexDelete` and `linearFree` free it. Emulated soak after
the fix: RSS levels off at 305–311 MB, about 309 textures, 0 uploads a
frame, and the race frames match the build before the fix.

**Also added:**
- A 512 MB `.swapfile`, as the other R36S ports have, on only while the
  game runs (`NFSHP2_SWAP=0`). Not tried on the device: `swapon` may refuse
  a file on the exFAT card, and the log says either way. Removed from the
  launcher on 2026-11-15 for PortMaster's rules, with the clock setting.
- `glTexSubImage2D` for same-size re-uploads (`NFSHP2_TEXSUB=0`).
- Limits on what may queue for the GL thread: 96 chunks and 48 MB of large
  copies, with 24 played chunks kept.
- A `memory:` line every two seconds (RSS, swapped, available, swap, major
  faults, malloc, GL textures made).
- After a non-zero exit, the launcher logs memory and the kernel's last
  lines, so another kill names its cause.

**Confirmed by run 4's new log lines:**
- **Shader compiles are the short stutters:** 73–135 ms a program on the
  device, up to 263.8 ms in one frame for two of them. The cache should
  remove them from the second run on.
- **The swap is a wait:** its CPU time is 0.4 ms of its 12–16 ms. So the
  GL thread blocks there on the GPU or the display. That points to GPU work
  and pacing as the next limit, not driver calls.

## Device run 3 (2026-11-08): grouping and the GL thread on

**Run:** track 44 forwards, 7 AI, no cops, 2 laps: a different race from
runs 1 and 2. The logs are in `r36s/build/device/run3/`. The user's verdict:
"pretty good, but still some dips".

| Race windows | p10 | median | p90 |
| --- | ---: | ---: | ---: |
| fps | 22.1 | 27.7 | 34.6 |
| frame (ms) | 28.9 | 36.2 | 45.3 |
| GL draws | 527 | 692 | 892 |
| draws before grouping | | about 1,157 | |
| game thread's wait for the GL thread (ms) | 12.7 | 16.6 | 22.1 |

Run 2's median was 58.4 ms for about 1,056 draws, so the frame's cost for a
similar load fell by about 38%. That isn't a like-for-like comparison.

**What the run showed:**
- **The panel has one mode:** `640x480@77`. `NFSHP2_HZ=60` can't help on
  this device. Each picture stays up for whole vblanks (12.9 ms), though
  the frame log isn't locked to them: a window can average 43 ms with a
  45.8 ms worst.
- **The GL thread bounds the frame:** it's busy for all of it, about 19 ms
  up to the swap and 17 ms in it. The game thread's work is about 20 ms.
- **Profile of the GL thread:** Mali 84%, libc 13%.
- **Profile of the game thread:** `try_hold` 12%, libc 20%.
- **The main thread's 8.5% isn't work:** all but one of its samples are at
  one libc address (taken to be its `pthread_join` wait), so the profiler's
  signal is landing on a sleeping thread.
- **Fit over the race windows:** outside the swap (r² 0.87), about 18 µs a
  GL draw and 6 µs a texture bind. The swap (r² 0.50) is about 10 ms
  constant and only about 1 µs a GL draw; the rest of it follows uniforms
  (a proxy for lit draws) and instances. That points to geometry or GPU
  work, not per-draw driver work. Unproven until the new CPU figures.
- **Two kinds of dip:**
  - Single-frame spikes of 100–440 ms in the game's `cars` and `mirror`
    stages. Suspected: shaders compiled mid-race. The game thread shows
    only 1.3% in Mali, so the total time is small but concentrated. The
    new hitch lines will confirm it.
  - Slow seconds (45–85 ms) where `submit` grows and `models` doesn't.
    They have 7–8 cars in view, and tyre smoke at the start. A static grid
    view with the same counts took between 67 and 85 ms a second.

**Built after run 3, each with an off switch:**
- **The program cache** (`NFSHP2_PROGCACHE=0`): compiled programs saved to
  `nfshp2_programs.bin`, tied to the driver's version and a hash of the
  shader text, and loaded together at the first draw of each vertex
  shader. Emulated second run: 16 programs loaded, none compiled.
  **The first run after installing still compiles.**
- **Group sorting** (`NFSHP2_INSTANCE_SORT=0`): order-free groups sorted by
  program and texture. Emulated: texture binds 835 → 551, program changes
  274 → 231.
- **Depth discarded** on leaving a target (`NFSHP2_INVALIDATE=0`), and the
  window asks for no depth or stencil. `NFSHP2_INVALIDATE=2` scribbles over
  the depth instead, for a software renderer. Emulated with the scribble
  and the sort: 1–3 isolated pixels differ by 9 or more levels, about 6,000
  by 1 level, and no regions change.
- **`try_hold`:** a word-wise hash and a smaller record.
- **New in the perf line:** the GL thread's CPU time, the swap's CPU time
  (the rest of the swap is a wait), the game thread's time, and programs
  built.
- **New hitch lines:** any frame over 66 ms logs the game, GL and swap times,
  programs compiled, uploads and meshes made.

**Not measured on the device:** any of the above. The savings are unknown:
the spikes should go if they are compiles, and the GPU-side work (the
discard) may or may not show.

## Built after run 2 (2026-11-08), measured in run 3

**Item 12, grouped (instanced) draws** (`c3d_gles.c`, `NFSHP2_INSTANCE=0`
off):
- **Which draws:** a geosolid draw that tests and writes depth, doesn't
  blend, and draws from a mesh's GPU buffer waits ("held").
- **How they merge:** held draws with the same state and mesh become one
  draw of geosolid's instanced twin (`kVsGeoSolidInst`). The model's
  `mvp`, `fogModel` and `lightModel` become six vec4 attributes per
  instance.
- **What flushes them first:** anything else that depends on draw order.
  That's a blended draw or one that doesn't write depth, citro2d, a
  clear, a target change, a texture or mesh about to upload, a texture or
  target deleted, and the frame's end.
- **Lit opaque draws** go straight through: they are order-free too.
- **Emulated, track 11 with 3 AI and 4 cops:** about 1,400 of the frame's
  2,000 draws are held and become about 560, so the frame has **about
  1,210 GL draws instead of 2,077 (−41%)**. Texture binds fall from 1,665
  to 843, and uniform uploads from 2,029 to 1,105.
- **Not held:** about 210 a frame are lit or not geosolid, about 380 test
  depth but don't write it, and about 40 blend.
- **Needs an ES 3 context:** the window and the headless pbuffer now ask
  for ES 3.0 (falling back to 2.0), and grouping turns itself off without
  ES 3.
- **A bug on the way:** splitting `apply_fixed` into a snapshot and an
  apply first made the snapshot copy zeros. The emulated images exposed
  it, and it was fixed before any measurement below.

**Item 4, the GL thread** (`glthread.c` and `glthread.h`,
`NFSHP2_GLTHREAD=0` off):
- **Recording:** every GL call of the layer is recorded into 256 KB
  chunks, with copies of what it reads: uniform values, pixels, buffer
  data, shader text, and the vertex range and indices of client-array
  draws.
- **Playing:** a thread named `nfshp2-gl` holds the context and plays
  chunks as they fill (at 32 KB), then does the swap and the frame log.
- **Pacing:** one frame in flight.
- **Calls that need answers** (program links, uniform locations,
  framebuffer status, debug reads) wait for the thread and borrow the
  context. A program build borrows it once.
- **Names:** textures and buffers come from pools the thread refills.
- **New in the perf line:** "GL thread busy X ms, game thread waited Y ms".

**Item 2 (part): attribute pointers cached** (`r36sgl_attrib_pointer`):
the batches of one mesh set their pointers once. The cache is cleared when
mesh buffers are deleted, or when GL's state is unknown.

**Items 8 and 9 as options, off unless set** (`trackview.c`, under
`NFSHP2_R36S`):
- `NFSHP2_ENVMAP_FACES=N` redraws N of the envmap's six faces a frame, in
  turn. The fold already updates only the faces drawn, so the others keep
  their last picture.
- `NFSHP2_MIRROR_EVERY=N` redraws the mirror every Nth frame; its texture
  shows the last picture in between.
- Emulated with 1 face and every 2nd frame: about 830–980 GL draws a
  frame, against about 1,190 with grouping alone.
- The user hasn't chosen them yet.

**Pacing (item 11, part):** the log lists the display's modes and refresh
rate at the start. `NFSHP2_HZ=60` switches to a 60 Hz mode of the same
size, if the panel has one.

**Image checks (QEMU and softpipe, pixel for pixel):**
- Track 42 with everything off matches the frames saved before these
  changes.
- Track 11's race frames are compared with grouping on and off, and with
  the GL thread on and off.

**Expected on the device (estimate):**
- **The GL thread** carries the driver's work: about 39 µs × the GL draws.
  That's about 1,056 → about 620 GL draws at the median, and about 2,400
  → about 1,450 in the vegetation.
- **The game thread** keeps the game, the layer and the recording: about
  35% of today's frame.
- **Median:** max(about 20, about 24 + 5) ≈ 25–30 ms.
- **Vegetation:** max(about 38, about 57 + 5) ≈ 55–65 ms.
- **On a 77.4 Hz panel,** vsync rounds frames to 25.8 ms (38.7 fps) or
  38.7 ms (25.8 fps). 30 fps needs a 60 Hz mode, or frames under 25.8 ms.

## Item 1: built (measured in run 2)

**The code:**
- `r36s/compat/src/c3d_buffers.c` keeps each mesh's vertices and indices in
  GL buffers.
- `geobake.c` reports a bake (`r36s_mesh_static`) and a refresh
  (`r36s_mesh_changed`) under `NFSHP2_R36S`.
- `linearFree` forgets a block, and its buffers are deleted at the next
  frame's start.

**Settings:**
- `NFSHP2_VBO=0` turns it off.
- `NFSHP2_VBO=2` checks every block against the game's memory each frame
  and logs any difference.

**The perf line** now ends with "N draws from mesh buffers, M made (K KB)".

**What stays on client arrays:**
- the rings (quads, strips, sprites, skins);
- the envmap's fold;
- citro2d;
- posed copies, which draw ring vertices with a mesh's indices.

**Emulated (QEMU and softpipe, track 42 by SkipFE):**
- **Coverage:** 1,694 to 1,773 of the race's 1,796 to 1,872 draws a frame
  come from mesh buffers (about 95%). 118 blocks (1.2 MB) are made at the
  start of the race.
- **Verify mode:** no differences reported.
- **Image:** frames 24 and 30 are pixel-identical with `NFSHP2_VBO=2` and
  `NFSHP2_VBO=0`.

**Not measured on the device yet:** the next run is track 11 forwards
with 3 AI and 4 cops, as in run 1.

## Not to retry without new evidence

- **Sorting draws by texture across a pass:** it changes the order of
  blended draws.
- **Merging adjacent draws at run time:** Burnout 3 found it merged under
  1% of its draws (see the perf skill's tried-and-failed list).
