# Wild 9 Temporal Frame Blending

This mod leaves Wild 9's executable, VSync waits, simulation, physics, timers,
input, and audio untouched. It combines the two most recent completed game
frames in PSXrecomp's OpenGL presentation path at 120 FPS (or 60, 144, 165, or
the measured display refresh rate).

Use 120 FPS on a 120 Hz or faster display. Presentation and display refresh are
separate: on a 60 Hz monitor the presenter still blends, but the compositor
only shows 60 of those presents per second.

The motion-adaptive clarity blend avoids crossfading large pixel changes to
reduce double-image trails on Wild 9's fast sprite and missile motion. It is
presentation-only temporal blending, not motion-vector frame generation, so it
cannot reconstruct true in-between positions.
