#include "pch.h"

#include "ReactNativeWebview.h"
#include "RCTWebView2ComponentView.h"

namespace winrt::ReactNativeWebview
{

// See https://microsoft.github.io/react-native-windows/docs/native-platform for help writing native modules

void RNCWebViewModule::Initialize(React::ReactContext const &reactContext) noexcept {
  m_context = reactContext;
}

void RNCWebViewModule::shouldStartLoadWithLockIdentifier(bool shouldStart, double lockIdentifier) noexcept {
  // Resolve the pending onShouldStartLoadWithRequest decision on the target WebView.
  // The WebView2 control must be accessed on the UI thread, so dispatch from the JS thread.
  auto uiDispatcher = m_context.UIDispatcher();
  if (uiDispatcher) {
    uiDispatcher.Post([shouldStart, lockIdentifier]() {
      ReactNativeWebView::implementation::RCTWebView2ComponentView::ResolvePendingNavigation(lockIdentifier, shouldStart);
    });
  } else {
    ReactNativeWebView::implementation::RCTWebView2ComponentView::ResolvePendingNavigation(lockIdentifier, shouldStart);
  }
}

void RNCWebViewModule::isFileUploadSupported(React::ReactPromise<bool> &&promise) noexcept {
  // WebView2 on Windows supports file uploads
  promise.Resolve(true);
}

} // namespace winrt::ReactNativeWebview
