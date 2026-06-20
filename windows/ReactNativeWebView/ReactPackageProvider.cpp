#include "pch.h"

#include "ReactPackageProvider.h"
#if __has_include("ReactPackageProvider.g.cpp")
#include "ReactPackageProvider.g.cpp"
#endif

#include "ReactNativeWebview.h"
#include "RCTWebView2ComponentView.h"

using namespace winrt::Microsoft::ReactNative;

namespace winrt::ReactNativeWebView::implementation
{

void ReactPackageProvider::CreatePackage(IReactPackageBuilder const &packageBuilder) noexcept
{
  AddAttributedModules(packageBuilder, true);

  try {
    RegisterRCTWebView2ComponentView(packageBuilder);
  } catch (winrt::hresult_error const&) {
  } catch (std::exception const&) {
  } catch (...) {
  }
}

} // namespace winrt::ReactNativeWebView::implementation
