# Microchip Zephyr LVGL App Demo using maXTouch and MTCH Buttons/Sliders/Wheels

This repository contains sample applications for maXTouch touch and MTCH BSW products.
Apps are designed to demonstrate touch devices working under the Zephyr OS using LVGL Library APIs.  Example input drivers can be used to build custom applications.

## Requirements

- **Python 3.12+**
- **Git 2.x+**
- **Windows 10/11**, **Linux**, or **macOS**
- **SAMA7D65 Curiosity Board P/N EV64J76A**
- **MPU32-MIPI-DISPLAY-720P P/N AC40T08A (mXT336UD) or**
- **MPU32-LVDS-DISPLAY-WVGA P/N AC69T88AA (mXT336UD)**
- **MTCH 3380P or MTCH3240P Evaluation kit**

Repository requires the zephyr-mchp-workspace repository.  The workspace contains scripts for all dependencies for building and flashing which includes (west, CMake, ninja, Zephyr SDK, cross-compilers). Scripts will install automatically by the `/install` cmd in the Zephyr venv.

## Install Instructions

Note: Refer to https://github.com/mchpgfx/zephyr-mchp-workspace for generic instructions

**Download Workspace**

Download the Zephyr MCHP workspace to install the initial files for Zephyr, tools and apps.
```
git clone https://github.com/mchpgfx/zephyr-mchp-workspace.git && cd zephyr-mchp-workspace

chmod +x zephyr.sh
./zephyr.sh /install --zephyr-repo https://github.com/mhtmhn/zephyr.git --zephyr-ref \ custom-ad7d65              

Select [] mgs_lvgl_touch_demo_ac69t88a or [] mgs_lvgl_touch_demo_ac40t08a during installation
```

**Build Application**

Building the solution:

```
/build mgs_lvgl_touch/touch_demo_ac40t08a/ -b sama7d65_curiosity/sama7d65 or
/build mgs_lvgl_touch/touch_demo_ac69t88a -b sama7d65_curiosity/sama7d65

```

**Flash Application**

Finally flash the application if you have a J-link connected
```
/flash mgs_lvgl_touch/touch_demo_ac40t08a  or
/flash mgs_lvgl_touch/touch_demo_ac69t88a

or copy the binary in /build/mgs_lvgl_touch/touch_demo_ac40t08a/zephyr/zephry.bin along with the boot.bin into a FAT32 SD Card.
```

The SDK version is auto-detected from the Zephyr source — no manual version management needed.

## Supported Boards

For this repository the demos were run using the SAMA7d65 Curiosity board

## App Packs

Pre-built Microchip app packs can be installed from the community registry. During `/install`, the CLI fetches the registry and presents an interactive selector:

```
  Available app packs:

  [x] mgs_lvgl_touch    MGS LVGL Touch Demo using AC40T08A MIPI Display (Examples)

  Space: toggle  |  Enter: confirm  |  Ctrl+C: skip
```

## Project Layout

```
manifest/west.yml       West manifest (Zephyr version + module allowlist, auto-generated)
app/                    Zephyr applications (standalone apps + cloned app packs)
tools/zephyr_cli/       Python CLI source
scripts/setup.ps1       PowerShell bootstrap (alternative to /install)
zephyr.bat / zephyr.ps1 Windows entry points
zephyr.sh               Linux / macOS entry point
```

Directories created at runtime (gitignored): `.venv/`, `.sdk/`, `.west/`, `zephyr/`, `modules/`, `build/`
