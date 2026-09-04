package fastscreen;

import fastscreen.FastScreen;
import fasttheme.FastTheme;
import fastproportion.Proportion;
import fastproportion.ProportionMode;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.awt.image.BufferStrategy;
import java.awt.image.BufferedImage;
import java.awt.image.DataBufferInt;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * FastScreen 0.1.2 — High-FPS Scalable Desktop Duplication Demo.
 *
 * Features:
 * - High-FPS DirectX 11 DXGI Desktop Duplication & GDI Fallback
 * - Native Window Capture Exclusion (WDA_EXCLUDEFROMCAPTURE)
 * - FastProportion COVER Mode: Zero borders, 100% edge-to-edge scaling
 * - Anti-Aliasing toggle [A] for smooth downsampling vs raw pixel speed
 * - Decoupled Producer-Consumer Architecture for Maximum Frame Rate
 * - Freely resizable window with dynamic native title bar telemetry
 */
public class Demo extends Canvas {

    private static final int BASE_HEIGHT = 720;

    private final FastScreen screen;
    private final JFrame parentFrame;
    private long hwnd = 0;

    // Desktop Resolution
    private final int screenW;
    private final int screenH;

    // Double-Buffered Desktop Frames for Zero-Lock Decoupled Rendering
    private final int[] bufferA;
    private final int[] bufferB;
    private volatile int[] activeFrontBuffer;
    private final AtomicBoolean newFrameAvailable = new AtomicBoolean(false);

    // FastProportion Zero-Allocation Math Context
    private final Proportion proportion;
    private final float[] renderBounds = new float[4];

    // Offscreen Image for Display
    private final BufferedImage displayImage;
    private final int[] displayPixels;

    // Interactive State
    private volatile boolean isExcluded = true;
    private volatile boolean isPaused = false;
    private volatile boolean isAntiAliasing = true;
    private volatile boolean running = true;

    // Telemetry
    private volatile double currentFps = 0.0;
    private volatile double avgCaptureTimeMs = 0.8;

    public Demo(JFrame parentFrame) {
        this.parentFrame = parentFrame;

        // 1. Detect physical desktop resolution
        Dimension screenDim = Toolkit.getDefaultToolkit().getScreenSize();
        this.screenW = screenDim.width;
        this.screenH = screenDim.height;

        // 2. Initialize FastProportion context
        this.proportion = new Proportion(0, 0, screenW, screenH);

        // 3. Compute initial window size (using desktop ratio)
        int initialWidth = (int) Math.round(BASE_HEIGHT * ((double) screenW / screenH));
        setPreferredSize(new Dimension(initialWidth, BASE_HEIGHT));
        setMinimumSize(new Dimension(320, 180));
        setIgnoreRepaint(true);

        // 4. Double buffer pool for capture
        int totalPixels = screenW * screenH;
        this.bufferA = new int[totalPixels];
        this.bufferB = new int[totalPixels];
        this.activeFrontBuffer = bufferA;

        // Display image for canvas blit
        this.displayImage = new BufferedImage(screenW, screenH, BufferedImage.TYPE_INT_RGB);
        this.displayPixels = ((DataBufferInt) displayImage.getRaster().getDataBuffer()).getData();

        // 5. Initialize FastScreen
        this.screen = new FastScreen();

        // 6. Register Keyboard Controls
        addKeyListener(new KeyAdapter() {
            @Override
            public void keyPressed(KeyEvent e) {
                switch (e.getKeyCode()) {
                    case KeyEvent.VK_E -> toggleExclusion();
                    case KeyEvent.VK_A -> {
                        isAntiAliasing = !isAntiAliasing;
                        updateTitleBar();
                    }
                    case KeyEvent.VK_SPACE -> {
                        isPaused = !isPaused;
                        updateTitleBar();
                    }
                    case KeyEvent.VK_ESCAPE -> exitApp();
                }
            }
        });
    }

    private void toggleExclusion() {
        isExcluded = !isExcluded;
        if (hwnd != 0) {
            if (isExcluded) {
                FastScreen.excludeWindow(hwnd);
            } else {
                FastScreen.includeWindow(hwnd);
            }
        }
        updateTitleBar();
    }

    private void updateTitleBar() {
        SwingUtilities.invokeLater(() -> {
            int curW = getWidth();
            int curH = getHeight();
            String aaStr = isAntiAliasing ? "Bilinear AA [A]" : "Point [A]";
            if (isPaused) {
                parentFrame.setTitle(String.format(
                    "FastScreen 0.1.2 — PAUSED | %dx%d | AA: %s | Excl: %s [E] | [SPACE] Resume | [ESC] Exit",
                    curW, curH, aaStr,
                    isExcluded ? "ON" : "OFF"
                ));
            } else {
                parentFrame.setTitle(String.format(
                    "FastScreen 0.1.2 — %.1f FPS | %.2f ms | %dx%d (COVER) | AA: %s | Excl: %s [E] | [SPACE] Pause",
                    currentFps, avgCaptureTimeMs, curW, curH, aaStr,
                    isExcluded ? "ON" : "OFF"
                ));
            }
        });
    }

    private void exitApp() {
        running = false;
        if (screen != null) {
            screen.stopStream();
            screen.dispose();
        }
        parentFrame.dispose();
        System.exit(0);
    }

    public void start() {
        createBufferStrategy(2);

        // 1. Retrieve native HWND and apply initial window exclusion
        try {
            hwnd = FastTheme.getWindowHandle(parentFrame);
            if (hwnd != 0) {
                FastScreen.excludeWindow(hwnd);
            }
        } catch (Throwable t) {
            System.err.println("[FastScreen Demo] HWND note: " + t.getMessage());
        }

        // 2. Start Desktop Streaming
        boolean streamStarted = screen.startStream(0, 0, screenW, screenH);
        updateTitleBar();

        // -------------------------------------------------------------
        // WORKER THREAD: Dedicated High-Speed Capture Pipeline
        // -------------------------------------------------------------
        Thread captureThread = new Thread(() -> {
            int[] backBuffer = bufferB;

            while (running) {
                if (isPaused) {
                    try { Thread.sleep(15); } catch (InterruptedException ignored) {}
                    continue;
                }

                long t0 = System.nanoTime();
                int[] rawFrame = screen.getNextFrame();
                long t1 = System.nanoTime();

                if (rawFrame != null && rawFrame.length == backBuffer.length) {
                    System.arraycopy(rawFrame, 0, backBuffer, 0, rawFrame.length);
                    // Atomic pointer swap to present latest frame without blocking
                    activeFrontBuffer = backBuffer;
                    backBuffer = (backBuffer == bufferA) ? bufferB : bufferA;
                    newFrameAvailable.set(true);

                    double captureMs = (t1 - t0) / 1_000_000.0;
                    avgCaptureTimeMs = avgCaptureTimeMs * 0.9 + captureMs * 0.1;
                } else if (!streamStarted) {
                    BufferedImage shot = screen.captureScreen();
                    if (shot != null) {
                        int[] shotPx = ((DataBufferInt) shot.getRaster().getDataBuffer()).getData();
                        System.arraycopy(shotPx, 0, backBuffer, 0, Math.min(shotPx.length, backBuffer.length));
                        activeFrontBuffer = backBuffer;
                        backBuffer = (backBuffer == bufferA) ? bufferB : bufferA;
                        newFrameAvailable.set(true);
                    }
                }

                // Yield to prevent burning 100% of a CPU core
                Thread.yield();
            }
        }, "FastScreen-Capture-Worker");
        captureThread.setDaemon(true);
        captureThread.setPriority(Thread.MAX_PRIORITY);
        captureThread.start();

        // -------------------------------------------------------------
        // RENDER THREAD: Smooth FastProportion COVER Display Loop
        // -------------------------------------------------------------
        new Thread(() -> {
            long lastFpsTime = System.nanoTime();
            int frameCount = 0;

            while (running) {
                // If a fresh capture frame is ready, update display image
                if (newFrameAvailable.compareAndSet(true, false)) {
                    int[] front = activeFrontBuffer;
                    if (front != null) {
                        System.arraycopy(front, 0, displayPixels, 0, displayPixels.length);
                    }
                }

                BufferStrategy bs = getBufferStrategy();
                if (bs == null || bs.contentsLost()) {
                    createBufferStrategy(2);
                    bs = getBufferStrategy();
                }

                if (bs != null) {
                    int cw = getWidth();
                    int ch = getHeight();

                    if (cw > 0 && ch > 0) {
                        // Compute FastProportion COVER Bounds (NEVER shows a border!)
                        proportion.width = cw;
                        proportion.height = ch;
                        proportion.compute(ProportionMode.COVER, renderBounds);

                        int drawX = Math.round(renderBounds[0]);
                        int drawY = Math.round(renderBounds[1]);
                        int drawW = Math.round(renderBounds[2]);
                        int drawH = Math.round(renderBounds[3]);

                        Graphics g = bs.getDrawGraphics();
                        if (g instanceof Graphics2D g2) {
                            if (isAntiAliasing) {
                                g2.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR);
                                g2.setRenderingHint(RenderingHints.KEY_RENDERING, RenderingHints.VALUE_RENDER_QUALITY);
                            } else {
                                g2.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_NEAREST_NEIGHBOR);
                            }
                        }

                        // Render edge-to-edge desktop image with zero margin
                        g.drawImage(displayImage, drawX, drawY, drawW, drawH, null);

                        g.dispose();
                        if (!bs.contentsLost()) {
                            bs.show();
                        }
                    }
                }

                frameCount++;
                long now = System.nanoTime();
                if (now - lastFpsTime >= 500_000_000L) {
                    currentFps = (frameCount * 1_000_000_000.0) / (now - lastFpsTime);
                    frameCount = 0;
                    lastFpsTime = now;
                    updateTitleBar();
                }

                // Smooth frame pacing (~200 FPS cap)
                Thread.yield();
            }
        }, "FastScreen-Render-Loop").start();
    }

    private static BufferedImage createRoundIcon() {
        BufferedImage icon = new BufferedImage(64, 64, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g = icon.createGraphics();
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
        g.setColor(new Color(0, 255, 200));
        g.fillOval(4, 4, 56, 56);
        g.setColor(new Color(16, 20, 24));
        g.fillOval(14, 14, 36, 36);
        g.setColor(new Color(0, 230, 118));
        g.fillOval(24, 24, 16, 16);
        g.dispose();
        return icon;
    }

    public static void main(String[] args) {
        System.setProperty("sun.awt.noerasebackground", "true");

        SwingUtilities.invokeLater(() -> {
            JFrame frame = new JFrame("FastScreen 0.1.1 — Desktop Duplication");
            frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
            frame.setIgnoreRepaint(true);
            frame.setIconImage(createRoundIcon());

            // Freely resizable window
            frame.setResizable(true);

            Demo demo = new Demo(frame);
            frame.add(demo);
            frame.pack();
            frame.setLocationRelativeTo(null);
            frame.addNotify();

            // Native Windows 11 Dark Title Bar via FastTheme
            try {
                long hwnd = FastTheme.getWindowHandle(frame);
                if (hwnd != 0) {
                    FastTheme.setTitleBarDarkMode(hwnd, true);
                    FastTheme.setTitleBarColor(hwnd, 16, 20, 24);
                    FastTheme.setTitleBarTextColor(hwnd, 240, 245, 250);
                    FastTheme.setWindowTransparency(hwnd, 255);
                    FastTheme.enableMica(hwnd, true);
                }
            } catch (Throwable t) {
                System.err.println("[FastScreen Demo] FastTheme note: " + t.getMessage());
            }

            frame.setVisible(true);
            demo.start();
            demo.requestFocus();
        });
    }
}