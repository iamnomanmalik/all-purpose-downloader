# All-Purpose Downloader (C++ / Qt6)

IDM-style multi-segment download manager. Ek file ko 8 parallel segments
mein split kar ke download karta hai, pause/resume support ke sath, aur
Chrome extension se link accept karta hai.

**Test ho chuka hai:** 11 MB aur 433 MB real files, 8 segments, dono
100% valid nikle (zip integrity check se confirm kiya).

---

## Kya kya hai is package mein

- `src/` — poora C++ source code
  - `DownloadTask.*` — core multi-segment engine (libcurl + std::thread)
  - `DownloadEngine.*` — saare downloads manage karta hai
  - `LocalServer.*` — extension se baat karne wala local HTTP server
  - `MainWindow.*`, `DownloadRowWidget.*` — Qt6 GUI (dark theme, stacked-rows style)
- `third_party/` — header-only libraries (cpp-httplib, nlohmann/json)
- `CMakeLists.txt` — build configuration
- `.github/workflows/build-windows.yml` — GitHub Actions se automatic Windows `.exe` build
- `linux-binary/downloader` — ready-to-run Linux binary (already built + tested)

---

## Linux par chalana (ready binary hai)

```bash
cd linux-binary
./downloader
```

Requirement: Qt6 runtime libraries aur libcurl already system mein hone chahiye
(zyada tar Linux distros mein already installed hote hain). Agar missing error
aaye:

```bash
sudo apt install qt6-base-dev libcurl4
```

---

## Source se khud build karna (Linux/Mac)

```bash
sudo apt install cmake build-essential libcurl4-openssl-dev qt6-base-dev qt6-base-dev-tools
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./downloader
```

---

## Windows `.exe` banana

Sandbox environment mein Qt ke Windows binaries download nahi ho sakte
(network restriction), is liye teen options hain:

### Option A — GitHub Actions (sabse aasan, Windows machine ki zaroorat nahi)

1. Ye poora folder GitHub pe ek naye repo mein push karein
2. `.github/workflows/build-windows.yml` already shamil hai — push karte hi
   automatic build shuru ho jayega
3. Actions tab mein ja kar "AllPurposeDownloader-windows" artifact download
   kar lein — `.exe` + zaroori DLLs sab milenge

### Option B — Apni Windows machine par

1. [Qt Creator](https://www.qt.io/download-qt-installer) install karein
   (Qt6 + MSVC compiler ke sath)
2. `vcpkg` se libcurl install karein: `vcpkg install curl:x64-windows`
3. Qt Creator mein `CMakeLists.txt` kholein, "Build" dabayein

---

## Extension se connect karna

Pehle wala `video-grabber-extension` (Chrome extension) **bina kisi tabdeeli
ke** is app ke sath kaam karega — dono same contract use karte hain:

- Server: `http://127.0.0.1:38471`
- `GET /ping` — check ke app chal rahi hai
- `POST /add-download` — `{url, filename, pageUrl}` bhejo, download shuru

---

## Kaise kaam karta hai (technical)

1. Jab koi URL add hoti hai, pehle ek HEAD request se file ka size aur
   `Accept-Ranges` support check hota hai
2. Agar server ranges support karta hai aur file 2MB se badi hai — file ko
   8 barabar hisson (byte ranges) mein baant diya jata hai
3. Har hissa apne alag thread mein, apne alag libcurl connection se
   parallel download hota hai — seedha file ke sahi offset par likha jata hai
4. Pause: sab threads ko signal milta hai rukne ka (unka progress safe
   rehta hai). Resume: wahi threads apne last byte se dobara shuru hote hain
5. Agar server ranges support nahi karta — automatically single-connection
   mode mein fallback ho jata hai (phir bhi resume-capable)

## Limitations / aage kya add ho sakta hai

- HLS (`.m3u8`) support abhi is version mein nahi hai (Electron version mein
  tha, ffmpeg ke zariye) — chahiye to add kar sakte hain
- Segment count (8) abhi fixed hai code mein — settings screen se
  configurable banaya ja sakta hai
- Download history / queue persistence (app band karne ke baad list yaad
  rakhna) abhi nahi hai
