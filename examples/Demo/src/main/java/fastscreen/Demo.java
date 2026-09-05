package fastscreen;

import fastscreen.FastScreen;
import fasttheme.FastTheme;
import fastproportion.Proportion;
import fastproportion.ProportionMode;

import fastimage.FastImage;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.awt.image.BufferStrategy;
import java.awt.image.BufferedImage;
import java.awt.image.DataBufferInt;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * FastScreen 0.1.3 — High-FPS Scalable Desktop Duplication Demo.
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

    // Decoupled Lock-Free Triple Buffer Pool (Slot States: 0=FREE, 1=WRITING, 2=READY, 3=READING)
    private static final int SLOT_FREE = 0;
    private static final int SLOT_WRITING = 1;
    private static final int SLOT_READY = 2;
    private static final int SLOT_READING = 3;
    private final int[][] displayBuffers;
    private final java.util.concurrent.atomic.AtomicIntegerArray slotStates = new java.util.concurrent.atomic.AtomicIntegerArray(3);

    // FastProportion Zero-Allocation Math Context
    private final Proportion proportion;
    private final float[] renderBounds = new float[4];

    // Offscreen Image for Display
    private final BufferedImage displayImage;
    private final int[] displayPixels;

    // Interactive State
    private volatile boolean isExcluded = true;
    private volatile boolean isPaused = false;
    private volatile boolean running = true;

    // AA Mode Cycle: 0 = RAW (Point / Max FPS), 1 = BILINEAR AA, 2 = BICUBIC AA (Catmull-Rom)
    private static final String[] AA_MODES = {"RAW [A]", "BILINEAR AA [A]", "BICUBIC AA [A]"};
    private volatile int aaModeIndex = 0; // Default RAW for blazing 800+ FPS

    // Optional FPS Limiter: 0 = Uncapped (Max FPS), 144, 60, 30
    private static final int[] FPS_LIMITS = {0, 144, 60, 30};
    private volatile int fpsLimitIndex = 0; // default uncapped

    // Telemetry
    private volatile double renderFps = 0.0;
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

        // 4. Triple buffer pool for capture
        int totalPixels = screenW * screenH;
        this.displayBuffers = new int[3][totalPixels];

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
                        aaModeIndex = (aaModeIndex + 1) % AA_MODES.length;
                        updateTitleBar();
                    }
                    case KeyEvent.VK_L -> cycleFpsLimit();
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

    private void cycleFpsLimit() {
        fpsLimitIndex = (fpsLimitIndex + 1) % FPS_LIMITS.length;
        updateTitleBar();
    }

    private void updateTitleBar() {
        SwingUtilities.invokeLater(() -> {
            int curW = getWidth();
            int curH = getHeight();
            int limit = FPS_LIMITS[fpsLimitIndex];
            String limitStr = (limit == 0) ? "UNCAPPED [L]" : limit + " FPS [L]";
            double captureFps = screen != null ? screen.getStreamFPS() : 0.0;
            String exclStr = isExcluded ? "LENS: HIDDEN [E]" : "LENS: MIRROR [E]";
            String aaStr = AA_MODES[aaModeIndex];

            if (isPaused) {
                parentFrame.setTitle(String.format(
                    "FastScreen 0.1.3 — PAUSED | %dx%d | %s | %s | %s | [SPACE] Resume | [ESC] Exit",
                    curW, curH, aaStr, limitStr, exclStr
                ));
            } else {
                parentFrame.setTitle(String.format(
                    "FastScreen 0.1.3 — Render: %.0f FPS | DXGI: %.0f FPS (%.2f ms) | %dx%d | %s | %s | %s | [SPACE] Pause",
                    renderFps, captureFps, avgCaptureTimeMs, curW, curH, aaStr, limitStr, exclStr
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
        // WORKER THREAD: Dedicated Zero-GC Capture Pipeline
        // -------------------------------------------------------------
        Thread captureThread = new Thread(() -> {
            int writeSlot = 0;
            slotStates.set(writeSlot, SLOT_WRITING);

            while (running) {
                if (isPaused) {
                    try { Thread.sleep(15); } catch (InterruptedException ignored) {}
                    continue;
                }

                long t0 = System.nanoTime();
                // Zero-GC: Capture directly into pre-allocated write buffer
                boolean gotFrame = screen.getNextFrame(displayBuffers[writeSlot]);
                long t1 = System.nanoTime();

                if (gotFrame) {
                    double captureMs = (t1 - t0) / 1_000_000.0;
                    avgCaptureTimeMs = avgCaptureTimeMs * 0.9 + captureMs * 0.1;

                    // Publish the written slot: state becomes READY
                    slotStates.set(writeSlot, SLOT_READY);

                    // Find next free or stale ready slot for writing
                    int nextSlot = -1;
                    for (int i = 0; i < 3; i++) {
                        if (slotStates.compareAndSet(i, SLOT_FREE, SLOT_WRITING)) {
                            nextSlot = i;
                            break;
                        }
                    }
                    // If all other slots are busy (e.g. 1 READING, 1 READY), overwrite older READY
                    if (nextSlot == -1) {
                        for (int i = 0; i < 3; i++) {
                            if (i != writeSlot && slotStates.compareAndSet(i, SLOT_READY, SLOT_WRITING)) {
                                nextSlot = i;
                                break;
                            }
                        }
                    }
                    if (nextSlot != -1) {
                        writeSlot = nextSlot;
                    } else {
                        // Reader is actively consuming; wait for next free slot via CAS
                        while (running) {
                            for (int i = 0; i < 3; i++) {
                                if (slotStates.compareAndSet(i, SLOT_FREE, SLOT_WRITING)) {
                                    writeSlot = i;
                                    break;
                                }
                            }
                            if (slotStates.get(writeSlot) == SLOT_WRITING) break;
                            java.util.concurrent.locks.LockSupport.parkNanos(100_000L);
                        }
                    }
                } else {
                    // Adaptively park 500µs to prevent 100% CPU core spinning when no frame changed
                    java.util.concurrent.locks.LockSupport.parkNanos(500_000L);
                }
            }
        }, "FastScreen-Capture-Worker");
        captureThread.setDaemon(true);
        captureThread.start();

        // -------------------------------------------------------------
        // RENDER THREAD: Smooth FastProportion COVER Display Loop
        // -------------------------------------------------------------
        Thread renderThread = new Thread(() -> {
            long lastFpsTime = System.nanoTime();
            int frameCount = 0;

            while (running) {
                // Find latest READY slot and claim it with CAS (READY -> READING)
                int readySlot = -1;
                for (int i = 0; i < 3; i++) {
                    if (slotStates.compareAndSet(i, SLOT_READY, SLOT_READING)) {
                        readySlot = i;
                        break;
                    }
                }

                if (readySlot != -1) {
                    System.arraycopy(displayBuffers[readySlot], 0, displayPixels, 0, displayPixels.length);
                    // Finished reading: mark slot as FREE for writer
                    slotStates.set(readySlot, SLOT_FREE);
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
                        if (aaModeIndex == 1) {
                            // BILINEAR AA
                            if (g instanceof Graphics2D g2) {
                                g2.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR);
                            }
                            g.drawImage(displayImage, drawX, drawY, drawW, drawH, null);
                        } else if (aaModeIndex == 2 && drawW > 0 && drawH > 0) {
                            // BICUBIC AA via FastImage SIMD Spline Engine
                            try {
                                FastImage fi = FastImage.fromBufferedImage(displayImage);
                                fi.resizeBicubic(drawW, drawH);
                                BufferedImage bicubicImg = fi.toBufferedImage();
                                fi.dispose();
                                g.drawImage(bicubicImg, drawX, drawY, drawW, drawH, null);
                            } catch (Throwable t) {
                                g.drawImage(displayImage, drawX, drawY, drawW, drawH, null);
                            }
                        } else {
                            // RAW POINT: Maximum unthrottled throughput
                            if (g instanceof Graphics2D g2) {
                                g2.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_NEAREST_NEIGHBOR);
                            }
                            g.drawImage(displayImage, drawX, drawY, drawW, drawH, null);
                        }

                        g.dispose();
                        if (!bs.contentsLost()) {
                            bs.show();
                        }
                    }
                }

                frameCount++;
                long now = System.nanoTime();
                if (now - lastFpsTime >= 500_000_000L) {
                    renderFps = (frameCount * 1_000_000_000.0) / (now - lastFpsTime);
                    frameCount = 0;
                    lastFpsTime = now;
                    updateTitleBar();
                }

                // FPS Pacing: If capped via [L], sleep target duration; otherwise Thread.yield() for maximum unthrottled throughput
                int limit = FPS_LIMITS[fpsLimitIndex];
                if (limit > 0) {
                    long targetFrameNanos = 1_000_000_000L / limit;
                    long elapsedNanos = System.nanoTime() - now;
                    long sleepNanos = targetFrameNanos - elapsedNanos;
                    if (sleepNanos > 0) {
                        java.util.concurrent.locks.LockSupport.parkNanos(sleepNanos);
                    }
                } else {
                    // Maximum unthrottled throughput
                    Thread.yield();
                }
            }
        }, "FastScreen-Render-Loop");
        renderThread.setDaemon(true);
        renderThread.start();
    }

    private static BufferedImage createRoundIcon() {
        BufferedImage icon = new BufferedImage(64, 64, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g = icon.createGraphics();
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
        // Clean, borderless filled circle in title bar gray (16, 20, 24)
        g.setColor(new Color(16, 20, 24));
        g.fillOval(6, 6, 52, 52);
        g.dispose();
        return icon;
    }

    public static void main(String[] args) {
        System.setProperty("sun.awt.noerasebackground", "true");

        SwingUtilities.invokeLater(() -> {
            JFrame frame = new JFrame("FastScreen 0.1.3 — Desktop Duplication");
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