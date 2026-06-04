# 🖥️ XWebVNC Server [![Build Xvfb (x86_64 + ARM64)](https://github.com/jishan484/xWebVNC/actions/workflows/main.yml/badge.svg)](https://github.com/jishan484/xWebVNC/actions/workflows/main.yml)

[**XWebVNC**](https://github.com/jishan484/xWebVNC/releases/tag/v1.0.0-stable) is an efficient, headless **Xorg/Xvfb-based display server** with built-in **WebVNC** capability — allowing you to stream and interact with your Linux desktop directly from any modern web browser.

---

## 💾 Install and setup
run the below command
```bash
curl -s https://raw.githubusercontent.com/jishan484/xWebVNC/main/install.sh | bash
```
> This will install the required dependencies and set up Xvfb. Once installed, you can start your desktop manager or any GUI application on the designated DISPLAY number.
> If the authentication option is chosen during setup, you must log in using your **Linux username and password**. At present, the system allows any Linux user, including root, to log in.

## 🚀 Features

- 🧠 **Headless X Server** — Works as either **Xorg** or **Xvfb**, optimized for headless environments.
- 🌐 **Built-in WebSocket & HTTP Server** — Serves both the web client and live framebuffer updates.
- ⚡ **Real-Time Compression** — Supports:
  - **JPEG** → High compression ratio with good visual fidelity
- 🪶 **Zero External Dependencies** — No x11vnc, no nginx, no proxies.
- 🔒 **Lightweight & Secure** — Ideal for servers, embedded systems, or containerized setups.
- 🧩 **Fully Self-Contained** — Includes built-in `index.html` client page.
- 🖱️**local mouse** -> use local mouse like few good webvnc app
- 🔊**Audio support** -> audio can be enabled with pulseaudio (need install it separately to enable audio).
- 📋**clipboard support** -> app uspports one way clipboard (client to server). keys: `CTRL + SHIFT + V`. to paste to terminal which needs ctrl+shift+v, you must change the key combination of terminal paste or paste it to other text pad and copy paste it from there. (this will be fixed in v1.0.2)

---

## 🧪 Example Use Cases
- Remote GUI access for headless Linux servers
- Browser-based desktops in Docker or VMs
- Lightweight remote development environments
- Embedded Linux systems with browser-based control

# build and run it youself
- prep the system
  ```sh
  apt-get update
  apt-get install -y meson ninja-build pkg-config python3 python3-pip build-essential git x11-xkb-utils libjpeg-dev
  ```
- run build dependencies
  ```bash
  bash .gitlab-ci/debian-install.sh
  ```
- setup and build
  ```sh
  meson setup build
  meson compile -C build
  ```
  
example command `meson compile -C build && ./build/hw/vfb/Xvfb :1 -screen 0 1280x720x24 -web 80`

## 🖼️ Screen Shot
<center><img width="475" height="267" alt="image" src="https://github.com/user-attachments/assets/af3ca428-9568-41b2-b0d8-ed229e3085aa" /></center>

## 🧩 Architecture

```mermaid
flowchart LR
    subgraph App["XWebVNC App"]
        subgraph Core["XWebVNC Core"]
            A1["Framebuffer Capture\n(Xorg/Xvfb modified)"]
            A2["Frame Compressor\n(JPEG damage-area)"]
            A3["Audio Capture + Codec\n(PulseAudio/ALSA → PCM)"]
        end
    
        subgraph Net["Network Layer"]
            B1["Built-in HTTP Server\n(serves index.html, JS, assets)"]
            B2["WebSocket Server\n(video/audio frames, input events)"]
            B3["Authentication Handler\n(Linux PAM / user login)"]
            B4["XwebVnc main\n(managed setup/cleanup/threads/dispatcher)"]
        end
    end

    subgraph Client["Browser Web Client"]
        C1["UI (index.html + JS)"]
        C2["Real-time Renderer\n(video frames → canvas)"]
        C3["Audio Player\n(PCM → WebAudio)"]
        C4["Input Handler\n(keyboard/mouse/clipboard/events → WebSocket)"]
    end

    %% Flow connections
    A1 --> A2
    A2 --> B2
    A3 --> B2
    A1 --> B1
    B1 --> C1
    B2 --> C2
    B2 --> C3
    C4 --> B2
    C2 -. "Displays live desktop" .-> C1
    C3 -. "Plays system audio" .-> C1
    B3 --> B2
    B4 --> B1
    B4 --> B2

```




