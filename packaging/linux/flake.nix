{
  description = "Move a computer's environment to one running another operating system";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "transmit";
          version = "0.1.0";
          src = ../..;

          nativeBuildInputs = with pkgs; [ cmake ninja pkg-config qt6.wrapQtAppsHook ];

          buildInputs = with pkgs; [
            qt6.qtbase qt6.qtdeclarative
            zstd xz openssl sqlite libsecret
          ];

          cmakeFlags = [ "-DTRANSMIT_BUILD_TESTS=ON" ];

          doCheck = true;
          checkPhase = "ctest --output-on-failure";

          meta = with pkgs.lib; {
            description = "Move a computer's environment to one running another operating system";
            homepage = "https://github.com/neramc/transmit";
            license = licenses.agpl3Plus;
            platforms = platforms.linux;
            mainProgram = "transmit";
          };
        };
      });

      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.system}.default ];
          packages = with pkgs; [ clang-tools gdb ];
        };
      });
    };
}
