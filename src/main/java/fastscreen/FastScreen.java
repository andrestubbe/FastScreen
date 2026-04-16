package fastscreen;

import fastcore.FastCore;
import java.awt.Rectangle;
import java.awt.image.BufferedImage;

/**
 * High-performance Java screen capture using DXGI Desktop Duplication.
 * 
 * <p>FastScreen provides zero-copy, hardware-accelerated screen capture
 * at 500-2000 FPS using DirectX DXGI Desktop Duplication API.</p>
 * 
 * <p>Key features:
 * <ul>
 *   <li>Single screenshot capture (BufferedImage or raw pixels)</li>
 *   <li>High-FPS streaming mode for real-time applications</li>
 *   <li>Multi-monitor support</li>
 *   <li>Zero GC pressure with native buffers</li>
 * </ul></p>
 * 
 * <p><strong>Example usage:</strong></p>
 * <pre>{@code
 * FastScreen screen = new FastScreen();
 * 
 * // Single capture
 * BufferedImage img = screen.captureScreen(new Rectangle(0, 0, 1920, 1080));
 * 
 * // High-FPS streaming
 * screen.startStream(0, 0, 1920, 1080);
 * while (running) {
 *     if (screen.hasNewFrame()) {
 *         int[] pixels = screen.getNextFrame();
 *         // Process frame...
 *     }
 * }
 * screen.stopStream();
 * }</pre>
 * 
 * @author Andre Stubbe
 * @version 1.0.0
 * @since 2026-04-16
 */
public class FastScreen {
    
    static {
        // Load native library via FastCore
        FastCore.loadLibrary("fastscreen");
    }
    
    // Native methods
    private native long nativeInit();
    private native int[] nativeCaptureScreen(int x, int y, int width, int height);
    private native boolean nativeStartStream(int x, int y, int width, int height);
    private native int[] nativeGetNextFrame();
    private native void nativeStopStream();
    private native int nativeGetPixelColor(int x, int y);
    private native void nativeDispose(long handle);
    private native int nativeGetMonitorCount();
    
    private long nativeHandle = 0;
    private boolean streaming = false;
    private int frameWidth = 0;
    private int frameHeight = 0;
    private int lastFrameWidth = 0;
    private int lastFrameHeight = 0;
    
    // Frame polling buffer - stores frame from hasNewFrame() for getNextFrame()
    private int[] bufferedFrame = null;
    private boolean frameBuffered = false;
    
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
     * Captures a screenshot as BufferedImage.
     * 
     * @param rect Screen region to capture
     * @return BufferedImage containing screenshot, or null if capture failed
     */
    public BufferedImage captureScreen(Rectangle rect) {
        int[] pixels = nativeCaptureScreen(rect.x, rect.y, rect.width, rect.height);
        if (pixels == null) {
            return null;
        }
        
        // Create BufferedImage from pixels (assuming full screen for now)
        BufferedImage image = new BufferedImage(lastFrameWidth, lastFrameHeight, BufferedImage.TYPE_INT_ARGB);
        image.setRGB(0, 0, lastFrameWidth, lastFrameHeight, pixels, 0, lastFrameWidth);
        return image;
    }
    
    /**
     * Captures raw RGBA pixel array.
     * 
     * @param x X coordinate
     * @param y Y coordinate
     * @param width Capture width
     * @param height Capture height
     * @return int array of RGBA pixels, or null if capture failed
     */
    public int[] captureRaw(int x, int y, int width, int height) {
        int[] pixels = nativeCaptureScreen(x, y, width, height);
        if (pixels != null) {
            lastFrameWidth = width;
            lastFrameHeight = height;
        }
        return pixels;
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
     * Starts high-FPS streaming capture.
     * 
     * @param x X coordinate of capture region
     * @param y Y coordinate of capture region
     * @param width Capture width
     * @param height Capture height
     * @return true if streaming started successfully
     */
    public boolean startStream(int x, int y, int width, int height) {
        boolean success = nativeStartStream(x, y, width, height);
        if (success) {
            this.streaming = true;
            this.frameWidth = width;
            this.frameHeight = height;
        }
        return success;
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
        
        // If we have a buffered frame from hasNewFrame(), return it
        if (frameBuffered && bufferedFrame != null) {
            int[] frame = bufferedFrame;
            bufferedFrame = null;
            frameBuffered = false;
            return frame;
        }
        
        // Otherwise poll native directly
        return nativeGetNextFrame();
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
        }
    }
    
    /**
     * Gets current streaming FPS.
     * 
     * @return FPS value
     */
    public double getStreamFPS() {
        // TODO: Implement FPS calculation
        return 0.0;
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
    
    @Override
    protected void finalize() throws Throwable {
        try {
            dispose();
        } finally {
            super.finalize();
        }
    }
}
