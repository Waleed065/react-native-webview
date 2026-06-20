/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * Portions copyright for react-native-windows:
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License.
 */
import React from 'react';
export type { NativeProps as NativeWebViewWindows } from './RCTWebView2NativeComponent';
import { WebViewSharedProps, WebViewOpenWindowEvent, WebViewNavigationEvent } from './WebViewTypes';
export interface WindowsWebViewProps extends WebViewSharedProps {
    /**
     * Boolean value that determines whether the web view should use the new chromium based edge webview.
     */
    useWebView2?: boolean;
    /**
     * Function that is invoked when the `WebView` should open a new window.
     * @platform windows
     */
    onOpenWindow?: (event: WebViewOpenWindowEvent) => void;
    /**
     * Function that is invoked when the `WebView` responds to a request to load a new resource.
     * @platform windows
     */
    onSourceChanged?: (event: WebViewNavigationEvent) => void;
}
declare const WebView: React.ForwardRefExoticComponent<WindowsWebViewProps & React.RefAttributes<{}>> & {
    isFileUploadSupported: () => Promise<boolean>;
};
export default WebView;
