package fastscreen;

import fastcore.FastCore;
import fastimage.FastImage;

import java.awt.Rectangle;
import java.awt.image.BufferedImage;
import java.nio.ByteBuffer;

/**
 * High-performance Java screen capture using DXGI Desktop Duplication.
 *
 * <p>FastScreen provides zero-copy, hardware-accelerated screen capture
 * at 240-2000 FPS using DirectX 11 DXGI Desktop Duplication API.</p>
 *
 * <p>Key features:
 * <ul>
 *   <li>Single screenshot capture (BufferedImage, raw pixels, or zero-copy FastImage)</li>
 *   <li>High-FPS streaming mode for real-time computer vision and rendering</li>
 *   <li>Zero GC pressure with native DirectByteBuffer buffers and frame pooling</li>
 *   <li>Hardware GPU scaling (Point and Bilinear AA via embedded HLSL shaders)</li>
 *   <li>Native Window Capture Exclusion (Win32 display affinity)</li>
 *   <li>Multi-monitor support and resilient GDI fallback</li>
 * </ul></p>
 *
 * <p><strong>Example usage:</strong></p>
 * <pre>{@code
 * FastScreen screen = new FastScreen();
 * try {
 *     // Single capture
 *     BufferedImage img = screen.captureScreen(new Rectangle(0, 0, 1920, 1080));
 *
 *     // High-FPS streaming
 *     screen.startStream(0, 0, 1920, 1080);
 *     while (running) {
 *         if (screen.hasNewFrame()) {
 *             ByteBuffer directBuf = screen.getNextFrameDirect();
 *             // Process direct native buffer with 0 GC overhead...
 *         }
 *     }
 *     screen.stopStream();
 * } finally {
 *     screen.dispose();
 * }
 * }</pre>
 *
 * @author Andre Stubbe
 * @version 0.1.2
 * @since 2026-04-16
 */
public class FastScreen {

    static {
        // Load native library via FastCore
        FastCore.loadLibrary("fastscreen");
    }

    private long nativeHandle = 0;
    private boolean streaming = false;
    private int frameWidth = 0;
    private int frameHeight = 0;
    private int lastFrameWidth = 0;
    private int lastFrameHeight = 0;

    // Current capture region
    private int captureX = 0;
    private int captureY = 0;
    private int captureWidth = 0;
    private int captureHeight = 0;

    // Frame polling buffer - stores frame from hasNewFrame() for getNextFrame()
    private int[] bufferedFrame = null;
    private boolean frameBuffered = false;

    // Streaming FPS calculation
    private long fpsStartTimeNanos = 0;
    private int fpsFrameCount = 0;
    private volatile double currentStreamingFps = 0.0;

    /**
     * Creates a new FastScreen instance.
     */
    public FastScreen() {
        nativeHandle = nativeInit();
        if (nativeHandle == 0) {
            throw new RuntimeException("Failed to initialize FastScreen native library");
        }
    }

    /**
     * Captures full desktop screen as BufferedImage.
     *
     * @return BufferedImage containing full screenshot, or null if capture failed
     */
    public BufferedImage captureScreen() {
        int[] pixels = captureRaw(0, 0, 0, 0);
        if (pixels == null) {
            return null;
        }
        int w = lastFrameWidth > 0 ? lastFrameWidth : 1920;
        int h = lastFrameHeight > 0 ? lastFrameHeight : 1080;
        BufferedImage image = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB);
        image.setRGB(0, 0, w, h, pixels, 0, w);
        return image;
    }

    /**
     * Captures a screenshot of a specified rectangle as BufferedImage.
     *
     * @param rect Screen region to capture
     * @return BufferedImage containing screenshot, or null if capture failed
     */
    public BufferedImage captureScreen(Rectangle rect) {
        int[] pixels = captureRaw(rect.x, rect.y, rect.width, rect.height);
        if (pixels == null) {
            return null;
        }
        int w = lastFrameWidth > 0 ? lastFrameWidth : rect.width;
        int h = lastFrameHeight > 0 ? lastFrameHeight : rect.height;
        BufferedImage image = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB);
        image.setRGB(0, 0, w, h, pixels, 0, w);
        return image;
    }

    /**
     * Captures raw RGBA pixel array.
     *
     * @param x      X coordinate
     * @param y      Y coordinate
     * @param width  Capture width
     * @param height Capture height
     * @return int array of RGBA pixels, or null if capture failed
     */
    public int[] captureRaw(int x, int y, int width, int height) {
        // Check if we need to reinitialize for a different region
        if (width != captureWidth || height != captureHeight ||
                x != captureX || y != captureY) {
            // Dispose old capture
            if (nativeHandle != 0) {
                nativeDispose(nativeHandle);
            }
            // Reinitialize with new region
            nativeHandle = nativeInitRegion(x, y, width, height);
            if (nativeHandle == 0) {
                // Fall back to full screen
                nativeHandle = nativeInit();
                if (nativeHandle == 0) {
                    return null;
                }
                // Update region to full screen
                captureX = 0;
                captureY = 0;
                captureWidth = 0; // Will be determined by native
                captureHeight = 0;
            } else {
                captureX = x;
                captureY = y;
                captureWidth = width;
                captureHeight = height;
            }
        }

        int[] pixels = nativeCaptureScreen(x, y, width, height);
        if (pixels != null) {
            int w = nativeGetFrameWidth();
            int h = nativeGetFrameHeight();
            lastFrameWidth = (w > 0) ? w : width;
            lastFrameHeight = (h > 0) ? h : height;
        }
        return pixels;
    }

    /**
     * Captures entire monitor.
     *
     * @param monitorIndex Monitor index (0-based)
     * @return BufferedImage of monitor
     */
    public BufferedImage captureMonitor(int monitorIndex) {
        // TODO: Implement monitor capture
        throw new UnsupportedOperationException("Not yet implemented");
    }

    /**
     * Starts high-FPS streaming capture.
     *
     * @param x      X coordinate of capture region
     * @param y      Y coordinate of capture region
     * @param width  Capture width
     * @param height Capture height
     * @return true if streaming started successfully
     */
    public boolean startStream(int x, int y, int width, int height) {
        boolean success = nativeStartStream(x, y, width, height);
        if (success) {
            this.streaming = true;
            this.frameWidth = width;
            this.frameHeight = height;
            synchronized (this) {
                this.fpsStartTimeNanos = 0;
                this.fpsFrameCount = 0;
                this.currentStreamingFps = 0.0;
            }
        }
        return success;
    }

    /**
     * Stops streaming capture.
     */
    public void stopStream() {
        if (streaming) {
            nativeStopStream();
            this.streaming = false;
            // Clear any buffered frame
            bufferedFrame = null;
            frameBuffered = false;
            synchronized (this) {
                this.fpsStartTimeNanos = 0;
                this.fpsFrameCount = 0;
                this.currentStreamingFps = 0.0;
            }
        }
    }

    /**
     * Enables hardware-accelerated scaling for streaming.
     * This dramatically reduces CPU load by scaling on the GPU.
     * Must be called AFTER startStream().
     *
     * @param outputWidth     Target width (e.g., 640)
     * @param outputHeight    Target height (e.g., 480)
     * @param useLinearFilter true for smooth (Linear), false for pixelated (Point)
     * @return true if hardware scaling was enabled
     */
    public boolean enableHardwareScaling(int outputWidth, int outputHeight, boolean useLinearFilter) {
        if (!streaming) {
            throw new IllegalStateException("Must call startStream() before enableHardwareScaling()");
        }
        int filter = useLinearFilter ? 1 : 0;
        boolean success = nativeSetupHardwareScaling(outputWidth, outputHeight, filter);
        if (success) {
            this.frameWidth = outputWidth;
            this.frameHeight = outputHeight;
        }
        return success;
    }

    /**
     * Excludes a window from screen capture by its HWND handle.
     *
     * @param hwnd Win32 HWND window handle
     * @return true if successfully applied
     */
    public static boolean excludeWindow(long hwnd) {
        return setWindowExcluded(hwnd, true);
    }

    /**
     * Re-includes a window in screen capture by its HWND handle.
     *
     * @param hwnd Win32 HWND window handle
     * @return true if successfully applied
     */
    public static boolean includeWindow(long hwnd) {
        return setWindowExcluded(hwnd, false);
    }

    /**
     * Excludes a window from screen capture by matching its window title.
     *
     * @param windowTitle Title or substring of the window to exclude
     * @return true if window found and excluded
     */
    public static boolean excludeWindow(String windowTitle) {
        return nativeSetWindowExcludedByTitle(windowTitle, true);
    }

    /**
     * Re-includes a window in screen capture by matching its window title.
     *
     * @param windowTitle Title or substring of the window to include
     * @return true if window found and included
     */
    public static boolean includeWindow(String windowTitle) {
        return nativeSetWindowExcludedByTitle(windowTitle, false);
    }

    /**
     * Releases native resources.
     */
    public void dispose() {
        if (streaming) {
            stopStream();
        }
        if (nativeHandle != 0) {
            nativeDispose(nativeHandle);
            nativeHandle = 0;
        }
    }

    /**
     * Checks if a new frame is available in streaming mode.
     * Polls native side and buffers the frame for getNextFrame().
     *
     * @return true if new frame available
     */
    public boolean hasNewFrame() {
        if (!streaming) {
            return false;
        }

        // If we already have a buffered frame, return true
        if (frameBuffered && bufferedFrame != null) {
            return true;
        }

        // Try to get next frame from native
        bufferedFrame = nativeGetNextFrame();
        frameBuffered = (bufferedFrame != null);
        return frameBuffered;
    }

    /**
     * Gets color of a single pixel.
     *
     * @param x X coordinate
     * @param y Y coordinate
     * @return RGBA color value
     */
    public int getPixelColor(int x, int y) {
        return nativeGetPixelColor(x, y);
    }

    /**
     * Gets the next frame in streaming mode.
     * If hasNewFrame() was called before, returns the buffered frame.
     * Otherwise polls native side directly.
     *
     * @return int array of RGBA pixels, or null if no new frame
     */
    public int[] getNextFrame() {
        if (!streaming) {
            return null;
        }

        int[] frame;
        // If we have a buffered frame from hasNewFrame(), return it
        if (frameBuffered && bufferedFrame != null) {
            frame = bufferedFrame;
            bufferedFrame = null;
            frameBuffered = false;
        } else {
            // Otherwise poll native directly
            frame = nativeGetNextFrame();
        }

        if (frame != null) {
            recordFrameReceived();
        }
        return frame;
    }

    /**
     * ZERO-COPY: Gets next frame as DirectByteBuffer - NO array copying!
     * This is 10-100x faster than getNextFrame() for high-FPS streaming.
     * The buffer points directly to native GPU memory.
     *
     * @return DirectByteBuffer of RGBA pixels, or null if no new frame
     */
    public ByteBuffer getNextFrameDirect() {
        if (!streaming) {
            return null;
        }

        // ZERO COPY! Returns native memory wrapped in ByteBuffer
        ByteBuffer buf = nativeGetNextFrameDirect();
        if (buf != null) {
            recordFrameReceived();
        }
        return buf;
    }

    /**
     * Records a received frame timestamp for real-time FPS throughput calculation.
     */
    private synchronized void recordFrameReceived() {
        long now = System.nanoTime();
        if (fpsStartTimeNanos == 0) {
            fpsStartTimeNanos = now;
            fpsFrameCount = 1;
            return;
        }

        fpsFrameCount++;
        long elapsedNanos = now - fpsStartTimeNanos;
        if (elapsedNanos >= 250_000_000L) { // Update FPS every 250 ms
            currentStreamingFps = (fpsFrameCount * 1_000_000_000.0) / elapsedNanos;
            fpsStartTimeNanos = now;
            fpsFrameCount = 0;
        }
    }

    /**
     * ZERO-COPY FastImage: Wraps the current native frame in a FastImage instance.
     * Allows immediate SIMD-accelerated Anti-Aliasing (resizeAreaAverage), bilinear scaling,
     * or blur without copying pixel data to Java heap.
     *
     * @return FastImage wrapping the native GPU frame, or null if no new frame
     */
    public FastImage getNextFrameImage() {
        ByteBuffer buf = getNextFrameDirect();
        if (buf == null) {
            return null;
        }
        return FastImage.wrap(buf, frameWidth, frameHeight);
    }

    /**
     * Captures a single screenshot directly into a SIMD-accelerated FastImage.
     *
     * @param region Rectangle defining capture bounds
     * @return FastImage containing captured pixels, or null if capture failed
     */
    public FastImage captureImage(Rectangle region) {
        BufferedImage bi = captureScreen(region);
        if (bi == null) {
            return null;
        }
        return FastImage.fromBufferedImage(bi);
    }

    /**
     * Gets current streaming FPS throughput measured from DXGI frame arrivals.
     *
     * @return Measured frames per second, or 0.0 if not streaming
     */
    public double getStreamFPS() {
        if (!streaming) {
            return 0.0;
        }
        return currentStreamingFps;
    }

    /**
     * Gets number of monitors.
     *
     * @return Monitor count
     */
    public int getMonitorCount() {
        return nativeGetMonitorCount();
    }

    /**
     * Excludes or includes a window from screen capture (DXGI Desktop Duplication, BitBlt, etc.).
     * Prevents self-capture feedback loops (Droste effect) when viewing screen streams.
     *
     * @param hwnd    Win32 HWND window handle
     * @param exclude true to make the window invisible to capture, false to capture normally
     * @return true if affinity successfully applied
     */
    public static boolean setWindowExcluded(long hwnd, boolean exclude) {
        return nativeSetWindowExcluded(hwnd, exclude);
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            dispose();
        } finally {
            super.finalize();
        }
    }

    // Native methods
    private native long nativeInit();

    private native long nativeInitRegion(int x, int y, int width, int height);

    private native int[] nativeCaptureScreen(int x, int y, int width, int height);

    private native boolean nativeStartStream(int x, int y, int width, int height);

    private native int[] nativeGetNextFrame();

    private native ByteBuffer nativeGetNextFrameDirect();  // ZERO-COPY!

    private native void nativeStopStream();

    private native boolean nativeSetupHardwareScaling(int outW, int outH, int filter);

    private native int nativeGetPixelColor(int x, int y);

    private native void nativeDispose(long handle);

    private native int nativeGetMonitorCount();

    private native int nativeGetFrameWidth();

    private native int nativeGetFrameHeight();

    private static native boolean nativeSetWindowExcluded(long hwnd, boolean exclude);

    private static native boolean nativeSetWindowExcludedByTitle(String title, boolean exclude);
}
