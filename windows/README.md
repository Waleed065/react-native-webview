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
- `onLoadingStart` / `onLoadingFinish` / `onLoadingError` / `onHttpError`
- `injectedJavaScript` / `injectedJavaScriptBeforeContentLoaded`
- `userAgent` / `applicationNameForUserAgent`
- `webviewDebuggingEnabled`
- `javaScriptEnabled`
- `cacheEnabled` (clears disk cache when set to `false`; WebView2 has no per-request no-cache mode)

## What is NOT supported

Legacy UWP / Old Architecture (Paper) / WinUI 2 / C# project templates are intentionally not supported. The old `useWebView2` prop has been removed because WebView2 is the only implementation.

The following shared props/events are declared but not yet wired on Windows:

- `onLoadingProgress`
- `onScroll`
- `source.headers` / `source.method` / `source.body` / `source.baseUrl`
- `basicAuthCredential`
- `mediaPlaybackRequiresUserAction`
- `javaScriptCanOpenWindowsAutomatically`
- `showsHorizontalScrollIndicator` / `showsVerticalScrollIndicator`
- `incognito` (in-private profile is not wired to the underlying WebView2 runtime in this version)

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

## PWA / offline usage

WebView2 fully supports service workers, CacheStorage, and localStorage, so loading a PWA that works offline is supported out of the box.

The messaging bridge follows the standard `react-native-webview` contract used by your existing PWA:

- PWA → native: `window.ReactNativeWebView.postMessage(JSON.stringify({ type, payload }))`
- Native → PWA: dispatched as a `MessageEvent` on `window`/`document` with `origin` set to the page origin.

To keep offline/PWA caching working:

- Keep `cacheEnabled={true}` (default). Setting it to `false` clears disk cache.
- `incognito` is not wired on Windows, so the WebView always uses the default (non-private) profile. If it becomes supported in the future, avoid it for PWAs because in-private profiles do not persist storage across sessions.
- Avoid calling `clearCache(true)` unless you intentionally want to reset offline data. `clearCache(false)` clears only disk cache; `clearCache(true)` also clears DOM storage (localStorage, IndexedDB, CacheStorage/service workers).
- If your PWA requires HTTP Basic auth or custom headers, note that `basicAuthCredential` and `source.headers` are not wired yet on Windows.

## Architecture notes

- The Fabric component is registered as `RCTWebView2` on both the JS and native sides.
- The TurboModule `RNCWebViewModule` handles `shouldStartLoadWithLockIdentifier` for `onShouldStartLoadWithRequest`.
- The native implementation lives in `RCTWebView2ComponentView.cpp` and uses `winrt::Microsoft::UI::Xaml::Controls::WebView2` hosted in a `XamlIsland`.
