import type { TurboModule } from 'react-native';
export interface Spec extends TurboModule {
    /**
     * Check if file upload is supported.
     * @returns Promise that resolves to true if file upload is supported
     */
    isFileUploadSupported(): Promise<boolean>;
    /**
     * Callback for onShouldStartLoadWithRequest.
     * Called to determine whether loading should continue.
     * @param shouldStart - Whether to allow the load request
     * @param lockIdentifier - Unique identifier for the request
     */
    shouldStartLoadWithLockIdentifier(shouldStart: boolean, lockIdentifier: number): void;
}
export declare const RNCWebViewModule: Spec | null;
export default RNCWebViewModule;
