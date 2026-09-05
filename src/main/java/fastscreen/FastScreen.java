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
 *   <li>Zero GC pressure with native DirectByteBuffer buffers and 64-byte aligned frame pooling</li>
 *   <li>Hardware GPU scaling (Point and Bilinear AA via embedded HLSL shaders)</li>
 *   <li>Native Window Capture Exclusion (Win32 display affinity)</li>
 *   <li>Multi-monitor enumeration and capture support with resilient GDI fallback</li>
 *   <li>Safe lifecycle management via {@link AutoCloseable} and instance handles</li>
 * </ul></p>
 *
 * <p><strong>Example usage:</strong></p>
 * <pre>{@code
 * try (FastScreen screen = new FastScreen()) {
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
 * }
 * }</pre>
 *
 * @author Andre Stubbe
 * @version 0.1.3
 * @since 2026-04-16
 */
public class FastScreen implements AutoCloseable {

    static {
        // Load native library via FastCore
        FastCore.loadLibrary("fastscreen");
    }

    private long nativeHandle = 0;
    private final int monitorIndex;
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
     * Creates a new FastScreen instance for the primary monitor (index 0).
     */
    public FastScreen() {
        this(0);
    }

    /**
     * Creates a new FastScreen instance for a specific monitor index.
     *
     * @param monitorIndex 0-based monitor index
     */
    public FastScreen(int monitorIndex) {
        this.monitorIndex = monitorIndex;
        this.nativeHandle = nativeInit(monitorIndex);
        if (this.nativeHandle == 0) {
            throw new RuntimeException("Failed to initialize FastScreen native library for monitor " + monitorIndex);
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
        return wrapPixelsToBufferedImage(pixels, w, h);
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
        return wrapPixelsToBufferedImage(pixels, w, h);
    }

    private static BufferedImage wrapPixelsToBufferedImage(int[] pixels, int w, int h) {
        java.awt.image.DataBufferInt buffer = new java.awt.image.DataBufferInt(pixels, pixels.length);
        int[] masks = {0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000};
        java.awt.image.SinglePixelPackedSampleModel sm = 
            new java.awt.image.SinglePixelPackedSampleModel(java.awt.image.DataBuffer.TYPE_INT, w, h, masks);
        java.awt.image.WritableRaster raster = java.awt.image.Raster.createWritableRaster(sm, buffer, null);
        java.awt.image.ColorModel cm = java.awt.image.ColorModel.getRGBdefault();
        return new BufferedImage(cm, raster, false, null);
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
        if (nativeHandle == 0) {
            return null;
        }

        // Seamless dynamic region update without tearing down the D3D11 device
        if (width != captureWidth || height != captureHeight ||
                x != captureX || y != captureY) {
            boolean updated = nativeSetRegion(nativeHandle, x, y, width, height);
            if (updated) {
                captureX = x;
                captureY = y;
                captureWidth = width;
                captureHeight = height;
            }
        }

        int[] pixels = nativeCaptureScreen(nativeHandle, x, y, width, height);
        if (pixels != null) {
            int w = nativeGetFrameWidth(nativeHandle);
            int h = nativeGetFrameHeight(nativeHandle);
            lastFrameWidth = (w > 0) ? w : width;
            lastFrameHeight = (h > 0) ? h : height;
        }
        return pixels;
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
     * Captures entire monitor by index.
     *
     * @param monitorIndex Monitor index (0-based)
     * @return BufferedImage of monitor
     */
    public static BufferedImage captureMonitor(int monitorIndex) {
        try (FastScreen screen = new FastScreen(monitorIndex)) {
            return screen.captureScreen();
        }
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
        if (nativeHandle == 0) {
            return false;
        }
        boolean success = nativeStartStream(nativeHandle, x, y, width, height);
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
        if (streaming && nativeHandle != 0) {
            nativeStopStream(nativeHandle);
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
        if (!streaming || nativeHandle == 0) {
            throw new IllegalStateException("Must call startStream() before enableHardwareScaling()");
        }
        int filter = useLinearFilter ? 1 : 0;
        boolean success = nativeSetupHardwareScaling(nativeHandle, outputWidth, outputHeight, filter);
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
     * Releases native resources. Implements {@link AutoCloseable#close()}.
     */
    @Override
    public void close() {
        dispose();
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
        if (!streaming || nativeHandle == 0) {
            return false;
        }

        // If we already have a buffered frame, return true
        if (frameBuffered && bufferedFrame != null) {
            return true;
        }

        // Try to get next frame from native
        bufferedFrame = nativeGetNextFrame(nativeHandle);
        frameBuffered = (bufferedFrame != null);
        return frameBuffered;
    }

    /**
     * Non-allocating frame arrival check (0 GC allocations).
     *
     * @return true if a new frame is ready in the native pipeline
     */
    public boolean pollNewFrame() {
        if (!streaming || nativeHandle == 0) {
            return false;
        }
        return nativePollNewFrame(nativeHandle);
    }

    /**
     * ZERO-GC Heap Streaming: Copies the next frame into a user-provided int[] buffer.
     *
     * @param destinationBuffer pre-allocated destination array (must be >= frameWidth * frameHeight)
     * @return true if frame was copied, false if no new frame available
     */
    public boolean getNextFrame(int[] destinationBuffer) {
        if (!streaming || nativeHandle == 0 || destinationBuffer == null) {
            return false;
        }
        boolean ok = nativeGetNextFrameInto(nativeHandle, destinationBuffer);
        if (ok) {
            recordFrameReceived();
        }
        return ok;
    }

    /**
     * Gets color of a single pixel.
     *
     * @param x X coordinate
     * @param y Y coordinate
     * @return RGBA color value
     */
    public int getPixelColor(int x, int y) {
        if (nativeHandle == 0) return 0;
        return nativeGetPixelColor(nativeHandle, x, y);
    }

    /**
     * Gets the next frame in streaming mode.
     * If hasNewFrame() was called before, returns the buffered frame.
     * Otherwise polls native side directly.
     *
     * @return int array of RGBA pixels, or null if no new frame
     */
    public int[] getNextFrame() {
        if (!streaming || nativeHandle == 0) {
            return null;
        }

        int[] frame;
        if (frameBuffered && bufferedFrame != null) {
            frame = bufferedFrame;
            bufferedFrame = null;
            frameBuffered = false;
        } else {
            frame = nativeGetNextFrame(nativeHandle);
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
        if (!streaming || nativeHandle == 0) {
            return null;
        }

        ByteBuffer buf = nativeGetNextFrameDirect(nativeHandle);
        if (buf != null) {
            recordFrameReceived();
        }
        return buf;
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
    public static int getMonitorCount() {
        return nativeGetMonitorCount();
    }

    /**
     * Gets current frame width.
     *
     * @return frame width in pixels
     */
    public int getFrameWidth() {
        if (nativeHandle == 0) return 0;
        return nativeGetFrameWidth(nativeHandle);
    }

    /**
     * Gets current frame height.
     *
     * @return frame height in pixels
     */
    public int getFrameHeight() {
        if (nativeHandle == 0) return 0;
        return nativeGetFrameHeight(nativeHandle);
    }

    /**
     * Gets the monitor index associated with this capture instance.
     *
     * @return 0-based monitor index
     */
    public int getMonitorIndex() {
        return monitorIndex;
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

    // Native methods
    private static native long nativeInit(int monitorIndex);

    private static native long nativeInitRegion(int monitorIndex, int x, int y, int width, int height);

    private static native boolean nativeSetRegion(long handle, int x, int y, int width, int height);

    private static native int[] nativeCaptureScreen(long handle, int x, int y, int width, int height);

    private static native boolean nativeStartStream(long handle, int x, int y, int width, int height);

    private static native boolean nativePollNewFrame(long handle);

    private static native boolean nativeGetNextFrameInto(long handle, int[] destArray);

    private static native int[] nativeGetNextFrame(long handle);

    private static native ByteBuffer nativeGetNextFrameDirect(long handle);

    private static native void nativeStopStream(long handle);

    private static native boolean nativeSetupHardwareScaling(long handle, int outW, int outH, int filter);

    private static native int nativeGetPixelColor(long handle, int x, int y);

    private static native void nativeDispose(long handle);

    private static native int nativeGetMonitorCount();

    private static native int nativeGetFrameWidth(long handle);

    private static native int nativeGetFrameHeight(long handle);

    private static native boolean nativeSetWindowExcluded(long hwnd, boolean exclude);

    private static native boolean nativeSetWindowExcludedByTitle(String title, boolean exclude);
}

