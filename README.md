# Alien Sun

A puzzle platform game made for [N64 Homebrew Jam 2023](https://itch.io/jam/n64brew2023)

### Building on Debian Linux

```
export N64_INST=/opt/libdragon
git clone -b unstable https://github.com/DragonMinded/libdragon.git
wget https://github.com/DragonMinded/libdragon/releases/download/toolchain-continuous-prerelease/gcc-toolchain-mips64-x86_64.deb
sudo dpkg -i gcc-toolchain-mips64-x86_64.deb
(cd libdragon && ./build.sh)
sudo apt install libeigen-dev
git clone --recurse-submodules https://github.com/9nova/aliensun.git
cd aliensun
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements.txt
make
```
