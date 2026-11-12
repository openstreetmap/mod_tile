{
  apacheHttpd,
  apr,
  aprutil,
  boost,
  cairo,
  cmake,
  curl,
  glib,
  harfbuzz,
  iana-etc,
  icu,
  iniparser,
  jq,
  lib,
  libmemcached,
  mapnik,
  memcached,
  pkg-config,
  ps,
  stdenv,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "mod_tile";

  version =
    let
      versionLine = builtins.head (
        builtins.filter (
          line: builtins.match ''[ ]*VERSION[ ]*[0-9]+\.[0-9]+(\.[0-9]+)?[ ]*'' line != null
        ) (lib.splitString "\n" (builtins.readFile ../CMakeLists.txt))
      );
    in
    builtins.elemAt (builtins.match "[ ]*VERSION[ ]*([0-9.]+)[ ]*" versionLine) 0;

  src = lib.cleanSourceWith {
    src = ../.;
    filter = path: _type: baseNameOf path != ".git";
  };

  strictDeps = true;

  nativeBuildInputs = [
    apacheHttpd
    cmake
    curl
    pkg-config
  ];

  buildInputs = [
    apacheHttpd
    apr
    aprutil
    boost
    cairo
    curl
    glib
    harfbuzz
    icu
    iniparser
    libmemcached
    mapnik
  ];

  enableParallelBuilding = true;

  cmakeFlags = [
    (lib.cmakeFeature "CMAKE_INSTALL_BINDIR" "bin")
    (lib.cmakeFeature "CMAKE_INSTALL_MANDIR" "share/man")
    (lib.cmakeFeature "CMAKE_INSTALL_MODULESDIR" "modules")
    (lib.cmakeFeature "CMAKE_INSTALL_PREFIX" "")
    (lib.cmakeBool "ENABLE_TESTS" finalAttrs.finalPackage.doCheck)
  ];

  installFlags = [ "DESTDIR=${placeholder "out"}" ];

  doCheck = true;
  enableParallelChecking = false;

  nativeCheckInputs = [
    iana-etc
    jq
    memcached
    ps
  ];

  meta = {
    description = "Efficiently render and serve OpenStreetMap tiles using Apache and Mapnik";
    homepage = "https://github.com/openstreetmap/mod_tile";
    license = lib.licenses.gpl2Plus;
    platforms = lib.platforms.linux;
  };
})
