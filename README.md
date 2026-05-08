# libamlgui — AML ImGui Overlay

ImGui overlay via `eglSwapBuffers` hook untuk SA-MP Android.  
Pure C++, no Lua. ARM 32-bit (armeabi-v7a).

## Struktur

```
amlgui/
├── mod/
│   ├── main.cpp                        ← entry point + eglSwapBuffers hook
│   ├── imgui_impl_android_gles2.cpp    ← backend GLES2 renderer
│   └── imgui/                          ← diisi otomatis oleh CI (imgui v1.90.4)
├── include/AML/                        ← AML headers
├── jni/
│   ├── Android.mk
│   └── Application.mk
└── .github/workflows/build.yml         ← CI otomatis download ImGui + build
```

## Build via GitHub Actions

Push ke `main` → Actions otomatis:
1. Download ImGui v1.90.4
2. Build dengan NDK r25c → armeabi-v7a
3. Upload artifact `libamlgui-arm32`

Download hasil build dari tab **Actions → artifact**.

## Deploy ke device

```bash
# Copy ke folder AML mods
cp libamlgui.so /storage/emulated/0/Android/data/com.sampmobilerp.game/mods/

# Atau via Shizuku
rish -c "cp /storage/emulated/0/mods/libamlgui.so \
  /data/data/com.sampmobilerp.game/lib/libamlgui.so"
```

## Cek log

```bash
tail -f /storage/emulated/0/amlgui_log.txt
```

## Termux: push pertama kali

```bash
git clone https://github.com/brruham-arch/amlgui.git
cd amlgui
# extract file zip ke sini
git add .
git commit -m "init: AML ImGui overlay v1.0"
git push origin main
gh run watch $(gh run list --limit 1 --json databaseId -q '.[0].databaseId')
```

## Author
brruham-arch
