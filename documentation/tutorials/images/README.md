# Tutorial Image Maintenance

Captured from the working editor on 2026-09-07, using the D3D12 backend at 1440x900.
These are application framebuffer images, not mockups or generated illustrations.

## Regeneration

From the source root containing the solution:

```powershell
.\scripts\build.ps1 -Config Debug -Platform x64 -Action Build
.\scripts\CaptureEditorTutorials.ps1 -Config Debug -Force
```

For a targeted update, supply names such as `-Steps grid,footprint,play`. The script
uses `--tutorial-step`, waits for the editor to render the configured state, and
converts its PPM framebuffer into PNG without changing image contents. It rejects
engine errors, missing ready markers, malformed output, and uniform captures.

Capture runs use a transient layout, suppress interactive input, and modify only
their own in-memory/temporary scene data. Play uses the real runtime in the main
capture viewport, while normal editor sessions retain their separate hosted window.
The helper is not enabled unless explicitly requested on the command line.

The optional model lesson requires a locally supplied reference imported as
`Models/PlacementBuilding.glb`. It is not part of the default capture set or
distributed with the repository. Its regression fixture expects 92 joints, 17
clips, the `Stand Work`/`Stand` clips, and a helper shape at geometry index zero.

```powershell
.\scripts\CaptureEditorTutorials.ps1 -Steps model-assign,model-parts,model-animation,model-reloaded,model-play -Force
```

Use `-Api vulkan` (or `d3d11`/`gl`) and a separate `-OutputRoot` for API smoke
evidence. Do not overwrite the documented D3D12 images with a different backend.
Model checks include fitted bounds, compatible material/skinning variants,
independent bone movement, invalid asset/clip/part rejection, Clear Model undo/redo,
reload, and animated hosted Play. Inspect the images as well: a nonuniform frame
does not prove that its geometry or materials rendered correctly.

## Maintenance Rules

- Keep the image next to the step it actually demonstrates.
- Verify displayed values and status messages after regenerating.
- Do not substitute a final scene picture for an import/settings/error step.
- Keep captions synchronized with visible control names.
- Keep named external comparisons and external source links out of the lessons.
- Update the documented capture date when behavior or framing changes.

Return to the [tutorial index](../README.md).