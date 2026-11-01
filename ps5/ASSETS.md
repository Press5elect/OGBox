# Assets

The assets the title ships, written from `ps5/assets.json` by
`ps5/tools/build-assets.py --notices`. Only assets with a clear licence are
shipped: in PS5_VulkanTemplate, the asset pack's other files (the `assets`
submodule) stay out, and the samples that use them get the replacements here.

| Path under `/app0/assets/` | Asset | Author | Licence | Samples |
| --- | --- | --- | --- | --- |
| `Roboto-Medium.ttf` | [Roboto Medium](https://fonts.google.com/specimen/Roboto) | Christian Robertson (Google Fonts) | Apache-2.0 | all |
| `ogbox-logo.png` | [OGBox logo](https://github.com/Press5elect/OGBox) (none; shown scaled until the game's first frame) | Press5elect | GPL-3.0-or-later | all |
