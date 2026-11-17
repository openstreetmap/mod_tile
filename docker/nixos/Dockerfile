# hadolint global ignore=DL3025,DL3059
# Arguments
ARG nix_version=latest
ARG nixpkgs_channel=nixpkgs-unstable

# Base
FROM nixos/nix:${nix_version} AS base

## Arguments
ARG nix_version
ARG nixpkgs_channel

## Configure Nix channel
RUN nix-channel --add https://nixos.org/channels/${nixpkgs_channel} nixpkgs && \
    nix-channel --update

# Builder
FROM base AS builder

## Arguments
ARG nix_version
ARG nixpkgs_channel

## Build, Test & Install `mod_tile`
COPY . /tmp/mod_tile_src
WORKDIR /tmp/mod_tile_src
RUN nix-build --out-link /tmp/mod_tile_env - <<'EOF'
with import <nixpkgs> {};
let
  mod_tile = apacheHttpdPackages.mod_tile.overrideAttrs (_: {
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
  });
  httpdWrapped = symlinkJoin {
    name = "httpd-with-mod_tile";
    paths = [ apacheHttpd ];
    nativeBuildInputs = [ makeWrapper ];
    postBuild = ''
      rm -f $out/conf/httpd.conf
      grep -Ev "^(User|Group)[[:space:]]" ${apacheHttpd}/conf/httpd.conf > $out/conf/httpd.conf
      wrapProgram $out/bin/httpd \
        --add-flags "-f $out/conf/httpd.conf" \
        --add-flags "-c \"CustomLog /var/log/httpd/access_log common\"" \
        --add-flags "-c \"ErrorLog /var/log/httpd/error_log\"" \
        --add-flags "-c \"Include /etc/httpd/conf/extra/httpd-tile-renderd-example-map.conf\"" \
        --add-flags "-c \"LoadModule tile_module ${mod_tile}/modules/mod_tile.so\"" \
        --add-flags "-c \"PidFile /var/run/httpd.pid\""
    '';
  };
in
symlinkJoin {
  name = "mod_tile-env";
  paths = [ mod_tile httpdWrapped ];
}
EOF

## Prepare runtime configuration and Nix store
# hadolint ignore=SC2046
RUN mkdir -p /tmp/nix/store /tmp/etc && \
    cp -a $(nix-store --query --requisites /tmp/mod_tile_env) /tmp/nix/store/ && \
    nix-shell -p gnused --run ' \
      sed \
        --expression "s|var/run/renderd/|/var/run/renderd/|g" \
        --expression "s|var/cache/renderd/|/var/cache/renderd/|g" \
        /tmp/mod_tile_env/etc/renderd.conf > /tmp/etc/renderd.conf \
    '

# Runner
FROM base AS runner

## Arguments
ARG nix_version
ARG nixpkgs_channel

## Copy Nix store & files from builder
COPY --from=builder /tmp/nix/store /nix/store
COPY --from=builder /tmp/mod_tile_env /usr/local/mod_tile
COPY --from=builder /tmp/etc/renderd.conf /etc/renderd.conf
COPY --from=builder /tmp/mod_tile_src/utils/example-map /usr/share/renderd/example-map
COPY --from=builder /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf /etc/httpd/conf/extra/httpd-tile-renderd-example-map.conf

## Create runtime directories & add configuration
RUN mkdir -p /var/run/renderd /var/cache/renderd/tiles /var/log/httpd && \
    ln --symbolic /var/run /run && \
    printf '\n[example-map]\nMAXZOOM=20\nMINZOOM=0\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf && \
    printf '\n[example-map-jpg]\nMAXZOOM=20\nMINZOOM=0\nTYPE=jpg image/jpeg jpeg\nURI=/tiles/renderd-example-jpg\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf && \
    printf '\n[example-map-png256]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png256\nURI=/tiles/renderd-example-png256\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf && \
    printf '\n[example-map-png32]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png32\nURI=/tiles/renderd-example-png32\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf && \
    printf '\n[example-map-webp]\nMAXZOOM=20\nMINZOOM=0\nTYPE=webp image/webp webp\nURI=/tiles/renderd-example-webp\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf

ENV PATH="/usr/local/mod_tile/bin:${PATH}"

## Start services
CMD httpd -e debug -k start; \
    G_MESSAGES_DEBUG=${G_MESSAGES_DEBUG:-info} renderd --config /etc/renderd.conf --foreground
