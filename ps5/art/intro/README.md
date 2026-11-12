# The startup video

`intro.ivf` (VP9, 1920 x 1080, 60 frames a second, 8 s) and `intro.wav` (48 kHz 16-bit stereo) play
when OGBox starts (ps5/src/ogbox_intro.cpp; Settings > Interface > Startup video). The OGBox X glows
behind a sheet of black glass, three blows crack it, the glass bulges and shatters in 3D, and the
logo and the word are revealed. Both are made from the sources in `src/`, by Press5elect,
GPL-3.0-or-later like the rest of OGBox (the logo is `ps5/art/ogbox-logo.png`).

To make them again (Node.js 18+, Python 3 with NumPy, ffmpeg with libvpx, Playwright's Chromium):

```sh
cd ps5/art/intro/src
npm install three@0.160.0 playwright      # three.js (MIT): the renderer, not shipped
cp ../../ogbox-logo.png logo.png
node render.js video.mp4                  # every frame drawn in Chromium (SwiftShader): about 30 min
OUT=../intro.wav python3 audio.py         # the sound, timed to main.js's timeline (T)
for p in 1 2; do ffmpeg -y -i video.mp4 -an -c:v libvpx-vp9 -b:v 0 -crf 22 -pass $p -passlogfile vp9 \
  -row-mt 1 -tile-columns 2 -g 120 -auto-alt-ref 1 -lag-in-frames 25 -deadline good -cpu-used 1 \
  -pix_fmt yuv420p -f ivf $([ $p = 1 ] && echo /dev/null || echo ../intro.ivf); done
```

The colours are BT.601 studio range (ffmpeg's default from RGB), as the player turns them back.
`node render.js stills 1.9 3.9 5.5` writes single frames (`s_<t>.png`) to look at.
