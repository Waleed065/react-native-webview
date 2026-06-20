import { windowsAppDriverCapabilities } from 'selenium-appium';

const { platform } = require('./jest.setup.windows');

switch (platform) {
  case 'windows':
    // WinUI3 Desktop (Win32/WinAppSDK cpp-app) New Architecture executable path.
    const appPath = process.env.WEBVIEW_APP_PATH ||
      'D:\\react-native-webview\\example\\windows\\x64\\Debug\\ReactNativeWebviewExample.exe';
    module.exports = {
      capabilities: {
        platformName: 'Windows',
        'appium:app': appPath,
        'appium:deviceName': 'WindowsPC',
      },
    };
    break;
  default:
    throw 'Unknown platform: ' + platform;
}
