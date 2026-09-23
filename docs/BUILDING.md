# Building and packaging

## Windows

Install Visual Studio 2022 C++ build tools, CMake, and .NET 9 SDK. From the repository root:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
dotnet build desktop/gui/TandemAudio.Desktop.csproj -c Release
```

The desktop application launches `syncaudio.exe` from its own directory. To make the self-contained installer, install NSIS 3.12 and run `scripts/build-windows-release.ps1`; pass `-Makensis` if it is outside `PATH`. The installer installs for the current user under `%LOCALAPPDATA%\Programs\Tandem Audio`, adds Start Menu and Apps & Features entries, and needs no .NET runtime or compiler on the destination computer. It deliberately does not install a blanket firewall rule; allow the app only on a trusted private network if Windows prompts. The installer removes its own files and shortcuts, while logs in `%LOCALAPPDATA%\TandemAudio\Logs` remain for the user to manage.

## iPhone

The Xcode project is `ios/SyncAudioReceiver.xcodeproj`; its scheme is `SyncAudioReceiver`, iOS deployment target is 16.0, and bundle identifier is `com.anantchdryy.SyncAudioReceiver`. Xcode automatically manages signing, but you must select your own Apple team and change the bundle identifier if it conflicts. The app declares Bonjour `_tandemaudio._tcp`, Local Network usage, and background audio in `Info.plist`. It does not request microphone access because no acoustic microphone calibration is implemented. Bluetooth is an OS output route; the app does not scan or pair accessories and declares no Bluetooth privacy permission.

On a Mac with Xcode:

```sh
xcodebuild -project ios/SyncAudioReceiver.xcodeproj -scheme SyncAudioReceiver \
  -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build
```

Select an attached iPhone and your development team in Xcode, then choose **Product > Run**. For a signed archive choose **Product > Archive**. A signed device install/archive needs macOS, Xcode, an Apple Account, and the device or provisioning configuration. The Windows environment can edit the project and GitHub Actions can build and test the simulator, but neither produces an installable signed iPhone app for this account.

## Release vs Debug

The C++ Release engine rejects network impairment options. Developer diagnostics and manual route adjustment remain available in the app; raw testing controls are not presented in the normal interface. Retain both Debug and Release builds when investigating bugs.
