/*
 *  cosmo — Version: the one place the app's version string comes from (R-UX-5).
 *
 *  `COSMO_VERSION` is defined by the build (apps/cosmo/CMakeLists.txt sets it once and hands it
 *  to the sources that show it). The fallback below is for a hand-rolled compile that passes no
 *  define (the Android script): it says "dev" rather than pretending to be a release.
 */
#pragma once

#ifndef COSMO_VERSION
#define COSMO_VERSION "dev"
#endif
