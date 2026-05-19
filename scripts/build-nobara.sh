#!/usr/bin/env bash
# ======================================================================
#  RuView Autonomous Build Script — Nobara/Fedora
#
#  Builds the full pipeline in dependency order:
#    1. System packages (dnf)
#    2. Rust toolchain + workspace
#    3. Python venv + dependencies
#    4. ESP-IDF v5.3.2 (if not present)
#    5. ESP32-S3 firmware (8MB + 4MB targets)
#    6. Trust kill switch verification
#
#  Usage:
#    ./scripts/build-nobara.sh [--skip-firmware] [--skip-rust]
#                               [--skip-python] [--flash /dev/ttyUSB0]
#                               [--yes] [--help]
#
#  Flags:
#    --skip-firmware   Skip ESP32 firmware build (no ESP-IDF required)
#    --skip-rust       Skip Rust workspace build
#    --skip-python     Skip Python venv + deps
#    --flash PORT      Flash firmware after build (e.g. /dev/ttyUSB0)
#    --yes             Non-interactive (auto-confirm all prompts)
#    --help            Show this help
# ======================================================================

set -euo pipefail

PROJ_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUST_DIR="$PROJ_DIR/rust-port/wifi-densepose-rs"
VENV_DIR="$PROJ_DIR/.venv"
IDF_DIR="$HOME/esp/esp-idf"
IDF_VERSION="v5.3.2"
LOG_FILE="$PROJ_DIR/.build-nobara.log"

# ─── Flags ────────────────────────────────────────────────────────────
SKIP_FIRMWARE=false
SKIP_RUST=false
SKIP_PYTHON=false
FLASH_PORT=""
YES=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-firmware) SKIP_FIRMWARE=true; shift ;;
        --skip-rust)     SKIP_RUST=true; shift ;;
        --skip-python)   SKIP_PYTHON=true; shift ;;
        --flash)         FLASH_PORT="$2"; shift 2 ;;
        --yes|-y)        YES=true; shift ;;
        --help|-h)
            sed -n '3,20p' "${BASH_SOURCE[0]}" | sed 's/^#  \?//'
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ─── Colors ───────────────────────────────────────────────────────────
if [ -t 1 ]; then
    R='\033[0;31m' G='\033[0;32m' Y='\033[1;33m'
    C='\033[0;36m' B='\033[0;34m' W='\033[1m' D='\033[0m'
else
    R='' G='' Y='' C='' B='' W='' D=''
fi

log()  { echo -e "$1" | tee -a "$LOG_FILE"; }
step() { log "\n${C}[${1}]${D} ${W}${2}${D}"; }
ok()   { log "  ${G}OK${D}    $1"; }
warn() { log "  ${Y}WARN${D}  $1"; }
fail() { log "  ${R}FAIL${D}  $1"; }
info() { log "       $1"; }

confirm() {
    $YES && return 0
    read -rp "  $1 [Y/n]: " yn
    [[ ! "$yn" =~ ^[Nn] ]]
}

# ─── Init ─────────────────────────────────────────────────────────────
echo "RuView Nobara build — $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$LOG_FILE"

echo ""
echo -e "${W}======================================================================"
echo "  RuView Full Pipeline Build — Nobara/Fedora"
echo -e "======================================================================${D}"
echo ""

# ─── Step 1: System packages ──────────────────────────────────────────
step "1/6" "System Packages"

PKGS_NEEDED=()
for pkg in gcc gcc-c++ gcc-fortran make cmake git python3 python3-pip \
           openblas-devel pkgconf-pkg-config perl-FindBin perl-File-Compare \
           perl-File-Copy perl-lib; do
    rpm -q "$pkg" &>/dev/null || PKGS_NEEDED+=("$pkg")
done

# Rust needs libssl-dev equivalent
rpm -q openssl-devel &>/dev/null || PKGS_NEEDED+=("openssl-devel")

if [ ${#PKGS_NEEDED[@]} -gt 0 ]; then
    log "  Missing packages: ${PKGS_NEEDED[*]}"
    if confirm "Install via dnf?"; then
        sudo dnf install -y "${PKGS_NEEDED[@]}" 2>&1 | tee -a "$LOG_FILE" | tail -5
        ok "System packages installed"
    else
        warn "Skipped package install — build may fail"
    fi
else
    ok "All system packages present"
fi

# User dialout group (for serial access to ESP32)
if ! id -nG | grep -qw dialout; then
    warn "User not in dialout group — serial access will fail"
    if confirm "Add $(whoami) to dialout group?"; then
        sudo usermod -aG dialout "$(whoami)"
        ok "Added to dialout — re-login required for effect"
    fi
fi

# ─── Step 2: Rust workspace ───────────────────────────────────────────
if $SKIP_RUST; then
    step "2/6" "Rust workspace — SKIPPED"
else
    step "2/6" "Rust Workspace"

    # Rustup + stable toolchain
    if ! command -v rustc &>/dev/null; then
        if confirm "Rust not found — install via rustup?"; then
            curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path
            source "$HOME/.cargo/env"
            ok "Rustup installed"
        else
            fail "Rust required. Exiting."; exit 1
        fi
    else
        ok "Rust: $(rustc --version)"
    fi

    # Ensure cargo is on PATH
    if ! command -v cargo &>/dev/null; then
        source "$HOME/.cargo/env"
    fi

    # Check OpenBLAS (needed by ndarray-linalg)
    if ! pkg-config --exists openblas 2>/dev/null; then
        warn "OpenBLAS pkg-config not found; ndarray-linalg may fail"
        info "Install: sudo dnf install openblas-devel"
    else
        ok "OpenBLAS: $(pkg-config --modversion openblas)"
    fi

    # Build Rust workspace (release)
    log "\n  Building Rust workspace (this takes ~5 min first time)..."
    if (cd "$RUST_DIR" && cargo build --release --workspace 2>&1 | tee -a "$LOG_FILE" | grep -E "^(Compiling|Finished|error)" | tail -20); then
        ok "Rust workspace built"
    else
        fail "Rust build failed — check $LOG_FILE"
        exit 1
    fi

    # Verify key binaries
    for bin in sensing-server wifi-densepose; do
        if [ -f "$RUST_DIR/target/release/$bin" ]; then
            sz=$(du -h "$RUST_DIR/target/release/$bin" | cut -f1)
            ok "$bin  ($sz)"
        else
            warn "$bin binary not found (may be expected if crate not in workspace)"
        fi
    done

    # Run Rust tests (no-default-features avoids GPU/libtorch deps)
    log "\n  Running Rust tests..."
    if (cd "$RUST_DIR" && cargo test --workspace --no-default-features 2>&1 | tee -a "$LOG_FILE" | tail -10); then
        ok "Rust tests passed"
    else
        warn "Some Rust tests failed — check $LOG_FILE"
    fi
fi

# ─── Step 3: Python venv ──────────────────────────────────────────────
if $SKIP_PYTHON; then
    step "3/6" "Python venv — SKIPPED"
else
    step "3/6" "Python Environment"

    if ! command -v python3 &>/dev/null; then
        fail "python3 not found"; exit 1
    fi
    ok "Python: $(python3 --version)"

    # Create venv
    if [ ! -d "$VENV_DIR" ]; then
        python3 -m venv "$VENV_DIR"
        ok "Created venv at $VENV_DIR"
    else
        ok "Venv already exists"
    fi

    source "$VENV_DIR/bin/activate"

    # Core install (numpy + scipy for verification, then full requirements)
    pip install --upgrade pip --quiet 2>&1 | tail -2
    pip install numpy scipy --quiet 2>&1 | tail -2
    ok "numpy + scipy installed (verification ready)"

    if [ -f "$PROJ_DIR/requirements.txt" ]; then
        log "  Installing requirements.txt..."
        pip install -r "$PROJ_DIR/requirements.txt" --quiet 2>&1 | tail -5
        ok "requirements.txt installed"
    fi

    # Install package in dev mode
    if [ -f "$PROJ_DIR/pyproject.toml" ]; then
        pip install -e "$PROJ_DIR" --quiet 2>&1 | tail -3
        ok "Package installed in dev mode"
    fi

    # Run trust kill switch
    log "\n  Running trust kill switch (ADR-028)..."
    if python "$PROJ_DIR/v1/data/proof/verify.py" 2>&1 | tee -a "$LOG_FILE" | grep -E "VERDICT|PASS|FAIL"; then
        ok "Trust kill switch passed"
    else
        warn "Trust kill switch returned unexpected output"
    fi

    deactivate
fi

# ─── Step 4: ESP-IDF setup ────────────────────────────────────────────
if $SKIP_FIRMWARE; then
    step "4/6" "ESP-IDF — SKIPPED (--skip-firmware)"
else
    step "4/6" "ESP-IDF ${IDF_VERSION}"

    IDF_READY=false

    # Check existing install
    if [ -f "$IDF_DIR/export.sh" ]; then
        INSTALLED_VER=$(cd "$IDF_DIR" && git describe --tags --abbrev=0 2>/dev/null || echo "unknown")
        if [ "$INSTALLED_VER" = "$IDF_VERSION" ]; then
            ok "ESP-IDF $IDF_VERSION already installed at $IDF_DIR"
            IDF_READY=true
        else
            warn "ESP-IDF at $IDF_DIR is $INSTALLED_VER (want $IDF_VERSION)"
        fi
    elif [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
        ok "ESP-IDF found via IDF_PATH=$IDF_PATH"
        IDF_DIR="$IDF_PATH"
        IDF_READY=true
    fi

    if ! $IDF_READY; then
        info "ESP-IDF $IDF_VERSION not found"
        if confirm "Clone and install ESP-IDF $IDF_VERSION? (~600 MB)"; then
            mkdir -p "$HOME/esp"
            if [ -d "$IDF_DIR" ]; then
                warn "Stale IDF_DIR exists — removing"
                rm -rf "$IDF_DIR"
            fi
            git clone --recursive --branch "$IDF_VERSION" --depth 1 \
                https://github.com/espressif/esp-idf.git "$IDF_DIR" 2>&1 | tail -5
            # Install tools for esp32s3
            "$IDF_DIR/install.sh" esp32s3 2>&1 | tail -10
            ok "ESP-IDF $IDF_VERSION installed"
            IDF_READY=true
        else
            warn "Skipping ESP-IDF — firmware build will be skipped"
        fi
    fi
fi

# ─── Step 5: Firmware build ───────────────────────────────────────────
if $SKIP_FIRMWARE; then
    step "5/6" "Firmware — SKIPPED (--skip-firmware)"
else
    step "5/6" "ESP32-S3 Firmware"

    if ! $IDF_READY; then
        warn "ESP-IDF not ready — skipping firmware build"
    else
        FIRMWARE_DIR="$PROJ_DIR/firmware/esp32-csi-node"

        # Source ESP-IDF environment in a subshell to avoid polluting current env
        build_firmware() {
            local TARGET_NAME="$1"   # e.g. "8mb" or "4mb"
            local DEFAULTS_FILE="$2" # e.g. "sdkconfig.defaults" or "sdkconfig.defaults.4mb"

            log "\n  Building firmware: $TARGET_NAME..."

            (
                # Source IDF tools
                source "$IDF_DIR/export.sh" &>/dev/null

                cd "$FIRMWARE_DIR"

                # Switch to correct sdkconfig.defaults
                if [ "$DEFAULTS_FILE" != "sdkconfig.defaults" ]; then
                    cp "$DEFAULTS_FILE" sdkconfig.defaults
                fi

                # Remove stale sdkconfig to force regeneration from defaults
                rm -f sdkconfig

                idf.py set-target esp32s3 build 2>&1 | tee -a "$LOG_FILE" | \
                    grep -E "^(Project|Generating|Linking|Binary|esptool|error|warning)" | tail -20
            )

            if [ -f "$FIRMWARE_DIR/build/esp32-csi-node.bin" ]; then
                sz=$(du -h "$FIRMWARE_DIR/build/esp32-csi-node.bin" | cut -f1)
                ok "Firmware $TARGET_NAME built  ($sz)"
                # Save a copy
                cp "$FIRMWARE_DIR/build/esp32-csi-node.bin" \
                   "$PROJ_DIR/releases/esp32-csi-node-${TARGET_NAME}.bin" 2>/dev/null || true
            else
                warn "Build output not found — check $LOG_FILE"
            fi
        }

        mkdir -p "$PROJ_DIR/releases"

        # 8MB target (primary — uses sdkconfig.defaults)
        build_firmware "8mb" "sdkconfig.defaults"

        # 4MB target
        if [ -f "$FIRMWARE_DIR/sdkconfig.defaults.4mb" ]; then
            build_firmware "4mb" "sdkconfig.defaults.4mb"
            # Restore primary sdkconfig
            cp "$FIRMWARE_DIR/sdkconfig.defaults.template" \
               "$FIRMWARE_DIR/sdkconfig.defaults" 2>/dev/null || true
        fi

        # Flash if requested
        if [ -n "$FLASH_PORT" ]; then
            if [ ! -c "$FLASH_PORT" ]; then
                warn "Flash port $FLASH_PORT not found — skipping flash"
            elif confirm "Flash firmware to $FLASH_PORT?"; then
                log "\n  Flashing to $FLASH_PORT..."
                (
                    source "$IDF_DIR/export.sh" &>/dev/null
                    cd "$FIRMWARE_DIR"
                    idf.py -p "$FLASH_PORT" flash 2>&1 | tee -a "$LOG_FILE" | tail -10
                )
                ok "Firmware flashed to $FLASH_PORT"
            fi
        fi
    fi
fi

# ─── Step 6: Final verification ───────────────────────────────────────
step "6/6" "Summary"

SENSING_BIN="$RUST_DIR/target/release/sensing-server"
PYTHON_VERIFY="$PROJ_DIR/v1/data/proof/verify.py"

echo ""
echo -e "${W}  Build artifacts:${D}"
[ -f "$SENSING_BIN" ] && info "  sensing-server   $SENSING_BIN" || info "  sensing-server   NOT BUILT"
[ -d "$VENV_DIR" ] && info "  Python venv      $VENV_DIR" || info "  Python venv      NOT CREATED"
ls "$PROJ_DIR/releases/"*.bin 2>/dev/null | while read f; do
    info "  Firmware         $f"
done

echo ""
echo -e "${W}  Next steps:${D}"
echo ""

if [ -f "$SENSING_BIN" ]; then
    echo "  # Start sensing server (ESP32 → UDP:5005 → WebSocket:3001):"
    echo "  $SENSING_BIN --source auto --tick-ms 100 \\"
    echo "    --ui-path $PROJ_DIR/ui --http-port 3000 --ws-port 3001"
    echo ""
fi

echo "  # Install systemd services (auto-start on boot):"
echo "  bash $PROJ_DIR/scripts/systemd/install-systemd.sh"
echo ""
echo "  # Record live CSI from ESP32 (must be streaming to UDP:5005):"
echo "  python scripts/record-csi-udp.py --duration 300 --output data/recordings"
echo ""
echo "  # Provision ESP32 WiFi (Linux serial port):"
echo "  python $PROJ_DIR/firmware/esp32-csi-node/provision.py \\"
echo "    --port /dev/ttyUSB0 --ssid 'YourWiFi' --password 'secret' \\"
echo "    --target-ip \$(hostname -I | awk '{print \$1}')"
echo ""
echo "  # Log: $LOG_FILE"
echo ""
echo -e "${W}======================================================================${D}"
echo ""
