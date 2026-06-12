#!/bin/bash
# List of runtime libraries needed by Xvfb
deps=(
  libpixman-1-0
  libbsd0
  libxfont2
  libxdmcp6
  libselinux1
  libaudit1
  libunwind8
  libmd0
  libxau6
  libasound2
  libpam0g
  libgl1
  zlib1g
  libbz2-1.0
  libfontenc1
  libfreetype6
  libpcre2-8-0
  libcap-ng0
  liblzma5
  libpng16-16
  libbrotli1
  libx11-6
  libxcb1
  x11-xkb-utils
  xkb-data
  whiptail
)

# Check each package, install if missing
for pkg in "${deps[@]}"; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        echo "Installing $pkg..."
        apt-get install -y --no-install-recommends "$pkg"
    else
        echo "$pkg already installed."
    fi
done



# Release version variable
release="v1.0.0-stable"

# Detect architecture
arch=$(uname -m)
case "$arch" in
    x86_64)
        url="https://github.com/jishan484/xWebVNC/releases/download/$release/Xvfb_x86.zip"
        ;;
    aarch64|arm64)
        url="https://github.com/jishan484/xWebVNC/releases/download/$release/Xvfb-arm64.zip"
        ;;
    *)
        echo "Unsupported architecture: $arch"
        exit 1
        ;;
esac

# Download here and then put it in the 
zipfile=$(basename "$url")
echo "Downloading $url..."
wget -q "$url" -O "$zipfile"

# Unzip and clean up
unzip -o "$zipfile"
rm -f "$zipfile"

#copy it to bin and rename it to Xvfb_web
# Try to move into /usr/bin, fallback to ~/webVNC/
target="/usr/bin/Xvfb_vnc"
fallback="$HOME/webVNC/Xvfb_vnc"
final_path=""

if mv "Xvfb_x86/Xvfb" "$target" 2>/dev/null; then
    echo "Installed Xvfb to $target"
    final_path=$target
else
    echo "No access to /usr/bin, using fallback..."
    mkdir -p "$HOME/webVNC"
    mv "Xvfb_x86/Xvfb" "$fallback"
    echo "Installed Xvfb to $fallback"
    final_path=$fallback
fi

echo "Done. Extracted Xvfb ($release) for $arch into ($final_path)"


# Ask user for port with whiptail
port=$(whiptail --inputbox "Enter port number for Xvfb_vnc:" 8 40 3>&1 1>&2 2>&3)

# Ask user for authentication with whiptail
if whiptail --yesno "Enable authentication?" 8 40; then
    auth_flag="-login"
    auth="yes"
else
    auth_flag=""
    auth="no"
fi

# Check if DISPLAY is set
if [[ -n "$DISPLAY" ]]; then
    dport=$(whiptail --inputbox "Another Xorg/Xvfb is installed. DISPLAY is set ($DISPLAY). Enter a display No. (0-10):" 10 40 3>&1 1>&2 2>&3)
    display_flag=":$dport"
else
    display_flag=":0"
    export DISPLAY=:0
    if ! grep -q "export DISPLAY=:0" ~/.bashrc; then
        echo "export DISPLAY=:0" >> ~/.bashrc
    fi
fi

source $HOME/.bashrc

# Create systemd service file
cat <<EOF | sudo tee /etc/systemd/system/xwebvnc.service
[Unit]
Description=XWebVNC Service
After=network.target

[Service]
ExecStart=$final_path $display_flag -screen 0 1980x1080x24 -web $port $auth_flag
Restart=no
User=root
WorkingDirectory=/root

[Install]
WantedBy=multi-user.target
EOF


# Reload systemd and enable service
sudo systemctl daemon-reexec
sudo systemctl enable xwebvnc.service
sudo systemctl start xwebvnc.service

myip=$(ip -4 route get 8.8.8.8 | grep -oP 'src \K\d+(\.\d+){3}')

# Final result popup
if [[ "$auth" == "yes" ]]; then
    whiptail --msgbox "XWebVNC Installed

Start Desktop manager e.g.: DISPLAY=$display_flag && startlxde
Open http://$myip:$port and login with your Linux username and password" 12 60
else
    whiptail --msgbox "XWebVNC Installed

Next:
Start Desktop manager e.g.: DISPLAY=$display_flag && startlxde

Open http://$myip:$port" 12 60
fi
