# MobileKeyboard

Raspberry Pi Pico WHを、Wi-Fi経由で受け取った入力をUSB HIDキーボードとしてPCへ送るブリッジにするファームウェアである。  
MobileKeyboard is firmware that turns a Raspberry Pi Pico WH into a bridge that receives input over Wi-Fi and sends it to a PC as a USB HID keyboard.

## 方針 / Design

- Pico WHはmicro USBでPCへ接続する。 / The Pico WH connects to the PC over micro USB.
- PCからは通常のUSBキーボードとして認識される。 / The PC recognizes it as a regular USB keyboard.
- Pico WHはWi-Fiアクセスポイントを作る。 / The Pico WH creates its own Wi-Fi access point.
- PCやスマホをPico WHのWi-Fiへ接続する。 / A PC or phone connects to the Pico WH Wi-Fi network.
- 接続した端末からWeb UIまたはUDPで文字列やキーコマンドを送る。 / The connected device sends text or key commands through the Web UI or UDP.
- Pico WHは受信内容をUSB HIDキーボードレポートへ変換する。 / The Pico WH converts the received input into USB HID keyboard reports.

```text
スマホ / PC / Phone or PC
  -> Pico WHのWi-Fiへ接続 / Connect to Pico WH Wi-Fi
  -> HTTP http://192.168.4.1/ or UDP 192.168.4.1:4242
Raspberry Pi Pico WH
  -> USB HID Keyboard
PC
```

## 必要なもの / Requirements

- Raspberry Pi Pico WH / Raspberry Pi Pico WH
- Raspberry Pi Pico C/C++ SDK / Raspberry Pi Pico C/C++ SDK
- CMake / CMake
- ARM GCC toolchain / ARM GCC toolchain
- NinjaまたはNMake / Ninja or NMake
- USBケーブル / USB cable

`PICO_SDK_PATH` はPico SDKの場所を指している必要がある。  
`PICO_SDK_PATH` must point to the Pico SDK directory.

## 動作確認済みボード / Tested Board

動作確認済みボードは次の通りである。  
The tested board is as follows.

- Raspberry Pi Pico WH / Raspberry Pi Pico WH

## Windows環境の準備 / Windows Setup

`cmake` が認識されない場合は、CMakeが未インストールであるか、PATHに追加されていない。  
If `cmake` is not recognized, CMake is either not installed or not added to `PATH`.

Windowsでは次のどちらかの方法を使う。  
On Windows, use one of the following setup methods.

### 方法A: Raspberry Pi Pico Windows Installerを使う / Option A: Use Raspberry Pi Pico Windows Installer

Raspberry Pi公式のPico Windows Installerで、Pico SDK、CMake、Ninja、ARM GCCなどをまとめて入れる。  
The official Raspberry Pi Pico Windows Installer installs the Pico SDK, CMake, Ninja, ARM GCC, and related tools together.

インストール後は、通常のPowerShellではなく、インストーラーが用意するPico SDK用のDeveloper PowerShellまたはCommand Promptを開いてビルドする。  
After installation, build from the Pico SDK Developer PowerShell or Command Prompt provided by the installer, not from a plain PowerShell window.

### 方法B: 手動で入れる / Option B: Manual Setup

少なくとも次をインストールしてPATHに追加する。  
Install at least the following tools and add them to `PATH`.

- CMake / CMake
- Ninja / Ninja
- Arm GNU Toolchain / Arm GNU Toolchain
- Raspberry Pi Pico SDK / Raspberry Pi Pico SDK

PowerShellで確認する。  
Check from PowerShell.

```powershell
cmake --version
ninja --version
arm-none-eabi-gcc --version
echo $env:PICO_SDK_PATH
```

## ビルド / Build

PowerShell + Ninjaの例である。  
This is a PowerShell + Ninja example.

```powershell
mkdir build
cd build
cmake .. -G Ninja `
  -DPICO_BOARD=pico_w `
  -DMOBILEKBD_WIFI_SSID="PicoKeyboard" `
  -DMOBILEKBD_WIFI_PASSWORD="port3710"
ninja
```

Visual Studio Build ToolsのDeveloper PowerShellを使う場合は、NMakeでもビルドできる。  
When using the Visual Studio Build Tools Developer PowerShell, NMake can also be used.

```powershell
mkdir build
cd build
cmake .. -G "NMake Makefiles" `
  -DPICO_BOARD=pico_w `
  -DMOBILEKBD_WIFI_SSID="PicoKeyboard" `
  -DMOBILEKBD_WIFI_PASSWORD="port3710"
nmake
```

Unix系シェルの例である。  
This is a Unix-like shell example.

```sh
mkdir -p build
cd build
cmake .. \
  -DPICO_BOARD=pico_w \
  -DMOBILEKBD_WIFI_SSID="PicoKeyboard" \
  -DMOBILEKBD_WIFI_PASSWORD="port3710"
cmake --build .
```

生成された `mobile_keyboard.uf2` を、BOOTSELを押しながら接続したPico WHの `RPI-RP2` ドライブへコピーする。  
Copy the generated `mobile_keyboard.uf2` to the `RPI-RP2` drive that appears when the Pico WH is connected while holding BOOTSEL.

## 使い方 / Usage

Pico WHをPCへ接続した状態で、PCまたはスマホをPico WHが作るWi-Fiへ接続する。  
Connect the Pico WH to the PC, then connect a PC or phone to the Wi-Fi network created by the Pico WH.

デフォルト例である。  
This is the default configuration example.

```text
SSID: PicoKeyboard
Password: mobile_keyboard.local.cmake で設定した値 / value configured in mobile_keyboard.local.cmake
Pico IP: 192.168.4.1
HTTP: http://192.168.4.1/
UDP port: 4242
```

APのSSIDとパスワードをgitに入れたくない場合は、`mobile_keyboard.local.cmake.example` を `mobile_keyboard.local.cmake` にコピーして編集する。  
To keep the AP SSID and password out of git, copy `mobile_keyboard.local.cmake.example` to `mobile_keyboard.local.cmake` and edit it.

`mobile_keyboard.local.cmake` は `.gitignore` 済みである。  
`mobile_keyboard.local.cmake` is already ignored by `.gitignore`.

接続後、スマホやPCのブラウザで `http://192.168.4.1/` を開くと、簡易入力画面を使える。  
After connecting, open `http://192.168.4.1/` in a browser on a phone or PC to use the simple input UI.

UDPで直接送る場合は、`192.168.4.1:4242` へ送信する。  
To send input directly over UDP, send packets to `192.168.4.1:4242`.

USBはキーボード専用にしているため、ログはUSBシリアルには出ない。  
USB is used only as a keyboard, so logs are not available over USB serial.

Linux/macOSの例である。  
This is a Linux/macOS example.

```sh
printf "TEXT hello world\n" | nc -u -w1 192.168.4.1 4242
printf "KEY ENTER\n" | nc -u -w1 192.168.4.1 4242
```

PowerShellの例である。  
This is a PowerShell example.

```powershell
$udp = [System.Net.Sockets.UdpClient]::new()
$bytes = [Text.Encoding]::ASCII.GetBytes("TEXT hello world`n")
$udp.Send($bytes, $bytes.Length, "192.168.4.1", 4242)
$udp.Close()
```

## プロトコル / Protocol

### 文字列入力 / Text Input

```text
TEXT hello world
```

`TEXT ` 以降をASCIIキーボード入力として送信する。  
The content after `TEXT ` is sent as ASCII keyboard input.

接頭辞なしのUDP payloadも文字列として扱う。  
A UDP payload without a prefix is also treated as text input.

### 特殊キー / Special Keys

```text
KEY ENTER
KEY BACKSPACE
KEY ESC
KEY TAB
KEY LEFT
KEY RIGHT
KEY UP
KEY DOWN
KEY DELETE
KEY HOME
KEY END
KEY PAGEUP
KEY PAGEDOWN
KEY CTRL+C
KEY CTRL+SHIFT+ESC
KEY HANKAKU_ZENKAKU
KEY KANA
KEY EISU
KEY HENKAN
KEY MUHENKAN
KEY KATAKANA_HIRAGANA
```

修飾キーは `CTRL+`、`ALT+`、`SHIFT+`、`WIN+`、`GUI+` などを組み合わせて指定できる。  
Modifiers such as `CTRL+`, `ALT+`, `SHIFT+`, `WIN+`, and `GUI+` can be combined with key names.

## Web UI / Web UI

Web UIは `http://192.168.4.1/` で配信される。  
The Web UI is served at `http://192.168.4.1/`.

テキスト入力、Enter、Backspace、矢印キー、Ctrl系ショートカット、日本語キーボード系キーを送信できる。  
It can send text input, Enter, Backspace, arrow keys, Ctrl shortcuts, and Japanese keyboard keys.

Web UIの送信はJavaScriptの `fetch()` を使う。  
The Web UI uses JavaScript `fetch()` for sending input.

通常のフォーム送信を避けることで、iPhone/SafariのHTTPフォーム送信警告を出にくくしている。  
This avoids normal form submission and reduces HTTP form-submission warnings on iPhone/Safari.

## 現時点の制限 / Current Limitations

- 文字列入力はASCII中心である。 / Text input is mainly ASCII-oriented.
- `TEXT` で送る記号のキーマップはJIS配列（mac日本語配列向け）基準である。 / Symbol mapping for `TEXT` input follows a JIS layout (for macOS Japanese keyboard layout).
- 日本語入力はPC側OSのキーボードレイアウトとIME状態に依存する。 / Japanese input depends on the host OS keyboard layout and IME state.
- HTTPで配信しているため、ブラウザの「安全ではない」表示自体は消えない。 / Because the UI is served over HTTP, the browser's "Not Secure" indicator itself cannot be removed.
- Pico WHはAPモードで動作する。 / The Pico WH runs in AP mode.
- DHCPは最小実装である。 / DHCP is a minimal implementation.

## 次の拡張候補 / Future Work

- JP配列向けの文字入力キーマップ / Character mapping for Japanese keyboard layouts
- WebSocketまたはHTTP APIの拡張 / Expanded WebSocket or HTTP API support
- BLE GATT入力対応 / BLE GATT input support
- Web UIの入力履歴やボタン追加 / Input history and additional buttons in the Web UI
- キーキューの状態表示 / Key queue status display
