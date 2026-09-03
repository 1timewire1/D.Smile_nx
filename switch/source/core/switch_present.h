#pragma once

// Presents the current frame (main.cpp's eglSwapBuffers call), exposed so
// switch_dirbrowse.cpp - which runs its own blocking input/render loop
// separate from main.cpp's - can drive the same display/surface without
// main.cpp needing to hand out its EGLDisplay/EGLSurface directly.
void switch_present();
