if {[catch {package require fileutil}]} {
    puts stderr "Error: OSVVM requires the Tcllib fileutil package."
    puts stderr "       Install Tcllib (for example: apt-get install tcllib) and retry."
    exit 1
}

# OSVVM appends every build to index.yml in its checkout and writes index.html
# from it.  A build that failed earlier leaves an entry without its counts, and
# the report of every later build then fails on it, so start from a fresh index.
file delete -force index.yml index.html

source "Scripts/StartNVC.tcl"
set ::osvvm::VhdlLibraryDirectory "${::env(NVC_INSTALL_DEST)}"
set ::osvvm::VhdlLibrarySubdirectory "."
set ::osvvm::AnalyzeErrorStopCount 1

foreach std {2008 2019} {
    SetVHDLVersion $std
    build OsvvmLibraries
}
