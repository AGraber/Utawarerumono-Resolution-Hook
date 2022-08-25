# Utawarerumono 3D Resolution Mod
This mod removes the lock on resolution that forces the internal resolution to be 1080p downscaled to 720p, and makes it scale with the selected resolution from the settings menu.
The default for the 3d resolution is now double the selected resolution, to achieve a a nice supersampling effect. It supports all the three Utawarerumono games on Steam.

This mod only affects the 3d segments of the game: the VN segments are left intact.

# Configuration
The default supersampling rate is 2.0 (which means double the current resolution).
Most won't need to configure the mod, since the game's 3d segments are extremely lightweight compared to modern games, and scaling it even more than that doesn't really produce a better result.
Nonetheless, if you want to change the default supersampling rate, you have 2 options:

## As a launch parameter
The game will take a `--supersampling=[value]` value as a launch parameter.
For example, you can set this on the Launch Options field under Properties on Steam to force the 3d resolution to exactly match the one from the settings (1.0):

```
--supersampling=1.0
```

## Text file next to the .exe
Create a text alongside the .exe named `supersampling.txt`, and put the desired value on it as its only contents, eg.:
```
1.0
```

# Known caveats
- Various effects, like camera cuts/fades and portrait fade in/out are still 720p. I didn't manage to come up with a solution for those since it's a bunch of textures to resize: if someone knowledgable has any tips I'll gladly take try to implement them.
- Some UI textures might have visible seams and/or artifacts on 3d segments, as they're now being rendered to a higher render target than normal.
- Some overlays really don't like this mod (due to the way I hook some stuff), and will either display incorrectly or outright crash while rendering. I managed to prevent the Steam Overlay from malfunctioning, but keep this in mind if you have any other software running on top of the games.
- The source code is a bit messy

# Acknowledgements
Thanks to:
- [Vimiani](https://github.com/vimiani) for early testing and feedback
- [Tsuda Kageyu](https://github.com/TsudaKageyu) and [contributors](https://github.com/TsudaKageyu/minhook/graphs/contributors) of [MinHook - The Minimalistic x86/x64 API Hooking Library for Windows](https://github.com/TsudaKageyu/minhook)
