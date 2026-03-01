# Moddo Evolution Translator

**Offline email translation for GNOME Evolution using ArgosTranslate**

[![Version](https://img.shields.io/badge/version-1.2.0-blue.svg)](#)
[![License](https://img.shields.io/badge/license-LGPL--2.1%2B-green.svg)](#license)
[![Documentation](https://img.shields.io/badge/docs-complete-brightgreen.svg)](docs/USER_GUIDE.md)

> **Tested and verified on Manjaro Linux with GNOME Evolution 3.58.3.**
> Originally forked from [costantinoai/evolution-mail-translate](https://github.com/costantinoai/evolution-mail-translate),
> extended and maintained independently. Compatibility with Evolution ≥ 3.56's new EUIManager API
> was the original motivation — see [Changes from upstream](#changes-from-upstream).

## Overview

This Evolution extension adds instant, privacy-preserving email translation directly within GNOME Evolution. Translate foreign language emails to your preferred language with a single click—all processing happens locally on your machine with no data ever leaving your computer.

## Key Features

- **One-Click Translation**: Translate emails directly from the toolbar, menu, or keyboard shortcut
- **Toggle back instantly**: Press the shortcut (or click the toolbar button) again to restore the original
- **100% Offline & Private**: Uses local translation models (ArgosTranslate), no internet required, no data transmitted
- **Toolbar button**: Translate icon appears next to Reply/Forward in the mail toolbar
- **Custom keyboard shortcut**: Default `Alt+Shift+T`, fully configurable in Translate Settings
- **Install-on-Demand**: Automatically downloads missing translation models as needed
- **HTML Email Support**: Preserves formatting, styles, and structure in translated emails
- **Auto Language Detection**: Automatically detects source language
- **50+ Languages**: Supports translation between 50+ language pairs
- **GPU Acceleration**: Automatically uses CUDA when available for faster translation

## Quick Start

### Installation

#### Ubuntu / Debian

**For users:** Download and install the `.deb` package from [GitHub Releases](https://github.com/costantinoai/evolution-mail-translate/releases)

```bash
# Recommended: lets APT resolve dependencies automatically
sudo apt install ./evolution-translate-extension_1.0.0-1_amd64.deb

# Restart Evolution
killall evolution && evolution &
```

**From source (Ubuntu/Debian):**

```bash
# Install build dependencies
sudo apt install cmake pkg-config evolution-dev evolution-data-server-dev \
  python3 python3-venv python3-pip

# Clone and build
git clone https://github.com/MoDD0/moddo-evolution-translator.git
cd evolution-mail-translate

# Build and install to system directories (requires sudo)
./scripts/install-from-source.sh

# Restart Evolution
killall evolution 2>/dev/null || true
evolution &
```

#### Manjaro / Arch Linux

> Tested on Manjaro with GNOME Evolution 3.58.3. No `.deb` package — build from source.
> On Arch-based distros the `evolution` package already includes development headers,
> so no separate `-dev` package is needed.

```bash
# Install build dependencies
sudo pacman -S --needed cmake pkgconf python python-pip

# Clone this fork (has fixes for Evolution >= 3.56)
git clone https://github.com/MoDD0/moddo-evolution-translator.git
cd evolution-mail-translate

# Build and install to system directories (requires sudo)
./scripts/install-from-source.sh

# Restart Evolution
killall evolution 2>/dev/null || true
evolution &
```

**Notes:**
- Evolution only loads modules from `/usr/lib*/evolution/modules/`, so installation requires sudo
- The module is installed to `/usr/lib*/evolution/modules/libtranslate-module.so`
- Python helper scripts are installed to `/usr/share/evolution-translate/translate/`
- Python environment and models are per-user: run `evolution-translate-setup` to create a venv under `~/.local/lib/evolution-translate/venv` and install models under `~/.local/share/argos-translate/packages/`

**Uninstall:**

```bash
# From the repository directory
./scripts/uninstall.sh
```

See **[USER_GUIDE.md](docs/USER_GUIDE.md)** for detailed installation instructions and all available methods.

### Usage

1. Select an email and press `Alt+Shift+T` — or click the **Translate button** in the toolbar
2. Press the same shortcut (or click the toolbar button) again to toggle back to the original
3. Configure settings in `Edit` → `Translate Settings`
   - Change the target language, translation provider, or keyboard shortcut

See **[USER_GUIDE.md](docs/USER_GUIDE.md)** for complete usage documentation.

## Documentation

- **[USER_GUIDE.md](docs/USER_GUIDE.md)** - Installation, usage, configuration, and troubleshooting
- **[DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md)** - Architecture, development, and contribution guidelines
- **[CHANGELOG.md](docs/CHANGELOG.md)** - Notable changes

## Settings

Open “Translate Settings” via **Edit → Translate Settings**.

- **Target language**: Choose your default translation target
- **Provider**: Argos Translate (offline) or Google Translate (online)
- **Translate shortcut**: Customize the keyboard shortcut (default: `Alt+Shift+T`). Takes effect after restarting Evolution.
- **Install models on demand**: If enabled, missing Argos models are downloaded automatically the first time a pair is needed
- **Python venv**: Create and manage your per-user venv with `evolution-translate-setup` (installs Python deps and optionally models)

Tip: You can also set environment variables for development overrides:
- `TRANSLATE_HELPER_PATH` to point to a local translate_runner.py
- `TRANSLATE_PYTHON_BIN` to point to a specific Python interpreter

## Security & Privacy

- **No Data Transmission**: All translation happens locally; message content never leaves your machine
- **Body Only**: Only email body is processed; headers, addresses, and attachments are never touched
- **Open Source Models**: Uses transparent, auditable open-source translation models
- **No API Keys**: No accounts, no tracking, no telemetry

## Requirements

- **GNOME Evolution** ≥ 3.36
- **Python** 3.8+
- **CMake** 3.10+ (for building from source)

## Contributing

We welcome contributions! See [CONTRIBUTING.md](docs/CONTRIBUTING.md) for guidelines.

## License

This project follows the same LGPL-2.1+ licensing model as the Evolution example module files.

## Changes from upstream

This fork fixes two issues that prevent the plugin from building and running on
**Evolution ≥ 3.56** (which ships with Manjaro, Fedora, and other rolling-release distros):

### 1. `e_ui_manager_add_actions_with_eui_data` API change

Evolution 3.56 reduced the function from 10 arguments to 7. The `name` string,
`-1` length, and `&error` parameters were removed, and `user_data` moved before `eui`:

```c
/* Old (Evolution < 3.56) */
e_ui_manager_add_actions_with_eui_data(ui_manager, group, domain,
    entries, n_entries, "ui-name", eui_def, -1, user_data, &error);

/* New (Evolution ≥ 3.56) */
e_ui_manager_add_actions_with_eui_data(ui_manager, group, domain,
    entries, n_entries, user_data, eui_def);
```

### 2. EUI XML format for custom submenus

The new EUI parser requires `<submenu action='...'>` referencing a registered action.
The old `<submenu id='...'>` with `<attribute name='label'>` and `after=` attributes
are no longer valid. Custom plugin menus must use the `custom-menus` placeholder:

```xml
<eui>
  <menu id='main-menu'>
    <placeholder id='custom-menus'>
      <submenu action='translate-menu'>
        <item action='translate-message-action'/>
        ...
      </submenu>
    </placeholder>
  </menu>
</eui>
```

The submenu header (`translate-menu`) must be registered as an `EUIActionEntry`
with a `NULL` activate callback.

## Credits

- Originally forked from [costantinoai/evolution-mail-translate](https://github.com/costantinoai/evolution-mail-translate)
- Built on [ArgosTranslate](https://github.com/argosopentech/argos-translate)
- Integrates with [GNOME Evolution](https://wiki.gnome.org/Apps/Evolution)
- Translation models from [OpenNMT](https://opennmt.net/)
