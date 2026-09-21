#pragma once

// Application: Core 0 startup and per-pass control loop.
// Musical role: keeps the box playable — boots the saved song, then scans hands,
// drains clock ticks, and refreshes lights/display every pass. Runs on Core 0 only;
// never block here (flash I/O is staged) or the groove stutters.
namespace Application
{
void begin();
void update();
}
