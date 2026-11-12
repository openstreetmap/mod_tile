{
  pkgs ? import <nixpkgs> { },
}:

let
  mod_tile = import ./default.nix { inherit pkgs; };

in
pkgs.mkShell {
  inputsFrom = [ mod_tile ];

  packages = with pkgs; [
    clang-tools
    git
  ];
}
