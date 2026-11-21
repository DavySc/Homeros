# A SystemVerilog project for Homeros (plan phase 1): `nix develop` puts the
# language server, the formatter, two simulators, synthesis and the wave
# viewer on PATH, where Homeros finds them.  A new project:
#   cp -r src/lsp/systemverilog/. ~/my-design && cd ~/my-design && git init && git add . && nix develop
# Its .slang/ marks the project root for slang-server and makes :w format
# with Verible (Davy, 2026-11-21); .slang/server.json takes slang-server's
# settings (a "build" .f file elaborates the whole design).
{
  inputs.nixpkgs.url = "https://channels.nixos.org/nixos-unstable/nixexprs.tar.zst";

  outputs = { nixpkgs, ... }:
    let
      forAll = f: nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ] (s: f nixpkgs.legacyPackages.${s});
      # slang-server is not in nixpkgs: built from main as of 2026-11-19
      # (v0.3.1 and 38 commits), with nixpkgs' libraries in place of the
      # ones its CMake would download
      slang-server = pkgs: pkgs.stdenv.mkDerivation {
        pname = "slang-server";
        version = "0.3.1-unstable-2026-11-19";
        src = pkgs.fetchgit {
          url = "https://github.com/hudson-trading/slang-server";
          rev = "4349e86487d3766b77f156a01fbc695e4854007f";
          fetchSubmodules = true;
          hash = "sha256-MxIDTrLWsp/UQekjXpf9bl8/2ak1yZ7CCFIorbEklbg=";
        };
        nativeBuildInputs = with pkgs; [ cmake ninja python3 ];
        buildInputs = with pkgs; [ fmt boost tomlplusplus uriparser ];
        cmakeFlags = [
          "-DSLANG_SERVER_USE_SYSTEM_FMT=ON" "-DSLANG_SERVER_USE_SYSTEM_URIPARSER=ON"
          "-DSLANG_USE_SYSTEM_FMT=ON" "-DSLANG_USE_SYSTEM_BOOST=ON" "-DSLANG_USE_SYSTEM_TOMLPLUSPLUS=ON"
          "-DSLANG_USE_MIMALLOC=OFF" "-DSLANG_SERVER_INCLUDE_TESTS=OFF"
          # its binary goes to the build tree's bindir, which nix sets to $out/bin
          "-DCMAKE_INSTALL_BINDIR=bin"
        ];
        ninjaFlags = [ "slang_server" ];
        installPhase = "install -Dm755 bin/slang-server $out/bin/slang-server";
      };
    in {
      packages = forAll (pkgs: { slang-server = slang-server pkgs; });
      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          packages = [ (slang-server pkgs) ] ++ (with pkgs; [ verible iverilog verilator yosys surfer ]);
        };
      });
    };
}
