# The only place klogg version numbers are defined. Change a number here, commit
# it, and local builds and CI agree - nothing is stamped from git or the CI run
# counter, so the same commit always builds the same version.
#
# KLOGG_UI_VERSION  Qt UI binary (klogg, klogg_grep) and the packages shipping it
# KLOGG_APP_VERSION zero-Qt server and the Electron installer
# KLOGG_API_VERSION Core API / HTTP-JSON protocol revision, bump on API changes
set(KLOGG_UI_VERSION "26.11.28")
set(KLOGG_APP_VERSION "26.11.28")
set(KLOGG_API_VERSION "1.0")
