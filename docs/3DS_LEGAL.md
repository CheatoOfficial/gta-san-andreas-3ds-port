# Legal notes

This is not legal advice. It summarises the rules this repository follows.

* The code here is a derived work of PS2RC and follows the [PS2RC License 1.2](../LICENSE): free of charge, credit
  to the original recompiler (see the README), the PS2RC code stays under that license, and **no game data**.
* "Game data" includes anything taken or produced from a game disc: program code in original, translated or
  generated form (the C that PS2RC generates is game data), graphics, models, sound, video, text, scripts, maps,
  data tables, and **links to copies of any of it**.
* Raw runtime logs and captures count as game-derived: share only the output of `3ds-port/scripts/summarize-log.py`.
* Therefore: no disc images, no generated sources, no staged `disc/` folders, no 3DSX/ELF builds made from them, no
  logs or screenshots that show game content, and no pointers to where disc images can be downloaded. To use this
  port you dump a disc you own and convert it on your own machine.
* Facts needed to work with a game (function addresses, sizes, and the checksum that identifies a disc) are allowed
  by the license; you remain responsible for what you publish.
* The project and app are named after the game only to say what they work with. They are unofficial: do not use the
  name, icon or artwork in a way that suggests endorsement, and do not add the game's artwork or logos here.
* The 3DS button/graphics conventions follow public references (libctru, Citro3D, and observations of the
  REGTA 3DS port's mappings). No REGTA code is included. Third-party code keeps its own license (see NOTICE.md).
* Rights holders with concerns: see `TAKEDOWN.md` and open an issue.

Before every commit run `python 3ds-port/scripts/check-no-game-data.py` (CI does too).
