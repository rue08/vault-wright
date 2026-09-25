# Building VaultWright from source

This page is for building the desktop app yourself. If you just want to use VaultWright, download
the latest build from the [Releases page](https://github.com/rue08/VaultWright/releases) instead — it works out of the box, including cloud sync.

## Read this first: a source build can't use the hosted cloud backend

The hosted cloud backend only accepts sign-ins from the maintainer's own Firebase project. A build
made from this source uses **your** Firebase project and Google OAuth client (step 2), so the hosted
backend rejects it. To inspect the cloud features (Upload, The Vault) in your build, do step 6, which
builds the server.

Local editing, saving, compiling and running don't involve the backend, so you can skip step 6 and
still use those.

## Requirements

- **Qt 6** with the Widgets, Network, WebEngineWidgets and WebChannel modules
- **CMake 3.16** or newer
- A **C++17** compiler (MSVC or MinGW on Windows, Xcode Command Line Tools on macOS)
- **npm** on your `PATH` (used once, to fetch the Monaco editor)
- **Docker** with Compose, only for step 6
- **g++** if you want the in-app **Run** button to work: the Xcode Command Line Tools on macOS
  (`xcode-select --install`), or MinGW-w64 on Windows (see step 4)

## 1. Get the source

```bash
git clone https://github.com/rue08/VaultWright.git
cd VaultWright
```

## 2. Create `config.h`

The app won't compile without it. It's gitignored, so create it from the template:

```bash
cp config.h.example config.h
```

Then fill in your own values (the template lists where each comes from):

- `FIREBASE_API_KEY` — Firebase Console → Project settings → General → Web API key
- `GOOGLE_OAUTH_CLIENT_ID` and `GOOGLE_OAUTH_CLIENT_SECRET` — Google Cloud Console → APIs & Services
  → Credentials → an OAuth client of type **Desktop app**
- `BACKEND_URL` — the address of the server from step 6

Use a Firebase project you own, with **Google** enabled as a sign-in method. Never commit
`config.h`.

## 3. Fetch the Monaco editor

The embedded editor (about 24 MB) isn't stored in git. Fetch it once per machine:

```bash
third_party/fetch-monaco.sh
```

It's a bash script that uses `npm` and `tar`, so on Windows run it from Git Bash or WSL. CMake stops
with an error if `third_party/monaco/vs/` is missing.

## 4. Windows only: bundling a compiler (optional)

Skip this if you already have `g++` on your `PATH`: the Run button will use it. It's only needed to
make a build that runs C++ on a machine with no compiler installed.

1. Download a portable x86_64 MinGW-w64 build, for example from [WinLibs](https://winlibs.com/).
2. Unzip it so you end up with `windows/mingw64/bin/g++.exe` (and its `.dll` files beside it).

**Do this before your first `cmake -B build`.** CMake checks for the folder while *configuring*,
not building, so if you add it later the copy step is silently never added. If you already
configured, delete `build/` and configure again. `windows/mingw64/` is gitignored, and a full
toolchain adds roughly 150–400 MB to the output.

## 5. Build

Point CMake at your Qt install (the path depends on how and where you installed Qt):

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/<platform>
cmake --build build
```

Alternatively, open `CMakeLists.txt` in Qt Creator and build from there. The result is
`VaultWright.app` (macOS) or `VaultWright.exe` (Windows) inside `build/`. The Monaco editor is
copied next to it as a `monaco/` folder, and the app needs that folder beside it to run.

## 6. Server (only for cloud features)

The cloud backend is a Node.js + PostgreSQL server in `server/`, built and run with Docker. Use the
**same Firebase project** as in step 2: the server only accepts sign-ins from the project id you give
it.

```bash
cd server
cp .env.example .env
```

Edit `.env`:

- `POSTGRES_PASSWORD` — generate one with `openssl rand -hex 24` or give one according to your convenience.
- `FIREBASE_PROJECT_ID` — your Firebase **Project ID** (Firebase Console → Project settings →
  General). It must be the project your `FIREBASE_API_KEY` in `config.h` belongs to.

Then build and start it, and check that it answers:

```bash
docker compose up -d --build
curl http://127.0.0.1:5000/health     # {"ok":true}
```

This builds the image from `server/Dockerfile` and starts PostgreSQL, whose tables are created
automatically the first time it boots on an empty volume. Compose refuses to start if
`POSTGRES_PASSWORD` or `FIREBASE_PROJECT_ID` is missing. The server listens only on `127.0.0.1:5000`
and the database isn't exposed. Stop it with `docker compose down`.

Finally, set `BACKEND_URL` in `config.h` to `http://127.0.0.1:5000` and rebuild the app.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `config.h: No such file or directory` | Step 2 wasn't done. |
| `third_party/monaco/vs/ not found` | Step 3 wasn't done. |
| Run button says `g++` wasn't found | No compiler on `PATH`. On macOS install the Command Line Tools; on Windows put `g++` on `PATH` or do step 4 and reconfigure. |
| Sign-in fails | Check your Firebase API key and that the OAuth client is type **Desktop app**. |
| Signed in, but Upload or The Vault fails with 401 | The server's `FIREBASE_PROJECT_ID` isn't the project your `config.h` keys belong to, or the app is still pointed at the hosted backend (see step 6). |
| Signed in, but Upload or The Vault can't connect | The server isn't running, or `BACKEND_URL` in `config.h` is wrong. |
| `docker compose up` says a variable is not set | `POSTGRES_PASSWORD` or `FIREBASE_PROJECT_ID` is missing from `server/.env`. |
