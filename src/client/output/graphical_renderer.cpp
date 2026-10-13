// B534 (#181): the output-layer camera vocabulary is GONE with it. This TU
// used to hold the class's only two non-virtual methods (the cycle over the
// old Free/Follow/First-Person triple and the string form of it); both are
// deleted. Nothing derives from this class (T241), and the live camera route
// is a DIFFERENT vocabulary over the official six-mode set:
//   F9 -> HotkeyAction/RendererAction hotkey -> the PlayerCameraComponent
//   cycle (T461); /camera cycle reaches the same component
//   (renderer_slash_commands.cpp). This TU now carries no code: the class
//   has no out-of-line methods, so the file is a comment alone.
