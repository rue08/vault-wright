# VaultWright

## Overview
A lightweight, self-contained **C++ IDE** built to solve a simple problem: your code should be as easy to pick up from anywhere as your photos or documents already are.

Cloud backup has made personal files effortless to access and sync across machines. Raw source code, in a real development environment, has never had that same convenience. This project closes that gap — sign up, and your files are available from any machine you install the IDE on.

The main aim is to build a full-featured project, and it's under active development.

## Features
- An embedded [Monaco Editor](https://microsoft.github.io/monaco-editor/) (VS Code's editor core) for every workspace tab — real find/replace, multi-cursor editing, code folding, a minimap, and language-aware syntax highlighting.
- One-click compile & run, driving the `g++` toolchain directly from the editor.
- A cloud-backed file system — save a file locally, upload it, and pull it down again from any other machine you're signed into.
- Stay signed in across restarts — logging in once is enough; the app silently restores your session on the next launch instead of asking again.
- Asynchronous upload/download with real-time status feedback, including batch upload and batch delete for multiple files at once (multi-select in either file tree, or use the File menu's Upload/Delete File to/from Cloud actions).
- Local file and folder management alongside your cloud files, side by side.
- Light/dark theme, either following the OS or set explicitly via **View > Theme**.
- Delete your account and every file you've stored in the cloud, at any time, from the **Profile** menu.

## Tech Stack
- **Language**: C++
- **Framework**: Qt (Widgets + Network + WebEngine + WebChannel)
- **Editor**: [Monaco Editor](https://microsoft.github.io/monaco-editor/), embedded via `QWebEngineView`
- **Auth**: Firebase Authentication (Google sign-in only)
- **File storage**: [Node.js + PostgreSQL backend](server/) (`server/`) — Firebase ID tokens in,
  files out; no Firestore/Cloud Storage involved. One instance is hosted centrally for you, see
  **Cloud Backend** below — you never need to run this yourself.
- **Compiler**: g++
- **Networking**: QtNetwork (REST APIs)

## Getting Started

Download the latest build from the [Releases page](https://github.com/rue08/VaultWright/releases) — both macOS and Windows builds are self-contained, no separate compiler or Qt install needed.

**macOS**
1. Open the `.dmg` and drag `VaultWright.app` out to Applications (or anywhere else) — don't run it from inside the mounted image.
2. First launch: **right-click → Open** (not a plain double-click) — the app isn't signed/notarized yet, so Gatekeeper will warn "unidentified developer." Right-click → Open gives an "Open anyway" option a plain double-click doesn't.

**Windows**
1. Unzip the download wherever you like.
2. First launch: if you see **"Windows protected your PC"** (Microsoft Defender SmartScreen) —
   expected, since the app isn't code-signed yet — click **More info**, then **Run anyway**.
3. Run `VaultWright.exe` directly.

Sign-in works out of the box once installed. Cloud file storage talks to a central backend
that's already running for you — nothing to set up on your end (see **Cloud Backend** below).

Want to build it yourself instead? See [BUILD.md](BUILD.md).

## Cloud Backend

File storage (upload/list/download) talks to a [`server/`](server/) Node.js + PostgreSQL
backend, not Firebase — but as a user, **you don't need to run this yourself.** One instance is
hosted centrally at `https://vaultwright.duckdns.org`.

## Using the IDE

1. **Sign in with Google** — click the login icon in the toolbar and sign in with your Google account (a browser tab opens for the Google consent screen). This is what ties your files to you across machines; once you're signed in, the app keeps you signed in on future launches too, so this is normally a one-time step per machine.
2. **Create or open a file** — `Ctrl+N` for a new file, `Ctrl+O` to open an existing one, or "Open Folder..." to work across a whole project.
3. **Write and save** — `Ctrl+S` saves locally, same as any editor.
4. **Run it** — hit the ▶️ **Run** button to compile and execute the current file with `g++`; output opens in a terminal window.
5. **Push to the cloud** — with the file open, click the ☁️ **Upload** button (or **File > Upload File to Cloud**) to save it to your account. Select multiple files in the local files tree first to upload them all in one batch.
6. **Pull it down elsewhere** — open **The Vault** from the toolbar to browse the files you've uploaded, and click one to bring it down to whatever machine you're on.
7. **Delete a cloud file** — select it (or several) in The Vault and use **File > Delete File from Cloud**; you'll be asked to confirm first.
8. **Delete your account** — choose **Profile > Delete Account** to permanently remove your account and every file you have stored in the cloud. You'll be asked to confirm, and it can't be undone.

## Author
Mehul Sharma\
Gmail: mehssi2004@gmail.com

If you have any suggestions feel free to reach out to me via mail, and if you liked the project, make sure to give a star ⭐️.
