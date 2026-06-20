# react-native-webview on Windows

This fork targets **React Native Windows C++ New Architecture (Fabric/Bridgeless) with WinUI 3 Desktop (Win32)** only.

## Requirements

- Visual Studio 2026 (v145 platform toolset)
- Windows SDK 10.0.26100.0
- react-native-windows >= 0.82.0-preview.11
- Microsoft.WindowsAppSDK 1.8.260209005 (WinUI 3)
- WebView2 runtime

## What is supported

- WebView2 rendering via `ContentIsland`/`XamlIsland`
- Navigation (`source`, `loadUrl`, `goBack`, `goForward`, `reload`, `stopLoading`)
- Bidirectional messaging (`postMessage`, `onMessage`, `injectJavaScript`)
- `onOpenWindow` / `onSourceChanged`
- `onShouldStartLoadWithRequest`
- `injectedJavaScript`
- `userAgent`
- `webviewDebuggingEnabled`
- `javaScriptEnabled`

## What is NOT supported

Legacy UWP / Old Architecture (Paper) / WinUI 2 / C# project templates are intentionally not supported. The old `useWebView2` prop has been removed because WebView2 is the only implementation.

## Integrating into a C++ New Arch app

1. Create your RN Windows app with the `cpp-app` template:
   ```bash
   npx react-native init MyApp --template react-native@latest
   cd MyApp
   npx react-native-windows-init --overwrite --template cpp-app
   ```

2. Install this fork:
   ```bash
   npm install <your-fork-git-url>
   ```

3. Autolinking will pick up `windows/ReactNativeWebView/ReactNativeWebView.vcxproj`.

4. Build and run as usual:
   ```bash
   npx react-native run-windows
   ```

## Architecture notes

- The Fabric component is registered as `RCTWebView2` on both the JS and native sides.
- The TurboModule `RNCWebViewModule` handles `shouldStartLoadWithLockIdentifier` for `onShouldStartLoadWithRequest`.
- The native implementation lives in `RCTWebView2ComponentView.cpp` and uses `winrt::Microsoft::UI::Xaml::Controls::WebView2` hosted in a `XamlIsland`.
