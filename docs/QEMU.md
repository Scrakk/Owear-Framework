# Probar Owear en Windows y macOS con QEMU (Quickemu)

[Quickemu](https://github.com/quickemu-project/quickemu) crea VMs optimizadas
de **Windows 10/11 (con TPM 2.0)** y **macOS** con descarga automática del SO.

## Requisitos del host

| Requisito | Por qué |
|---|---|
| **KVM activado** (VT-x/AMD-V en BIOS) | Sin esto QEMU emula por software: 10–20× más lento |
| ≥ 8 GB RAM libres | Win11/macOS piden 4 GB+ para el guest |
| 60–80 GB disco | cada VM |
| `qemu`, `spice-tools`… | dependencias de quickemu |

Comprobar: `ls /dev/kvm` debe existir. Si no: activar VT-x/SVM en BIOS
(o en el host real si tú mismo eres una VM).

## Instalación (Ubuntu)

```bash
sudo add-apt-repository ppa:flexiondotorg/quickemu
sudo apt update && sudo apt install quickemu
```

## Windows 11

```bash
quickget windows 11            # descarga ISO + drivers VirtIO + crea conf
quickemu --vm windows-11.conf  # arranca; instala Windows (30–60 min c/KVM)
```

Dentro del guest:
1. Instala [MSYS2](https://www.msys2.org/) → `pacman -S mingw-w64-ucrt-x86_64-toolchain`
   (o Visual Studio Build Tools si prefieres MSVC).
2. Comparte este repo (Quickemu monta WebDAV en `\\localhost@8080\DavWWWRoot`
   o usa Samba si el host tiene smbd).
3. Compila:
   ```bat
   cd D:\owear
   cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release ^
         -DCMAKE_CXX_COMPILER=ucrt64.exe ^  :: o cl.exe de VS
   cmake --build build-win
   ```
4. WebView2 SDK (backend webview): `nuget install Microsoft.Web.WebView2` y
   apunta `include` a los headers. Los módulos `.owm` compilan sin WebView2.

## macOS

```bash
quickget macos sonoma
quickemu --vm macos-sonoma.conf
```

> ⚠️ La licencia de Apple exige ejecutar macOS en hardware Apple. Úsalo solo
> si cumples esos términos (p. ej. para CI interno sobre Macs reales, mejor
> usa los runners `macos-latest` de GitHub Actions ya configurados en CI).

Dentro del guest:
```bash
xcode-select --install
brew install cmake ninja pkg-config glib openssl@3
cd <carpeta compartida>/owear
OPENSSL_ROOT_DIR=$(brew --prefix openssl@3) cmake --preset macos-release
cmake --build --preset macos-release
```

## Alternativa sin VM (recomendada para compile-checks)

Los runners de **GitHub Actions** (`windows-latest`, `macos-latest`) son
máquinas físicas reales y gratuitas en repos públicos. `.github/workflows/ci.yml`
ya compila las 3 plataformas en cada push — los fallos que salgan ahí son el
feedback real que necesitamos para pulir los backends Win/Mac.
