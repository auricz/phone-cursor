# PhoneCursor

Use your Android phone as a mouse and keyboard for a Windows PC. The cursor follows the phone's gyroscope: tilt up/down (pitch) and turn left/right (yaw). The phone app also has on-screen buttons to send click, and a keyboard button sends typed text. Traffic is UDP for low latency, integrity-protected, and keyboard input is encrypted.

| Component | Path | Stack |
| --- | --- | --- |
| Phone app | [android_app/](android_app/) | Kotlin, Android Studio |
| PC app | [desktop_server/](desktop_server/) | C++17, CMake, Windows only |
| Rendezvous server | [rendezvous_server/](rendezvous_server/) | TypeScript, Cloudflare Workers |

The rendezvous server is only needed to connect across different networks. If on the same local network, the phone can connect to the PC directly.

## Setup

### 1. Desktop server (Windows PC)

Requires Windows, CMake 3.20+, and Visual Studio (or Build Tools) with the C++ workload.

Open `desktop_server/` in Visual Studio and click `Build > Build All` to build the app. Start the app in VS by clicking `Debug > Start without Debugging` or run the executable at `desktop_server/out/build/x64-Debug/PhoneCursorServer.exe`.

On launch, choose **L**ocal network or **I**nternet. Allow the app through Windows Firewall when prompted (it listens on UDP port 24800).

### 2. Android app

Requires Android Studio and an Android 7.0+ (API 24) phone with a gyroscope.

1. Open `android_app/` in Android Studio and let Gradle sync.
2. Enable USB or Wifi debugging on the phone and connect it. 
3. Run the `app` configuration, it should open the app on your phone.

NOTE: If both devices are on the same Wifi network, you can only connect locally, unless you enable NAT hairpinning/loopback/reflection on your router.

### 3. Rendezvous server (Internet mode only)

Requires Node.js. To run the server locally:

```bash
cd rendezvous_server
npm install
npx wrangler login
npx wrangler dev
```

The default hostname in the config files points to a deployed Cloudflare Worker. To use your own, set `RENDEZVOUS_HOST` (and `name` in `wrangler.toml` if you change it) to your worker's `*.workers.dev` hostname, in both:

- [desktop_server/src/Config.h](desktop_server/src/Config.h) (`kRendezvousHost`)
- [android_app/app/src/main/java/com/example/phonecursor/Config.kt](android_app/app/src/main/java/com/example/phonecursor/Config.kt) (`RENDEZVOUS_HOST`)

Then rebuild the desktop and Android apps.

## Connecting

**Same network:** run the desktop server, choose **L**, find the PC's IPv4 address with `ipconfig`, and enter it and port `24800` in the phone app.

**Different networks:** run the desktop server and choose **I**. It prints a 6-digit pairing code. Enter it in the phone app's Internet mode, then confirm that the confirmation code shown on both devices matches.

## Configuration

All tunable values live in each component's central config file: `Config.h` (desktop), `Config.kt` (Android), and `src/config.ts` (rendezvous). The rotation needed to reach the screen edge is set with the yaw and pitch sliders in the phone app. For best experience, lock the screen's orientation to portrait. The phone should be held like a TV remote.
