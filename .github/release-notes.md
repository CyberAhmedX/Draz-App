Ready-to-run builds of Draz. Nothing to install and no compiler needed.

**Download the file for your system**, unzip it, and open `START-HERE.txt`:

- **Windows (64-bit):** the `windows-x64.zip` file, then double-click `try-demo.bat`
- **macOS (Apple silicon and Intel):** the `macos-universal.tar.gz` file, then double-click `try-demo.command`
- **Linux (x64):** the `linux-x64.tar.gz` file, then run `./try-demo.sh`

The demo opens with a sample store. Log in to the shop as `ali`, or to the admin panel as `admin`. The passwords are in `START-HERE.txt` and the README.

The builds are not code-signed, so Windows SmartScreen and macOS Gatekeeper warn the first time. `START-HERE.txt` explains how to allow them. Each download is listed in `SHA256SUMS.txt` so you can check it.

Every build here was compiled by GitHub Actions from this release's commit, passed the tests, and was started and logged in to the demo data before it was published.
