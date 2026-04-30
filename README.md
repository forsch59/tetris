## Dependencies
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config
sudo apt update
sudo apt install libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxxf86vm-dev libxss-dev libgl1-mesa-dev libdbus-1-dev libudev-dev libgles2-mesa-dev libegl1-mesa-dev libibus-1.0-dev fcitx-libs-dev libwayland-dev libxkbcommon-dev wayland-protocols libpipewire-0.3-dev libdecor-0-dev libxtst-dev

### Android emulator install
sudo apt install qemu-kvm libvirt-daemon-system libvirt-clients bridge-utils
sudo usermod -aG kvm $USER
flatpak install flathub com.google.AndroidStudio
flatpak run com.google.AndroidStudio

Open Android Studio and navigate to Tools > SDK Manager.
Under the SDK Platforms tab, ensure the latest Android API (e.g., API 35 or 36) is checked.

Switch to the SDK Tools tab. You must check the following:
    Android SDK Command-line Tools
    NDK (Side by side) - This is critical for C++ development.
    CMake
    Android Emulator

Click Apply to download and install.
In Android Studio, go to Tools > Device Manager (or AVD Manager).
Click Create Device, select a modern phone profile (like a Pixel), and download the recommended system image.
Start the emulator once to ensure it boots correctly, then you can close Android Studio.

### VS Code
In vscode install Android iOS Emulator

## BUILD & RUN

### Linux
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)
./Tetris

### Android emulator
Create file: android-project/local.properties
Add to file: sdk.dir=/home/fors/Android/Sdk
ANDROID_AVD_HOME=~/.var/app/com.google.AndroidStudio/config/.android/avd ~/Android/Sdk/emulator/emulator -list-avds
ANDROID_AVD_HOME=~/.var/app/com.google.AndroidStudio/config/.android/avd ~/Android/Sdk/emulator/emulator -avd YOUR_DEVICE_NAME &
cd ../android-project
./gradlew installDebug

## Cleanup

### Android emulator
~/Android/Sdk/platform-tools/adb emu kill