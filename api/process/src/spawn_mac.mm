// macOS usa la MISMA base fork/exec que Linux.
// VERIFICAR-EN-MACOS: execvpe no existe → env con setenv como en Linux.
#include "spawn_linux.cpp"
