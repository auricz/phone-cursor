# Project Context

When working with this codebase, prioritize readability over cleverness. Ask clarifying questions before making architectural changes or when a requirement is ambiguous. Fix all errors and warnings. Do not hardcode text or values, use variables from a centralized location for each of the 3 main components. Follow SOLID design principles. 

## About This Project

A user can use their Android phone as a mouse and keyboard to control a Windows PC. They download an Android app onto their phone and the Windows app onto their computer they want to control. If both devices are on the same network, the user can connect by entering the PC's IP and port number. If they are on different networks, then both devices use a rendezvous server to know one another's IP and port. Connections use UDP for low latency. The mouse cursor is controlled by the phone's gyroscope, not by moving the phone through space: when the phone is held flat, the cursor sits at the center of the screen; pointing the phone upward (until the phone's screen is parallel with the desktop screen) moves the cursor upward, and changing the phone's yaw (turning it left/right) moves the cursor horizontally. A centralized variable controls how much rotation is needed to reach the screen edge, expressed as a fraction of a 90-degree turn (1.0 = a full 90-degree tilt/turn reaches the edge, 0.5 = only 45 degrees is needed). Buttons on the phone app can simulate clicks, and there is also a keyboard button that will bring up the phone's keyboard, allowing the user to send keyboard input.

## Key Directories/Components

- `android_app/` - Android Studio app, to be installed on phone. Should be openable in Android Studio and uses Kotlin. Use a minimialist theme with few colors. 
- `desktop_server/` - Windows desktop app to install on Windows PC. Uses C++ for networking and simulating keyboard/mouse input.
- `rendezvous_server/` - A server to let the phone and PC connect to one another if on different private networks. Uses TS since it will be hosted using Cloudflare Workers.

## Security

All UDP traffic from the phone to the PC must have integrity protected, and also confidentiality for keyboard input. Mouse movement data (sensor data) do not need to be confidential. 