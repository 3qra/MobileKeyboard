# MobileKeyboard

Raspberry Pi Pico WHを、Wi-Fi経由で受け取った入力をUSB HIDキーボードとしてPCへ送るブリッジにするファームウェアです。

## 方針

- Pico WHはmicro USBでPCへ接続します。
- PCからは通常のUSBキーボードとして認識されます。
- Pico WHはWi-Fiアクセスポイントを作ります。
- PCやスマホをPico WHのWi-Fiへ接続します。
- 接続した端末からUDPで文字列やキーコマンドを送ります。
- Pico WHは受信内容をUSB HIDキーボードレポートへ変換します。

```text
スマホ / PC
  -> Pico WHのWi-Fiへ接続
  -> UDP 192.168.4.1:4242
Raspberry Pi Pico WH
  -> USB HID Keyboard
PC
```

## 必要なもの

- Raspberry Pi Pico WH
- Raspberry Pi Pico C/C++ SDK
- CMake
- ARM GCC toolchain
- NinjaまたはNMake
- USBケーブル

`PICO_SDK_PATH` がPico SDKの場所を指している必要があります。

## Windows環境の準備

`cmake` が認識されない場合は、まだCMakeがインストールされていないか、PATHに追加されていません。

Windowsでは次のどちらかの方法が扱いやすいです。

### 方法A: Raspberry Pi Pico Windows Installerを使う

Raspberry Pi公式のPico Windows Installerで、Pico SDK、CMake、Ninja、ARM GCCなどをまとめて入れます。

インストール後は、通常のPowerShellではなく、インストーラーが用意するPico SDK用のDeveloper PowerShell/Command Promptを開いてビルドしてください。

### 方法B: 手動で入れる

少なくとも次をインストールしてPATHに追加します。

- CMake
- Ninja
- Arm GNU Toolchain
- Raspberry Pi Pico SDK

PowerShellで確認:

```powershell
cmake --version
ninja --version
arm-none-eabi-gcc --version
echo $env:PICO_SDK_PATH
```

## ビルド

PowerShell + Ninja例:

```powershell
mkdir build
cd build
cmake .. -G Ninja `
  -DPICO_BOARD=pico_w `
  -DMOBILEKBD_WIFI_SSID="PicoKeyboard" `
  -DMOBILEKBD_WIFI_PASSWORD="port3710"
ninja
```

Visual Studio Build ToolsのDeveloper PowerShellを使う場合は、NMakeでもビルドできます。

```powershell
mkdir build
cd build
cmake .. -G "NMake Makefiles" `
  -DPICO_BOARD=pico_w `
  -DMOBILEKBD_WIFI_SSID="PicoKeyboard" `
  -DMOBILEKBD_WIFI_PASSWORD="port3710"
nmake
```

Unix系シェル例:

```sh
mkdir -p build
cd build
cmake .. \
  -DPICO_BOARD=pico_w \
  -DMOBILEKBD_WIFI_SSID="PicoKeyboard" \
  -DMOBILEKBD_WIFI_PASSWORD="port3710"
cmake --build .
```

生成された `mobile_keyboard.uf2` を、BOOTSELを押しながら接続したPico WHの `RPI-RP2` ドライブへコピーします。

## 使い方

Pico WHをPCへ接続した状態で、PCまたはスマホをPico WHが作るWi-Fiへ接続します。

デフォルト例:

```text
SSID: PicoKeyboard
Password: mobile_keyboard.local.cmake で設定した値
Pico IP: 192.168.4.1
HTTP: http://192.168.4.1/
UDP port: 4242
```

APのSSID/パスワードをgitに入れたくない場合は、`mobile_keyboard.local.cmake.example` を `mobile_keyboard.local.cmake` にコピーして編集します。`mobile_keyboard.local.cmake` は `.gitignore` 済みです。

接続後、スマホやPCのブラウザで `http://192.168.4.1/` を開くと、簡易入力画面を使えます。

UDPで直接送る場合は、`192.168.4.1:4242` へ送信します。USBはキーボード専用にしているため、ログはUSBシリアルには出ません。

Linux/macOS例:

```sh
printf "TEXT hello world\n" | nc -u -w1 192.168.4.1 4242
printf "KEY ENTER\n" | nc -u -w1 192.168.4.1 4242
```

PowerShell例:

```powershell
$udp = [System.Net.Sockets.UdpClient]::new()
$bytes = [Text.Encoding]::ASCII.GetBytes("TEXT hello world`n")
$udp.Send($bytes, $bytes.Length, "192.168.4.1", 4242)
$udp.Close()
```

## プロトコル

### 文字列入力

```text
TEXT hello world
```

`TEXT ` 以降をASCIIキーボード入力として送信します。接頭辞なしのUDP payloadも文字列として扱います。

### 特殊キー

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
```

## 現時点の制限

- 初期版はASCII中心です。
- HIDのキー配列はUS配列基準です。
- 日本語入力はIME側の状態に依存します。
- Pico WHはAPモードで動作します。
- DHCPは最小実装です。
- Web UI、BLE入力は今後の拡張候補です。

## 次の拡張候補

- スマホ向けWeb UIをPico上で配信する
- WebSocketまたはHTTP API対応
- BLE GATT入力対応
- JP配列向けキーマップ
- 修飾キー同時押しやショートカットコマンド対応
