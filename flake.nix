{
  description = "Delta Tel: Telegram Desktop UI on Delta Chat core";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      # Fetched from cache.nixos.org. App and core sources are not inputs,
      # so editing them does not rebuild Qt, compilers, or the other libs.
      depList = with pkgs; [
        cmake
        ninja
        pkg-config
        ccache
        python3
        git
        gnumake
        gperf
        patchelf
        gcc
        rustc
        cargo
        perl
        openssl
        zlib
        lz4
        xxhash
        libopus
        libvpx
        ffmpeg
        openal
        alsa-lib
        libpulseaudio
        gtk3
        glib
        gobject-introspection
        sysprof
        sysprof.dev
        tlottie
        (pkgs.lib.findFirst (p: (p.pname or "") == "tg_owt")
          (throw "Telegram dependency set has no tg_owt")
          pkgs.telegram-desktop.unwrapped.buildInputs)
        hunspell
        protobuf
        range-v3
        fmt
        spdlog
        ada
        minizip
        tl-expected
        rnnoise
        openh264
        rlottie
        boost
        microsoft-gsl
        pango
        libjpeg
        xz
        libheif
        libavif
        kdePackages.kcoreaddons
        libxcb
        libx11
        libxcomposite
        libxdamage
        libxext
        libxfixes
        libxrandr
        libxrender
        libxtst
        pipewire
        libdrm
        mesa
        libgbm
        wayland
        qt6.qtbase
        qt6.qtsvg
        qt6.qtwayland
        qt6.qtimageformats
        qt6.qt5compat
        qt6.qttools
        kdePackages.extra-cmake-modules
      ];
    in
    {
      packages.${system} = {
        deps = pkgs.buildEnv {
          name = "delta-tel-deps";
          paths = depList;
          ignoreCollisions = true;
        };
        default = self.packages.${system}.deps;
      };

      devShells.${system} = {
      default = pkgs.mkShell {
        packages = depList;
        # ccache and the CMake/Cargo trees live outside the Nix store so a
        # one-line edit recompiles that object only.
        shellHook = ''
          export DELTA_TEL_ROOT="''${DELTA_TEL_ROOT:-$(pwd)}"
          source "$DELTA_TEL_ROOT/nix/paths.sh"
          export CCACHE_BASEDIR="$DELTA_TEL_ROOT"
          export CCACHE_COMPRESS=1
          export DELTA_TEL_EXTRA_QT_PLUGIN_ROOTS="${pkgs.qt6.qtimageformats}/lib/qt-6/plugins:${pkgs.qt6.qtsvg}/lib/qt-6/plugins"
          export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/lib/qt-6/plugins:$DELTA_TEL_EXTRA_QT_PLUGIN_ROOTS''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
          mkdir -p "$CCACHE_DIR" "$CMAKE_BUILD_DIR" "$CARGO_TARGET_DIR" "$CARGO_HOME"
          ccache --max-size "$CCACHE_MAXSIZE" >/dev/null
          export CMAKE_C_COMPILER_LAUNCHER=ccache
          export CMAKE_CXX_COMPILER_LAUNCHER=ccache
          export PATH="$DELTA_TEL_ROOT/nix:$PATH"
        '';
      };
      # Optional GUI automation/debug tools stay out of normal CI builds.
      ui = self.devShells.${system}.default.overrideAttrs (old: {
        nativeBuildInputs = old.nativeBuildInputs ++ (with pkgs; [
          xorg-server xdotool imagemagick gdb openbox upx rustfmt clippy
        ]);
      });
      };
    };
}
