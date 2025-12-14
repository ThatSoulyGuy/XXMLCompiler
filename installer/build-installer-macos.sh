#!/bin/bash
#
# XXML Compiler macOS Installer Builder
# Creates a signed and notarized PKG installer for macOS
#
# Usage: ./build-installer-macos.sh [options]
#
# Options:
#   --skip-build        Skip building the compiler (use existing build)
#   --skip-sign         Skip code signing (for testing only)
#   --skip-notarize     Skip notarization (for local testing)
#   --version VERSION   Override version number
#   --output DIR        Output directory (default: out)
#   --help              Show this help message
#

set -e

# =============================================================================
# Configuration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INSTALLER_DIR="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_ROOT/build/release"
STAGING_DIR="$INSTALLER_DIR/staging"
SCRIPTS_DIR="$INSTALLER_DIR/scripts"
RESOURCES_DIR="$INSTALLER_DIR/resources"

# Default values
OUTPUT_DIR="$INSTALLER_DIR/out"
SKIP_BUILD=false
SKIP_SIGN=false
SKIP_NOTARIZE=false
VERSION=""

# Installation paths
INSTALL_PREFIX="/usr/local"
INSTALL_BIN="$INSTALL_PREFIX/bin"
INSTALL_LIB="$INSTALL_PREFIX/lib/xxml"
INSTALL_SHARE="$INSTALL_PREFIX/share/xxml"

# Product identifiers
BUNDLE_ID="com.xxml.compiler"
PKG_ID="com.xxml.compiler.pkg"

# =============================================================================
# Color Output
# =============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

print_banner() {
    echo -e "${CYAN}"
    echo "  ██╗  ██╗██╗  ██╗███╗   ███╗██╗"
    echo "  ╚██╗██╔╝╚██╗██╔╝████╗ ████║██║"
    echo "   ╚███╔╝  ╚███╔╝ ██╔████╔██║██║"
    echo "   ██╔██╗  ██╔██╗ ██║╚██╔╝██║██║"
    echo "  ██╔╝ ██╗██╔╝ ██╗██║ ╚═╝ ██║███████╗"
    echo "  ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝"
    echo "         macOS Installer Builder"
    echo -e "${NC}"
}

print_header() {
    echo ""
    echo -e "${CYAN}============================================================${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}============================================================${NC}"
}

print_step() {
    echo -e "${YELLOW}[*]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[+]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[!]${NC} $1"
}

print_error() {
    echo -e "${RED}[X]${NC} $1"
}

# =============================================================================
# Utility Functions
# =============================================================================

load_env() {
    local env_file="$INSTALLER_DIR/.env.local"

    if [[ -f "$env_file" ]]; then
        print_step "Loading configuration from .env.local"
        # Export variables from .env.local, ignoring comments and empty lines
        set -a
        source "$env_file"
        set +a
        print_success "Configuration loaded"
    else
        print_warn "No .env.local found - code signing will be skipped"
        print_warn "Copy .env.local.example to .env.local and configure it"
        SKIP_SIGN=true
        SKIP_NOTARIZE=true
    fi
}

get_project_version() {
    local cmake_file="$PROJECT_ROOT/CMakeLists.txt"
    if [[ -f "$cmake_file" ]]; then
        # Use [[:space:]] instead of \s for BSD sed compatibility on macOS
        grep -E 'project.*VERSION' "$cmake_file" | \
            sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/' | head -1
    else
        echo "3.0.0"
    fi
}

check_prerequisites() {
    print_header "Checking Prerequisites"

    local missing=()

    # Check for required tools
    if ! command -v cmake &> /dev/null; then
        missing+=("cmake")
    fi

    if ! command -v pkgbuild &> /dev/null; then
        missing+=("pkgbuild (Xcode Command Line Tools)")
    fi

    if ! command -v productbuild &> /dev/null; then
        missing+=("productbuild (Xcode Command Line Tools)")
    fi

    if [[ "$SKIP_SIGN" == false ]]; then
        if ! command -v codesign &> /dev/null; then
            missing+=("codesign")
        fi

        if [[ "$SKIP_NOTARIZE" == false ]]; then
            if ! command -v xcrun &> /dev/null; then
                missing+=("xcrun (Xcode)")
            fi
        fi
    fi

    if [[ ${#missing[@]} -gt 0 ]]; then
        print_error "Missing required tools:"
        for tool in "${missing[@]}"; do
            echo "    - $tool"
        done
        echo ""
        echo "Install Xcode Command Line Tools: xcode-select --install"
        exit 1
    fi

    print_success "All prerequisites found"
}

validate_signing_identity() {
    if [[ "$SKIP_SIGN" == true ]]; then
        return 0
    fi

    print_step "Validating code signing identities..."

    # Check Developer ID Application identity
    if [[ -z "$DEVELOPER_ID_APPLICATION" ]]; then
        print_error "DEVELOPER_ID_APPLICATION not set in .env.local"
        return 1
    fi

    if ! security find-identity -v -p codesigning | grep -q "$DEVELOPER_ID_APPLICATION"; then
        print_error "Certificate not found: $DEVELOPER_ID_APPLICATION"
        echo "    Available identities:"
        security find-identity -v -p codesigning | grep "Developer ID" | sed 's/^/      /'
        return 1
    fi
    print_success "Application signing identity valid"

    # Check Developer ID Installer identity
    if [[ -z "$DEVELOPER_ID_INSTALLER" ]]; then
        print_error "DEVELOPER_ID_INSTALLER not set in .env.local"
        return 1
    fi

    if ! security find-identity -v | grep -q "$DEVELOPER_ID_INSTALLER"; then
        print_error "Certificate not found: $DEVELOPER_ID_INSTALLER"
        return 1
    fi
    print_success "Installer signing identity valid"

    return 0
}

# =============================================================================
# Build Functions
# =============================================================================

build_compiler() {
    print_header "Building XXML Compiler"

    cd "$PROJECT_ROOT"

    print_step "Configuring CMake..."
    cmake -B build/release -DCMAKE_BUILD_TYPE=Release -G Ninja

    print_step "Building..."
    cmake --build build/release

    # Verify build outputs
    if [[ ! -f "$BUILD_DIR/bin/xxml" ]]; then
        print_error "Compiler binary not found: $BUILD_DIR/bin/xxml"
        exit 1
    fi

    if [[ ! -f "$BUILD_DIR/bin/xxml-lsp" ]]; then
        print_warn "LSP binary not found (optional)"
    fi

    print_success "Build complete"
}

# =============================================================================
# Staging Functions
# =============================================================================

prepare_staging() {
    print_header "Preparing Staging Directory"

    # Clean and create staging directory
    rm -rf "$STAGING_DIR"
    mkdir -p "$STAGING_DIR$INSTALL_BIN"
    mkdir -p "$STAGING_DIR$INSTALL_LIB"
    mkdir -p "$STAGING_DIR$INSTALL_SHARE"

    # Copy binaries
    print_step "Copying binaries..."
    cp "$BUILD_DIR/bin/xxml" "$STAGING_DIR$INSTALL_BIN/"

    if [[ -f "$BUILD_DIR/bin/xxml-lsp" ]]; then
        cp "$BUILD_DIR/bin/xxml-lsp" "$STAGING_DIR$INSTALL_BIN/"
    fi

    # Copy runtime library
    if [[ -f "$BUILD_DIR/lib/libXXMLLLVMRuntime.a" ]]; then
        print_step "Copying runtime library..."
        cp "$BUILD_DIR/lib/libXXMLLLVMRuntime.a" "$STAGING_DIR$INSTALL_LIB/"
    fi

    # Copy standard library
    print_step "Copying standard library..."
    if [[ -d "$PROJECT_ROOT/Language" ]]; then
        cp -R "$PROJECT_ROOT/Language" "$STAGING_DIR$INSTALL_SHARE/"
    fi

    # Copy documentation
    print_step "Copying documentation..."
    mkdir -p "$STAGING_DIR$INSTALL_SHARE/doc"

    if [[ -f "$PROJECT_ROOT/README.md" ]]; then
        cp "$PROJECT_ROOT/README.md" "$STAGING_DIR$INSTALL_SHARE/doc/"
    fi

    if [[ -d "$PROJECT_ROOT/docs" ]]; then
        cp -R "$PROJECT_ROOT/docs" "$STAGING_DIR$INSTALL_SHARE/"
    fi

    print_success "Staging directory prepared"
}

# =============================================================================
# Code Signing Functions
# =============================================================================

sign_binary() {
    local binary="$1"
    local identifier="$2"

    if [[ ! -f "$binary" ]]; then
        print_warn "Binary not found: $binary"
        return 0
    fi

    print_step "Signing: $(basename "$binary")"

    local sign_args=(
        --force
        --timestamp
        --options runtime
        --sign "$DEVELOPER_ID_APPLICATION"
    )

    if [[ -n "$identifier" ]]; then
        sign_args+=(--identifier "$identifier")
    fi

    # Add entitlements if they exist
    local entitlements="$RESOURCES_DIR/entitlements.plist"
    if [[ -f "$entitlements" ]]; then
        sign_args+=(--entitlements "$entitlements")
    fi

    codesign "${sign_args[@]}" "$binary"

    # Verify signature
    if ! codesign --verify --verbose "$binary" 2>/dev/null; then
        print_error "Signature verification failed: $binary"
        return 1
    fi

    print_success "Signed: $(basename "$binary")"
}

sign_all_binaries() {
    if [[ "$SKIP_SIGN" == true ]]; then
        print_warn "Skipping code signing (--skip-sign)"
        return 0
    fi

    print_header "Code Signing Binaries"

    # Sign main compiler
    sign_binary "$STAGING_DIR$INSTALL_BIN/xxml" "$BUNDLE_ID"

    # Sign LSP if present
    if [[ -f "$STAGING_DIR$INSTALL_BIN/xxml-lsp" ]]; then
        sign_binary "$STAGING_DIR$INSTALL_BIN/xxml-lsp" "$BUNDLE_ID.lsp"
    fi

    print_success "All binaries signed"
}

# =============================================================================
# Package Building Functions
# =============================================================================

create_distribution_xml() {
    local dist_xml="$STAGING_DIR/distribution.xml"
    local pkg_version="$VERSION"

    print_step "Creating distribution XML..."

    cat > "$dist_xml" << EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>XXML Compiler</title>
    <organization>com.xxml</organization>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <options customize="never" require-scripts="false" hostArchitectures="x86_64,arm64"/>

    <welcome file="welcome.html"/>
    <license file="license.html"/>
    <conclusion file="conclusion.html"/>

    <choices-outline>
        <line choice="default">
            <line choice="com.xxml.compiler.pkg"/>
        </line>
    </choices-outline>

    <choice id="default"/>

    <choice id="com.xxml.compiler.pkg" visible="false">
        <pkg-ref id="com.xxml.compiler.pkg"/>
    </choice>

    <pkg-ref id="com.xxml.compiler.pkg" version="$pkg_version" onConclusion="none">xxml-compiler.pkg</pkg-ref>
</installer-gui-script>
EOF

    print_success "Distribution XML created"
}

create_installer_resources() {
    print_step "Creating installer resources..."

    mkdir -p "$STAGING_DIR/resources"

    # Welcome HTML
    cat > "$STAGING_DIR/resources/welcome.html" << EOF
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; padding: 20px; }
        h1 { color: #333; }
        .version { color: #666; font-size: 14px; }
    </style>
</head>
<body>
    <h1>XXML Compiler</h1>
    <p class="version">Version $VERSION</p>
    <p>Welcome to the XXML Compiler installer.</p>
    <p>This will install:</p>
    <ul>
        <li><strong>xxml</strong> - The XXML compiler</li>
        <li><strong>xxml-lsp</strong> - Language Server Protocol support</li>
        <li><strong>Standard Library</strong> - XXML standard library modules</li>
    </ul>
    <p>Click Continue to proceed with the installation.</p>
</body>
</html>
EOF

    # License HTML
    cat > "$STAGING_DIR/resources/license.html" << EOF
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; padding: 20px; font-size: 12px; }
        h2 { color: #333; font-size: 14px; }
    </style>
</head>
<body>
    <h2>XXML Compiler License</h2>
    <p>Copyright (c) 2025 XXML Project</p>
    <p>Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:</p>
    <p>The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.</p>
    <p>THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.</p>
</body>
</html>
EOF

    # Conclusion HTML
    cat > "$STAGING_DIR/resources/conclusion.html" << EOF
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; padding: 20px; }
        h1 { color: #28a745; }
        code { background: #f4f4f4; padding: 2px 6px; border-radius: 3px; }
    </style>
</head>
<body>
    <h1>Installation Complete</h1>
    <p>XXML Compiler has been installed successfully.</p>
    <p>You can now use the compiler from your terminal:</p>
    <pre><code>xxml --help
xxml myprogram.XXML -o myprogram</code></pre>
    <p>For VS Code integration, install the XXML extension from the VS Code marketplace.</p>
    <p>Documentation is available at: <code>/usr/local/share/xxml/docs</code></p>
</body>
</html>
EOF

    print_success "Installer resources created"
}

create_installer_scripts() {
    print_step "Creating installer scripts..."

    local scripts_dir="$STAGING_DIR/scripts"
    mkdir -p "$scripts_dir"

    # Create preinstall script to remove previous installation
    cat > "$scripts_dir/preinstall" << 'EOF'
#!/bin/bash
#
# XXML Compiler Preinstall Script
# Removes previous installation before installing new version
#

echo "Checking for previous XXML installation..."

# Remove previous binaries
if [ -f "/usr/local/bin/xxml" ]; then
    echo "  Removing /usr/local/bin/xxml"
    rm -f "/usr/local/bin/xxml"
fi

if [ -f "/usr/local/bin/xxml-lsp" ]; then
    echo "  Removing /usr/local/bin/xxml-lsp"
    rm -f "/usr/local/bin/xxml-lsp"
fi

# Remove previous library directory
if [ -d "/usr/local/lib/xxml" ]; then
    echo "  Removing /usr/local/lib/xxml/"
    rm -rf "/usr/local/lib/xxml"
fi

# Remove previous share directory (contains Language stdlib and docs)
if [ -d "/usr/local/share/xxml" ]; then
    echo "  Removing /usr/local/share/xxml/"
    rm -rf "/usr/local/share/xxml"
fi

# Forget previous package receipt (if exists)
if pkgutil --pkgs | grep -q "com.xxml.compiler.pkg"; then
    echo "  Forgetting previous package receipt"
    pkgutil --forget "com.xxml.compiler.pkg" 2>/dev/null || true
fi

echo "Previous installation removed (if any)"
exit 0
EOF

    # Create postinstall script for any post-installation tasks
    cat > "$scripts_dir/postinstall" << 'EOF'
#!/bin/bash
#
# XXML Compiler Postinstall Script
#

echo "XXML Compiler installed successfully!"
echo ""
echo "Installation locations:"
echo "  Compiler:  /usr/local/bin/xxml"
echo "  LSP:       /usr/local/bin/xxml-lsp"
echo "  Library:   /usr/local/lib/xxml/"
echo "  Stdlib:    /usr/local/share/xxml/Language/"
echo ""
echo "Run 'xxml --help' to get started."

exit 0
EOF

    # Make scripts executable
    chmod +x "$scripts_dir/preinstall"
    chmod +x "$scripts_dir/postinstall"

    print_success "Installer scripts created"
}

build_component_pkg() {
    print_header "Building Component Package"

    local component_pkg="$STAGING_DIR/xxml-compiler.pkg"
    local scripts_dir="$STAGING_DIR/scripts"

    # Create installer scripts
    create_installer_scripts

    print_step "Running pkgbuild..."

    pkgbuild \
        --root "$STAGING_DIR$INSTALL_PREFIX" \
        --identifier "$PKG_ID" \
        --version "$VERSION" \
        --install-location "$INSTALL_PREFIX" \
        --scripts "$scripts_dir" \
        "$component_pkg"

    if [[ ! -f "$component_pkg" ]]; then
        print_error "Component package creation failed"
        exit 1
    fi

    print_success "Component package created"
}

build_product_pkg() {
    print_header "Building Product Package"

    mkdir -p "$OUTPUT_DIR"

    # Set global variable for the output path
    FINAL_PKG_PATH="$OUTPUT_DIR/XXML-Compiler-$VERSION.pkg"
    local unsigned_pkg="$STAGING_DIR/xxml-unsigned.pkg"

    create_distribution_xml
    create_installer_resources

    print_step "Running productbuild..."

    productbuild \
        --distribution "$STAGING_DIR/distribution.xml" \
        --resources "$STAGING_DIR/resources" \
        --package-path "$STAGING_DIR" \
        "$unsigned_pkg"

    if [[ ! -f "$unsigned_pkg" ]]; then
        print_error "Product package creation failed"
        exit 1
    fi

    # Sign the package if not skipping
    if [[ "$SKIP_SIGN" == true ]]; then
        mv "$unsigned_pkg" "$FINAL_PKG_PATH"
        print_warn "Package not signed (--skip-sign)"
    else
        print_step "Signing installer package..."
        productsign \
            --sign "$DEVELOPER_ID_INSTALLER" \
            --timestamp \
            "$unsigned_pkg" \
            "$FINAL_PKG_PATH"

        rm "$unsigned_pkg"

        # Verify package signature
        if pkgutil --check-signature "$FINAL_PKG_PATH" | grep -q "signed"; then
            print_success "Package signed and verified"
        else
            print_warn "Package signature verification inconclusive"
        fi
    fi

    print_success "Product package created: $FINAL_PKG_PATH"
}

# =============================================================================
# Notarization Functions
# =============================================================================

notarize_package() {
    local pkg_path="$1"

    if [[ "$SKIP_NOTARIZE" == true ]]; then
        print_warn "Skipping notarization (--skip-notarize)"
        return 0
    fi

    if [[ "$SKIP_SIGN" == true ]]; then
        print_warn "Cannot notarize unsigned package"
        return 0
    fi

    print_header "Notarizing Package"

    # Validate credentials
    if [[ -z "$APPLE_ID" || -z "$APPLE_APP_PASSWORD" || -z "$APPLE_TEAM_ID" ]]; then
        print_error "Apple notarization credentials not set in .env.local"
        print_warn "Required: APPLE_ID, APPLE_APP_PASSWORD, APPLE_TEAM_ID"
        return 1
    fi

    local bundle_id="${NOTARIZATION_BUNDLE_ID:-$BUNDLE_ID}"
    local timeout="${NOTARIZATION_TIMEOUT:-3600}"

    print_step "Submitting for notarization..."
    print_step "This may take several minutes..."

    # Submit for notarization using notarytool (macOS 12+)
    if xcrun notarytool submit "$pkg_path" \
        --apple-id "$APPLE_ID" \
        --password "$APPLE_APP_PASSWORD" \
        --team-id "$APPLE_TEAM_ID" \
        --wait \
        --timeout "$timeout"; then

        print_success "Notarization successful"

        # Staple the notarization ticket
        print_step "Stapling notarization ticket..."
        if xcrun stapler staple "$pkg_path"; then
            print_success "Notarization ticket stapled"
        else
            print_warn "Failed to staple ticket (package still valid)"
        fi
    else
        print_error "Notarization failed"
        print_step "Check the notarization log for details"
        return 1
    fi

    return 0
}

# =============================================================================
# Main Script
# =============================================================================

show_help() {
    cat << EOF
XXML Compiler macOS Installer Builder

Usage: $0 [options]

Options:
    --skip-build        Skip building the compiler (use existing build)
    --skip-sign         Skip code signing (for testing only)
    --skip-notarize     Skip notarization (for local testing)
    --version VERSION   Override version number
    --output DIR        Output directory (default: out)
    --help              Show this help message

Environment:
    Configure code signing by copying .env.local.example to .env.local
    and filling in your Apple Developer credentials.

Examples:
    $0                          # Full build with signing and notarization
    $0 --skip-build             # Use existing build
    $0 --skip-sign              # Build without signing (testing only)
    $0 --version 3.1.0          # Specify version

EOF
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --skip-sign)
            SKIP_SIGN=true
            shift
            ;;
        --skip-notarize)
            SKIP_NOTARIZE=true
            shift
            ;;
        --version)
            VERSION="$2"
            shift 2
            ;;
        --output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --help|-h)
            show_help
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            ;;
    esac
done

# Main execution
print_banner

# Load environment configuration
load_env

# Get version if not specified or invalid
# Validate version looks like X.Y.Z
if [[ -z "$VERSION" ]] || ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    VERSION=$(get_project_version)
fi

# Final validation
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    print_error "Invalid version: $VERSION"
    print_error "Version must be in format X.Y.Z (e.g., 3.0.0)"
    exit 1
fi

echo -e "${CYAN}Configuration:${NC}"
echo "  Project Root: $PROJECT_ROOT"
echo "  Build Dir:    $BUILD_DIR"
echo "  Output Dir:   $OUTPUT_DIR"
echo "  Version:      $VERSION"
echo "  Skip Build:   $SKIP_BUILD"
echo "  Skip Sign:    $SKIP_SIGN"
echo "  Skip Notarize: $SKIP_NOTARIZE"
echo ""

# Check prerequisites
check_prerequisites

# Validate signing identities
if [[ "$SKIP_SIGN" == false ]]; then
    if ! validate_signing_identity; then
        print_error "Code signing validation failed"
        echo ""
        echo "Options:"
        echo "  1. Configure .env.local with valid certificates"
        echo "  2. Run with --skip-sign for testing"
        exit 1
    fi
fi

# Build compiler if needed
if [[ "$SKIP_BUILD" == false ]]; then
    build_compiler
else
    print_step "Skipping build (--skip-build)"
    if [[ ! -f "$BUILD_DIR/bin/xxml" ]]; then
        print_error "Compiler not found: $BUILD_DIR/bin/xxml"
        print_error "Build first without --skip-build"
        exit 1
    fi
    print_success "Using existing build"
fi

# Prepare staging directory
prepare_staging

# Sign binaries
sign_all_binaries

# Build packages
build_component_pkg
build_product_pkg

# Notarize (uses FINAL_PKG_PATH set by build_product_pkg)
notarize_package "$FINAL_PKG_PATH"

# Final summary
echo ""
echo -e "${GREEN}============================================================${NC}"
echo -e "${GREEN}  BUILD SUCCESSFUL${NC}"
echo -e "${GREEN}============================================================${NC}"
echo ""
echo -e "  Installer: ${CYAN}$FINAL_PKG_PATH${NC}"

if [[ -f "$FINAL_PKG_PATH" ]]; then
    PKG_SIZE=$(du -h "$FINAL_PKG_PATH" | cut -f1)
    echo "  Size:      $PKG_SIZE"
fi

echo ""
echo "  To install: sudo installer -pkg \"$FINAL_PKG_PATH\" -target /"
echo ""

# Cleanup
if [[ -d "$STAGING_DIR" ]]; then
    print_step "Cleaning up staging directory..."
    rm -rf "$STAGING_DIR"
fi

print_success "Done!"
