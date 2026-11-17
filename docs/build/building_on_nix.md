# Building on Nix

This document provides users with step-by-step instructions on how to compile and use `mod_tile` and `renderd`.

Please see our [Continuous Integration script](/.github/workflows/build-and-test.yml) for more details.

Rather than maintaining separate Nix packaging files in this repository, you can build and test your local checkout directly against the upstream [`nixpkgs`](https://github.com/NixOS/nixpkgs) derivation (`apacheHttpdPackages.mod_tile`), which includes all required `build` and `test` dependencies:

```shell
# Build and run the test suite in an isolated Nix sandbox
nix-build -E '
  with import <nixpkgs> {};
  apacheHttpdPackages.mod_tile.overrideAttrs (_: {
    version =
      let
        versionLine = builtins.head (
          builtins.filter (
            line: builtins.match "[ ]*VERSION[ ]*[0-9]+\\.[0-9]+(\\.[0-9]+)?[ ]*" line != null
          ) (lib.splitString "\n" (builtins.readFile ./CMakeLists.txt))
        );
      in
      builtins.elemAt (builtins.match "[ ]*VERSION[ ]*([0-9.]+)[ ]*" versionLine) 0;
    src = lib.cleanSourceWith {
      src = ./.;
      filter = path: _type: baseNameOf path != ".git";
    };
  })
'

# Set up writable runtime directories (since /nix/store is read-only)
mkdir --parents \
  /tmp/mod_tile/etc \
  /tmp/mod_tile/var/run/renderd \
  /tmp/mod_tile/var/cache/renderd/tiles

# Create renderd configuration
nix-shell -p gnused --run ' \
sed \
  --expression "s|var/|/tmp/mod_tile/var/|g" \
  result/etc/renderd.conf > \
  /tmp/mod_tile/etc/renderd.conf \
'

cat << EOF >> /tmp/mod_tile/etc/renderd.conf

[example-map]
URI=/tiles/renderd-example
XML=$(pwd)/utils/example-map/mapnik.xml

[example-map-jpg]
TYPE=jpg image/jpeg jpeg
URI=/tiles/renderd-example-jpg
XML=$(pwd)/utils/example-map/mapnik.xml

[example-map-png256]
TYPE=png image/png png256
URI=/tiles/renderd-example-png256
XML=$(pwd)/utils/example-map/mapnik.xml

[example-map-png32]
TYPE=png image/png png32
URI=/tiles/renderd-example-png32
XML=$(pwd)/utils/example-map/mapnik.xml

[example-map-webp]
TYPE=webp image/webp webp
URI=/tiles/renderd-example-webp
XML=$(pwd)/utils/example-map/mapnik.xml
EOF

# Create apache configuration
nix-shell -p gnused --run ' \
sed \
  --expression "s|/etc/renderd.conf|/tmp/mod_tile/etc/renderd.conf|g" \
  --expression "s|/run/renderd/|/tmp/mod_tile/var/run/renderd/|g" \
  --expression "s|/usr/share/renderd/example-map|$(pwd)/utils/example-map|g" \
  --expression "s|/var/cache/renderd/|/tmp/mod_tile/var/cache/renderd/|g" \
  etc/apache2/renderd-example-map.conf > \
  /tmp/mod_tile/etc/renderd-example-map.conf \
'

# Start Apache httpd and renderd using the built Nix packages
nix-shell --pure -E '
  with import <nixpkgs> {};
  let
    mod_tile = ./result;
    httpdWrapped = symlinkJoin {
      name = "httpd-with-mod_tile";
      paths = [ apacheHttpd ];
      nativeBuildInputs = [ makeWrapper ];
      postBuild = '\'''\''
        rm -f $out/conf/httpd.conf
        grep -Ev "^(User|Group)[[:space:]]" ${apacheHttpd}/conf/httpd.conf > $out/conf/httpd.conf
        wrapProgram $out/bin/httpd \
          --add-flags "-f $out/conf/httpd.conf" \
          --add-flags "-c \"PidFile /tmp/mod_tile/var/run/httpd.pid\"" \
          --add-flags "-c \"ErrorLog /tmp/mod_tile/var/run/error_log\"" \
          --add-flags "-c \"CustomLog /tmp/mod_tile/var/run/access_log common\"" \
          --add-flags "-c \"LoadModule tile_module ${mod_tile}/modules/mod_tile.so\"" \
          --add-flags "-c \"Include /tmp/mod_tile/etc/renderd-example-map.conf\""
      '\'''\'';
    };
  in
  mkShell {
    packages = [
      mod_tile
      httpdWrapped
    ];
  }
' --run '
  httpd
  renderd --config /tmp/mod_tile/etc/renderd.conf --foreground
'
```

Then you can visit: `http://localhost:8081/renderd-example-map`

_(Note: Because `apacheHttpd`'s default configuration binds port 80, you will need to run `httpd` with `sudo` when running unprivileged)._

## Interactive Development Shell

If you prefer to build interactively inside a development shell with all build and test dependencies available:

```shell
nix-shell '<nixpkgs>' -A apacheHttpdPackages.mod_tile
cmake -B build -S . -DENABLE_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
