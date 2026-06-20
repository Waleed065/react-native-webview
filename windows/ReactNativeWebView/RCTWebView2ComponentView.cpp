// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "pch.h"
#include "RCTWebView2ComponentView.h"
#include "ReactWebViewHelpers.h"

#include <winrt/Windows.System.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <limits>
#include <optional>

namespace winrt::ReactNativeWebView::implementation {

// Static pending-navigation state for onShouldStartLoadWithRequest.
std::unordered_map<double, std::pair<winrt::weak_ref<winrt::IInspectable>, std::string>>
    RCTWebView2ComponentView::s_pendingNavigations;
double RCTWebView2ComponentView::s_nextLockIdentifier{0};

void RegisterRCTWebView2ComponentView(
    winrt::Microsoft::ReactNative::IReactPackageBuilder const &packageBuilder) noexcept {
    
    // Verify we can QI for IReactPackageBuilderFabric
    auto fabricBuilder = packageBuilder.try_as<winrt::Microsoft::ReactNative::IReactPackageBuilderFabric>();
    if (!fabricBuilder) {
        return;
    }
    
    RNCWebViewCodegen::RegisterRCTWebView2NativeComponent<RCTWebView2ComponentView>(
        packageBuilder,
        [](const winrt::Microsoft::ReactNative::Composition::IReactCompositionViewComponentBuilder &builder) {
            builder.as<winrt::Microsoft::ReactNative::IReactViewComponentBuilder>().XamlSupport(true);
            
            // Use SetContentIslandComponentViewInitializer for XAML hosting
            builder.SetContentIslandComponentViewInitializer(
                [](const winrt::Microsoft::ReactNative::Composition::ContentIslandComponentView &islandView) noexcept {
                    auto userData = winrt::make_self<RCTWebView2ComponentView>();
                    userData->InitializeContentIsland(islandView);
                    islandView.UserData(*userData);
                });

            // Register the measure function - WebView fills available space
            builder.as<winrt::Microsoft::ReactNative::IReactViewComponentBuilder>().SetMeasureContentHandler(
                [](winrt::Microsoft::ReactNative::ShadowNode const& /*shadowNode*/,
                   winrt::Microsoft::ReactNative::LayoutContext const&,
                   winrt::Microsoft::ReactNative::LayoutConstraints const& constraints) noexcept {
                    // WebView should fill available space. Cap infinity to reasonable defaults.
                    float w = constraints.MaximumSize.Width;
                    float h = constraints.MaximumSize.Height;
                    if (w == std::numeric_limits<float>::infinity() || w <= 0) w = 300;
                    if (h == std::numeric_limits<float>::infinity() || h <= 0) h = 200;
                    return winrt::Windows::Foundation::Size{w, h};
                });
        });
}

void RCTWebView2ComponentView::InitializeContentIsland(
    const winrt::Microsoft::ReactNative::Composition::ContentIslandComponentView &islandView) {
    
    // Create WebView2 control
    m_webView = winrt::Microsoft::UI::Xaml::Controls::WebView2();
    m_webView.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
    m_webView.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Stretch);

    // Register WebView2 events
    RegisterEvents();

    // Create XamlIsland and connect
    m_island = winrt::Microsoft::UI::Xaml::XamlIsland{};
    m_island.Content(m_webView);
    islandView.Connect(m_island.ContentIsland());
    m_islandView = winrt::make_weak(islandView);
    
    // CoreWebView2 initialization is deferred until UpdateProps so CreationProperties
    // (incognito, etc.) can be set first.
}

void RCTWebView2ComponentView::UpdateProps(
    const winrt::Microsoft::ReactNative::ComponentView &view,
    const winrt::com_ptr<RNCWebViewCodegen::RCTWebView2Props> &newProps,
    const winrt::com_ptr<RNCWebViewCodegen::RCTWebView2Props> &oldProps) noexcept {
    
    try {
        BaseRCTWebView2::UpdateProps(view, newProps, oldProps);
    } catch (...) {
        // Continue with prop application even if base fails
    }
    
    if (!m_webView || !newProps) {
        return;
    }

    // Apply messaging enabled
    m_messagingEnabled = newProps->messagingEnabled;
    
    // Apply link handling
    if (newProps->linkHandlingEnabled.has_value()) {
        m_linkHandlingEnabled = newProps->linkHandlingEnabled.value();
    }
    
    // Apply injected JavaScript (runs at document end)
    if (newProps->injectedJavaScript.has_value()) {
        m_injectedJavascript = winrt::to_hstring(newProps->injectedJavaScript.value());
    }
    
    // Apply injected JavaScript before content loaded (runs before page scripts)
    if (newProps->injectedJavaScriptBeforeContentLoaded.has_value()) {
        m_injectedJavascriptBeforeContentLoaded = winrt::to_hstring(newProps->injectedJavaScriptBeforeContentLoaded.value());
    }
    
    // Apply user agent / application name
    if (newProps->userAgent.has_value()) {
        m_userAgent = winrt::to_hstring(newProps->userAgent.value());
    }
    if (newProps->applicationNameForUserAgent.has_value()) {
        m_applicationNameForUserAgent = newProps->applicationNameForUserAgent.value();
    }
    
    // Store settings that must be applied when CoreWebView2 is ready
    m_javaScriptEnabled = newProps->javaScriptEnabled;
    m_webviewDebuggingEnabled = newProps->webviewDebuggingEnabled;
    m_cacheEnabled = newProps->cacheEnabled;
    if (newProps->incognito.has_value()) {
        m_incognito = newProps->incognito.value();
    }
    
    ApplySettings();
    
    // Inject the JS bridge if messaging was just enabled and CoreWebView2 is ready.
    bool messagingBecameEnabled = !oldProps ||
        (!oldProps->messagingEnabled && newProps->messagingEnabled);
    if (messagingBecameEnabled && m_webView.CoreWebView2()) {
        InjectMessagingBridge();
    }
    
    // Re-inject document-start script if the prop changed.
    bool beforeContentChanged = !oldProps ||
        newProps->injectedJavaScriptBeforeContentLoaded != oldProps->injectedJavaScriptBeforeContentLoaded;
    if (beforeContentChanged) {
        ApplyInjectedJavaScriptBeforeContentLoaded();
    }

    // Clear disk cache when cacheEnabled transitions from true to false.
    if (oldProps && newProps->cacheEnabled != oldProps->cacheEnabled && !newProps->cacheEnabled) {
        if (m_webView && m_webView.CoreWebView2()) {
            try {
                m_webView.CoreWebView2().Profile().ClearBrowsingDataAsync(
                    winrt::Microsoft::Web::WebView2::Core::CoreWebView2BrowsingDataKinds::DiskCache);
            } catch (...) {
                // Ignore failures.
            }
        }
    }
    
    // Trigger CoreWebView2 initialization now that CreationProperties are configured.
    if (!m_ensureCoreWebView2Called) {
        m_ensureCoreWebView2Called = true;
        try {
            m_webView.EnsureCoreWebView2Async();
        } catch (...) {
            // Initialization failure will be surfaced via OnCoreWebView2Initialized.
        }
    }
    
    // Handle source navigation
    if (newProps->newSource.uri.has_value() && !newProps->newSource.uri.value().empty()) {
        try {
            auto uri = winrt::Windows::Foundation::Uri(winrt::to_hstring(newProps->newSource.uri.value()));
            m_webView.Source(uri);
        } catch (...) {
            // Invalid URI
        }
    } else if (newProps->newSource.html.has_value() && !newProps->newSource.html.value().empty()) {
        if (m_webView.CoreWebView2()) {
            try {
                m_webView.NavigateToString(winrt::to_hstring(newProps->newSource.html.value()));
            } catch (...) {
                // Navigation failed
            }
        } else {
            // CoreWebView2 not ready yet - save HTML for later navigation in OnCoreWebView2Initialized
            m_pendingHtml = newProps->newSource.html.value();
        }
    }
}

void RCTWebView2ComponentView::UpdateLayoutMetrics(
    const winrt::Microsoft::ReactNative::ComponentView& /*view*/,
    const winrt::Microsoft::ReactNative::LayoutMetrics& newLayoutMetrics,
    const winrt::Microsoft::ReactNative::LayoutMetrics& /*oldLayoutMetrics*/) noexcept {
    if (m_webView && newLayoutMetrics.Frame.Width > 0 && newLayoutMetrics.Frame.Height > 0) {
        m_webView.Width(newLayoutMetrics.Frame.Width);
        m_webView.Height(newLayoutMetrics.Frame.Height);
    }
}

void RCTWebView2ComponentView::RegisterEvents() {
    if (!m_webView) return;
    
    m_navigationStartingRevoker = m_webView.NavigationStarting(
        winrt::auto_revoke, [this](auto const& /*sender*/, auto const& args) {
            OnNavigationStarting(args);
        });

    m_navigationCompletedRevoker = m_webView.NavigationCompleted(
        winrt::auto_revoke, [this](auto const& /*sender*/, auto const& args) {
            OnNavigationCompleted(args);
        });

    m_CoreWebView2InitializedRevoker = m_webView.CoreWebView2Initialized(
        winrt::auto_revoke, [this](auto const& /*sender*/, auto const& args) {
            OnCoreWebView2Initialized(args);
        });
}

void RCTWebView2ComponentView::RegisterCoreWebView2Events() {
    if (!m_webView || !m_webView.CoreWebView2()) return;
    
    auto coreWebView = m_webView.CoreWebView2();
    
    m_webResourceRequestedRevoker = coreWebView.WebResourceRequested(
        winrt::auto_revoke,
        [this](auto const& sender, auto const& args) {
            OnCoreWebView2ResourceRequested(sender, args);
        });

    m_CoreWebView2DOMContentLoadedRevoker = coreWebView.DOMContentLoaded(
        winrt::auto_revoke,
        [this](auto const& sender, auto const& args) {
            OnCoreWebView2DOMContentLoaded(sender, args);
        });

    m_sourceChangedRevoker = coreWebView.SourceChanged(
        winrt::auto_revoke,
        [this](auto const& sender, auto const& args) {
            OnCoreWebView2SourceChanged(sender, args);
        });

    m_newWindowRequestedRevoker = coreWebView.NewWindowRequested(
        winrt::auto_revoke,
        [this](auto const& sender, auto const& args) {
            OnCoreWebView2NewWindowRequested(sender, args);
        });
}

void RCTWebView2ComponentView::OnNavigationStarting(
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args) {
    
    std::string url;
    try {
        url = winrt::to_string(args.Uri());
    } catch (...) {
        // If we cannot read the URI, cancel the navigation to be safe.
        args.Cancel(true);
        return;
    }

    auto navigationType = NavigationTypeToString(args.NavigationKind());

    // If this URL was already approved by JS (e.g. retry after shouldStartLoadWithRequest),
    // allow it without re-emitting the event.
    auto approvedIt = m_approvedUrls.find(url);
    if (approvedIt != m_approvedUrls.end()) {
        m_approvedUrls.erase(approvedIt);
    } else {
        auto eventEmitter = EventEmitter();
        if (!eventEmitter) {
            // Event bridge isn't ready yet; we can't ask JS, so allow the navigation.
            // Subsequent navigations will go through the normal shouldStartLoadWithRequest flow.
        } else {
            // Cancel the navigation and ask JS whether to proceed.
            args.Cancel(true);

            double lockIdentifier = ++s_nextLockIdentifier;
            s_pendingNavigations[lockIdentifier] = {winrt::make_weak(static_cast<winrt::IInspectable>(*this)), url};

            // Defensive cap to prevent unbounded growth if JS never responds.
            constexpr size_t maxPendingNavigations = 32;
            if (s_pendingNavigations.size() > maxPendingNavigations) {
                auto oldest = std::min_element(
                    s_pendingNavigations.begin(),
                    s_pendingNavigations.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });
                if (oldest != s_pendingNavigations.end()) {
                    s_pendingNavigations.erase(oldest);
                }
            }

            RNCWebViewCodegen::RCTWebView2EventEmitter::OnShouldStartLoadWithRequest event;
            event.url = url;
            event.loading = true;
            event.title = "";
            event.canGoBack = m_webView ? m_webView.CanGoBack() : false;
            event.canGoForward = m_webView ? m_webView.CanGoForward() : false;
            event.lockIdentifier = lockIdentifier;
            event.navigationType = navigationType;
            event.isTopFrame = true; // NavigationStarting only fires for top-frame navigations
            eventEmitter->onShouldStartLoadWithRequest(event);
            return;
        }
    }

    // Emit loadingStart for navigations that are allowed to proceed.
    try {
        if (auto eventEmitter = EventEmitter()) {
            RNCWebViewCodegen::RCTWebView2EventEmitter::OnLoadingStart event;
            event.url = url;
            event.loading = true;
            event.title = "";
            event.canGoBack = m_webView ? m_webView.CanGoBack() : false;
            event.canGoForward = m_webView ? m_webView.CanGoForward() : false;
            event.navigationType = navigationType;
            eventEmitter->onLoadingStart(event);
        }
    } catch (...) {
        // Event dispatch failure is non-fatal
    }
}

void RCTWebView2ComponentView::OnNavigationCompleted(
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2NavigationCompletedEventArgs const& args) {
    
    std::string url;
    if (m_webView && m_webView.Source()) {
        try {
            url = winrt::to_string(m_webView.Source().AbsoluteCanonicalUri());
        } catch (...) {
            url = "";
        }
    }
    
    if (!args.IsSuccess()) {
        // Surface the failure as a loading error. We do not emit loadingFinish.
        auto status = args.WebErrorStatus();
        EmitLoadingError(url, "WebView2Navigation", static_cast<int32_t>(status), "Navigation failed");
        return;
    }

    // Surface HTTP errors (4xx/5xx) the same way Android/iOS do.
    auto httpStatus = args.HttpStatusCode();
    if (httpStatus >= 400) {
        EmitHttpError(url, static_cast<int32_t>(httpStatus));
    }
    
    try {
        if (auto eventEmitter = EventEmitter()) {
            RNCWebViewCodegen::RCTWebView2EventEmitter::OnLoadingFinish event;
            event.url = url;
            event.loading = false;
            event.title = "";
            event.canGoBack = m_webView ? m_webView.CanGoBack() : false;
            event.canGoForward = m_webView ? m_webView.CanGoForward() : false;
            // navigationType is intentionally omitted from loadingFinish to match iOS/Android.
            eventEmitter->onLoadingFinish(event);
        }
    } catch (...) {
        // Event dispatch failure is non-fatal
    }
}

void RCTWebView2ComponentView::OnCoreWebView2Initialized(
    winrt::Microsoft::UI::Xaml::Controls::CoreWebView2InitializedEventArgs const& args) {
    
    if (!m_webView) return;
    
    // If the WebView2 runtime failed to initialize, surface it as a loading error.
    auto initHr = args.Exception();
    if (initHr != S_OK) {
        try {
            auto error = winrt::hresult_error(initHr);
            EmitLoadingError("", "WebView2Initialization", error.code(), winrt::to_string(error.message()));
        } catch (...) {
            EmitLoadingError("", "WebView2Initialization", E_FAIL, "Failed to initialize WebView2 runtime");
        }
        return;
    }
    
    if (!m_webView.CoreWebView2()) return;
    
    // Capture the default user agent so applicationNameForUserAgent can append to it.
    try {
        m_defaultUserAgent = m_webView.CoreWebView2().Settings().UserAgent();
    } catch (...) {
        m_defaultUserAgent = L"";
    }
    
    RegisterCoreWebView2Events();
    ApplySettings();
    RegisterMessagingBridge();
    InjectMessagingBridge();
    ApplyInjectedJavaScriptBeforeContentLoaded();
    
    // Navigate to deferred HTML source if pending
    if (!m_pendingHtml.empty()) {
        try {
            m_webView.NavigateToString(winrt::to_hstring(m_pendingHtml));
        } catch (...) {
            // Deferred navigation failed
        }
        m_pendingHtml.clear();
    }
}

void RCTWebView2ComponentView::OnCoreWebView2ResourceRequested(
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2 const& /*sender*/,
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebResourceRequestedEventArgs const& /*args*/) {
    // Handle web resource requests if needed
}

void RCTWebView2ComponentView::OnCoreWebView2DOMContentLoaded(
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2 const& sender,
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2DOMContentLoadedEventArgs const& /*args*/) {
    
    if (!m_injectedJavascript.empty()) {
        sender.ExecuteScriptAsync(m_injectedJavascript);
    }
}

void RCTWebView2ComponentView::OnCoreWebView2SourceChanged(
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2 const& /*sender*/,
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2SourceChangedEventArgs const& /*args*/) {
    
    if (auto eventEmitter = EventEmitter()) {
        RNCWebViewCodegen::RCTWebView2EventEmitter::OnSourceChanged event;
        if (m_webView && m_webView.Source()) {
            event.url = winrt::to_string(m_webView.Source().AbsoluteCanonicalUri());
        }
        event.loading = false;
        event.title = "";
        event.canGoBack = m_webView ? m_webView.CanGoBack() : false;
        event.canGoForward = m_webView ? m_webView.CanGoForward() : false;
        event.navigationType = "other";
        eventEmitter->onSourceChanged(event);
    }
}

void RCTWebView2ComponentView::OnCoreWebView2NewWindowRequested(
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2 const& /*sender*/,
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2NewWindowRequestedEventArgs const& args) {
    
    // Always mark the request as handled so WebView2 itself does not try to create a popup.
    args.Handled(true);
    
    if (m_linkHandlingEnabled) {
        if (auto eventEmitter = EventEmitter()) {
            RNCWebViewCodegen::RCTWebView2EventEmitter::OnOpenWindow event;
            event.targetUrl = winrt::to_string(args.Uri());
            eventEmitter->onOpenWindow(event);
        }
    } else {
        try {
            winrt::Windows::Foundation::Uri uri(args.Uri());
            winrt::Windows::System::Launcher::LaunchUriAsync(uri);
        } catch (winrt::hresult_error&) {
            // Do Nothing
        }
    }
}

void RCTWebView2ComponentView::OnMessagePosted(winrt::hstring const& message) {
    HandleMessageFromJS(message);
}

void RCTWebView2ComponentView::HandleMessageFromJS(winrt::hstring const& message) {
    try {
        winrt::Windows::Data::Json::JsonObject jsonObject;
        if (winrt::Windows::Data::Json::JsonObject::TryParse(message, jsonObject) && jsonObject.HasKey(L"type")) {
            if (auto v = jsonObject.Lookup(L"type"); v && v.ValueType() == winrt::Windows::Data::Json::JsonValueType::String) {
                auto type = v.GetString();
                if (type == L"__alert") {
                    // Use Win32 MessageBoxW instead of UWP MessageDialog which is incompatible
                    // with Win32/WinAppSDK Composition apps
                    auto alertMsg = jsonObject.GetNamedString(L"message");
                    MessageBoxW(nullptr, alertMsg.c_str(), L"Alert", MB_OK);
                    return;
                }
            }
        }

        if (auto eventEmitter = EventEmitter()) {
            RNCWebViewCodegen::RCTWebView2EventEmitter::OnMessage event;
            if (m_webView && m_webView.Source()) {
                event.url = winrt::to_string(m_webView.Source().AbsoluteCanonicalUri());
            }
            event.data = winrt::to_string(message);
            event.loading = false;
            event.title = "";
            event.canGoBack = m_webView ? m_webView.CanGoBack() : false;
            event.canGoForward = m_webView ? m_webView.CanGoForward() : false;
            eventEmitter->onMessage(event);
        }
    } catch (...) {
        // Message handling failure is non-fatal
    }
}

void RCTWebView2ComponentView::WriteCookiesToWebView2(std::string const& cookies) {
    if (!m_webView || !m_webView.CoreWebView2()) return;
    
    auto cookieManager = m_webView.CoreWebView2().CookieManager();
    auto cookiesList = ReactWebViewHelpers::SplitString(cookies, ";,");
    for (const auto& cookie_str : cookiesList) {
        auto cookieData = ReactWebViewHelpers::ParseSetCookieHeader(ReactWebViewHelpers::TrimString(cookie_str));

        if (!cookieData.count("Name") || !cookieData.count("Value")) {
            continue;
        }

        auto cookie = cookieManager.CreateCookie(
            winrt::to_hstring(cookieData["Name"]),
            winrt::to_hstring(cookieData["Value"]),
            cookieData.count("Domain") ? winrt::to_hstring(cookieData["Domain"]) : L"",
            cookieData.count("Path") ? winrt::to_hstring(cookieData["Path"]) : L"");
        cookieManager.AddOrUpdateCookie(cookie);
    }
}

void RCTWebView2ComponentView::ResolvePendingNavigation(double lockIdentifier, bool shouldStart) noexcept {
    try {
        auto it = s_pendingNavigations.find(lockIdentifier);
        if (it == s_pendingNavigations.end()) {
            return;
        }

        auto weakView = it->second.first;
        auto url = it->second.second;
        s_pendingNavigations.erase(it);

        if (!shouldStart) {
            return;
        }

        if (auto inspectable = weakView.get()) {
            auto view = winrt::get_self<RCTWebView2ComponentView>(inspectable);
            view->m_approvedUrls.insert(url);
            view->NavigateToUrl(url);
        }
    } catch (...) {
        // Resolution failure is non-fatal
    }
}

void RCTWebView2ComponentView::NavigateToUrl(std::string const& url) noexcept {
    if (!m_webView || url.empty()) {
        return;
    }

    try {
        if (m_webView.CoreWebView2()) {
            m_webView.CoreWebView2().Navigate(winrt::to_hstring(url));
        } else {
            m_webView.Source(winrt::Windows::Foundation::Uri(winrt::to_hstring(url)));
        }
    } catch (...) {
        // Navigation retry failed
    }
}

void RCTWebView2ComponentView::ApplySettings() {
    if (!m_webView || !m_webView.CoreWebView2()) {
        return;
    }

    auto settings = m_webView.CoreWebView2().Settings();
    settings.IsScriptEnabled(m_javaScriptEnabled);

    if (m_webviewDebuggingEnabled.has_value()) {
        settings.AreDevToolsEnabled(m_webviewDebuggingEnabled.value());
    }

    ApplyUserAgent();
    ApplyProfileSettings();
}

void RCTWebView2ComponentView::ApplyUserAgent() {
    if (!m_webView || !m_webView.CoreWebView2()) {
        return;
    }

    auto settings = m_webView.CoreWebView2().Settings();
    if (!m_userAgent.empty()) {
        // Explicit userAgent wins, matching Android/iOS behavior.
        settings.UserAgent(m_userAgent);
    } else if (!m_applicationNameForUserAgent.empty() && !m_defaultUserAgent.empty()) {
        // Append applicationNameForUserAgent to the default UA.
        settings.UserAgent(m_defaultUserAgent + L" " + winrt::to_hstring(m_applicationNameForUserAgent));
    }
}

void RCTWebView2ComponentView::ApplyProfileSettings() {
    if (!m_webView) {
        return;
    }

    // Pre-initialization profile options (e.g. in-private mode via CreationProperties)
    // are not wired in this RNW/WebView2 version. Once CoreWebView2 is created, the
    // profile cannot be changed, so incognito must be set before first render on the JS
    // side if the underlying runtime supports it.
    // cacheEnabled transitions are handled directly in UpdateProps.
}

void RCTWebView2ComponentView::ApplyInjectedJavaScriptBeforeContentLoaded() {
    if (!m_webView || !m_webView.CoreWebView2() || m_injectedJavascriptBeforeContentLoaded.empty()) {
        return;
    }

    try {
        // This script runs before any page scripts, matching iOS/Android document-start behavior.
        auto op = m_webView.CoreWebView2().AddScriptToExecuteOnDocumentCreatedAsync(m_injectedJavascriptBeforeContentLoaded);
        (void)op; // fire-and-forget; the script id is not needed for basic injection.
    } catch (...) {
        // Injection failure is non-fatal.
    }
}

void RCTWebView2ComponentView::RegisterMessagingBridge() {
    if (!m_webView || !m_webView.CoreWebView2()) {
        return;
    }

    // Register the native -> JS message handler once.
    m_messageReceivedRevoker = m_webView.CoreWebView2().WebMessageReceived(
        winrt::auto_revoke,
        [this](auto const& /*sender*/, winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebMessageReceivedEventArgs const& messageArgs) {
            if (!m_messagingEnabled) {
                return;
            }
            try {
                auto message = messageArgs.TryGetWebMessageAsString();
                OnMessagePosted(message);
            } catch (...) {
                // Ignore non-string messages.
            }
        });
}

void RCTWebView2ComponentView::InjectMessagingBridge() {
    if (!m_messagingEnabled || !m_webView || !m_webView.CoreWebView2()) {
        return;
    }

    static const winrt::hstring bridgeScript = LR"(
        window.alert = function (msg) {
            window.chrome.webview.postMessage(JSON.stringify({ type: '__alert', message: msg }));
        };
        window.ReactNativeWebView = {
            postMessage: function (data) {
                window.chrome.webview.postMessage(String(data));
            }
        };
        window.chrome.webview.addEventListener('message', function (e) {
            var origin = window.location.origin;
            var event = new MessageEvent('message', { data: e.data, origin: origin });
            window.dispatchEvent(event);
            document.dispatchEvent(event);
        });
    )";

    try {
        // Inject into the current document immediately (in case it is already loaded).
        m_webView.CoreWebView2().ExecuteScriptAsync(bridgeScript);

        // Also register for future documents so the bridge is available before page scripts run.
        if (!m_messagingBridgeDocumentScriptAdded) {
            auto op = m_webView.CoreWebView2().AddScriptToExecuteOnDocumentCreatedAsync(bridgeScript);
            (void)op; // fire-and-forget
            m_messagingBridgeDocumentScriptAdded = true;
        }
    } catch (...) {
        // JS injection failure is non-fatal
    }
}

std::string RCTWebView2ComponentView::NavigationTypeToString(
    winrt::Microsoft::Web::WebView2::Core::CoreWebView2NavigationKind kind) const noexcept {
    using NavigationKind = winrt::Microsoft::Web::WebView2::Core::CoreWebView2NavigationKind;
    switch (kind) {
        case NavigationKind::BackOrForward:
            return "backforward";
        case NavigationKind::Reload:
            return "reload";
        case NavigationKind::NewDocument:
        default:
            return "other";
    }
}

void RCTWebView2ComponentView::EmitLoadingError(
    std::string const& url,
    std::string const& domain,
    int32_t code,
    std::string const& description) noexcept {
    try {
        if (auto eventEmitter = EventEmitter()) {
            RNCWebViewCodegen::RCTWebView2EventEmitter::OnLoadingError event;
            event.url = url;
            event.loading = false;
            event.title = "";
            event.canGoBack = m_webView ? m_webView.CanGoBack() : false;
            event.canGoForward = m_webView ? m_webView.CanGoForward() : false;
            event.domain = domain;
            event.code = code;
            event.description = description;
            eventEmitter->onLoadingError(event);
        }
    } catch (...) {
        // Event dispatch failure is non-fatal
    }
}

void RCTWebView2ComponentView::EmitHttpError(std::string const& url, int32_t statusCode) noexcept {
    try {
        if (auto eventEmitter = EventEmitter()) {
            RNCWebViewCodegen::RCTWebView2EventEmitter::OnHttpError event;
            event.url = url;
            event.loading = false;
            event.title = "";
            event.canGoBack = m_webView ? m_webView.CanGoBack() : false;
            event.canGoForward = m_webView ? m_webView.CanGoForward() : false;
            event.statusCode = statusCode;
            event.description = "HTTP error";
            eventEmitter->onHttpError(event);
        }
    } catch (...) {
        // Event dispatch failure is non-fatal
    }
}

// Command handlers
void RCTWebView2ComponentView::HandleGoBackCommand() noexcept {
    if (m_webView && m_webView.CanGoBack()) {
        m_webView.GoBack();
    }
}

void RCTWebView2ComponentView::HandleGoForwardCommand() noexcept {
    if (m_webView && m_webView.CanGoForward()) {
        m_webView.GoForward();
    }
}

void RCTWebView2ComponentView::HandleReloadCommand() noexcept {
    if (m_webView) {
        m_webView.Reload();
    }
}

void RCTWebView2ComponentView::HandleStopLoadingCommand() noexcept {
    if (m_webView && m_webView.CoreWebView2()) {
        m_webView.CoreWebView2().Stop();
    }
}

void RCTWebView2ComponentView::HandleInjectJavaScriptCommand(std::string javascript) noexcept {
    if (m_webView && m_webView.CoreWebView2()) {
        m_webView.CoreWebView2().ExecuteScriptAsync(winrt::to_hstring(javascript));
    }
}

void RCTWebView2ComponentView::HandleRequestFocusCommand() noexcept {
    if (m_webView) {
        m_webView.Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
    }
}

void RCTWebView2ComponentView::HandlePostMessageCommand(std::string data) noexcept {
    if (m_webView && m_webView.CoreWebView2()) {
        m_webView.CoreWebView2().PostWebMessageAsString(winrt::to_hstring(data));
    }
}

void RCTWebView2ComponentView::HandleLoadUrlCommand(std::string url) noexcept {
    NavigateToUrl(url);
}

void RCTWebView2ComponentView::HandleClearCacheCommand(bool includeDiskFiles) noexcept {
    if (!m_webView || !m_webView.CoreWebView2()) {
        return;
    }

    auto profile = m_webView.CoreWebView2().Profile();
    if (!profile) {
        return;
    }

    try {
        // For PWA/offline use cases, avoid clearing all profile data.
        // Clear disk cache only by default. When includeDiskFiles is true,
        // also clear DOM storage (localStorage, IndexedDB, CacheStorage/service workers).
        auto dataKinds = winrt::Microsoft::Web::WebView2::Core::CoreWebView2BrowsingDataKinds::DiskCache;
        if (includeDiskFiles) {
            dataKinds = dataKinds |
                winrt::Microsoft::Web::WebView2::Core::CoreWebView2BrowsingDataKinds::AllDomStorage;
        }
        profile.ClearBrowsingDataAsync(dataKinds);
    } catch (...) {
        // Ignore failures.
    }
}

} // namespace winrt::ReactNativeWebView::implementation
