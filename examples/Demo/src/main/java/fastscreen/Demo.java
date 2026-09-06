package fastscreen;

import fastproportion.Proportion;
import fastproportion.ProportionMode;
import fasttheme.FastTheme;
import fastdwm.FastDWM;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.awt.image.BufferStrategy;
import java.awt.image.BufferedImage;
import java.awt.image.DataBufferInt;
import java.util.concurrent.atomic.AtomicIntegerArray;
import java.util.concurrent.locks.LockSupport;

/**
 * FastScreen 0.1.3 — High-FPS Scalable Desktop Duplication Demo.
 * <p>
 * Key Features:
 * <ul>
 *   <li>DirectX 11 DXGI Desktop Duplication engine with GDI Fallback</li>
 *   <li>Zero-Copy FastPointer memory bridge to FastImage</li>
 *   <li>Multi-threaded SIMD Resampling via FastImage ([A] toggle: Point, Bilinear, Bicubic, Area-Average)</li>
 *   <li>Native Window Exclusion ([E] toggle: WDA_EXCLUDEFROMCAPTURE) to prevent infinite mirror recursion</li>
 *   <li>Edge-to-edge borderless scaling via {@link Proportion} COVER mode</li>
 *   <li>Zero-GC decoupled Lock-Free Triple Buffering between capture and render loops</li>
 *   <li>Dynamic Windows 11 Dark Mica Title Bar via FastTheme</li>
 * </ul>
 */
public class Demo extends Canvas {

    private static final int BASE_HEIGHT = 720;

    private final FastScreen screen;
    private final JFrame parentFrame;
    private long hwnd = 0;

    // Desktop dimensions
    private final int screenW;
    private final int screenH;

    // Target render dimensions for GPU Hardware Scaling
    private final int targetW;
    private final int targetH;

    // Lock-Free Triple Buffer Pool (0=FREE, 1=WRITING, 2=READY, 3=READING)
    private static final int SLOT_FREE = 0;
    private static final int SLOT_WRITING = 1;
    private static final int SLOT_READY = 2;
    private static final int SLOT_READING = 3;
    private final int[][] captureBuffers;
    private final AtomicIntegerArray slotStates = new AtomicIntegerArray(3);

    // Geometry Context
    private final Proportion proportion;
    private final float[] renderBounds = new float[4];

    // Offscreen capture buffer matching target dimensions
    private final BufferedImage captureImage;
    private final int[] capturePixels;

    // Interactive State: [E] Exclude window, [A] Anti-Aliasing mode
    private volatile boolean isExcluded = true;
    private volatile boolean running = true;

    // Resampling Modes (Powered entirely by FastImage via FastPointer):
    // 0 = FASTIMAGE POINT (Nearest-Neighbor, O(1) per pixel)
    // 1 = FASTIMAGE BILINEAR (Bilinear Interpolation)
    // 2 = FASTIMAGE BICUBIC (Catmull-Rom Spline Anti-Aliasing)
    // 3 = FASTIMAGE AREA-AVERAGE (Box Anti-Aliasing Downsampling)
    private static final String[] AA_MODES = {
        "POINT [A]",
        "BILINEAR [A]",
        "BICUBIC [A]",
        "AREA-AVERAGE [A]"
    };
    private volatile int aaModeIndex = 1; // Default: FastImage Bilinear

    // Real-Time Telemetry
    private volatile double renderFps = 0.0;
    private volatile double processFps = 0.0;
    private volatile double avgCaptureTimeMs = 0.8;

    public Demo(JFrame parentFrame) {
        this.parentFrame = parentFrame;

        // 1. Detect physical desktop resolution
        Dimension screenDim = Toolkit.getDefaultToolkit().getScreenSize();
        this.screenW = screenDim.width;
        this.screenH = screenDim.height;

        // 2. Initialize Proportion context
        this.proportion = new Proportion(0, 0, screenW, screenH);

        // 3. Compute initial window aspect size and target render resolution
        this.targetH = BASE_HEIGHT;
        this.targetW = (int) Math.round(BASE_HEIGHT * ((double) screenW / screenH));
        setPreferredSize(new Dimension(targetW, targetH));
        setMinimumSize(new Dimension(320, 180));
        setIgnoreRepaint(true);

        // 4. Allocate triple buffers matching target render dimensions
        int totalPixels = targetW * targetH;
        this.captureBuffers = new int[3][totalPixels];

        this.captureImage = new BufferedImage(targetW, targetH, BufferedImage.TYPE_INT_RGB);
        this.capturePixels = ((DataBufferInt) captureImage.getRaster().getDataBuffer()).getData();

        // 5. Initialize Engine & Input Controls
        this.screen = new FastScreen();

        addKeyListener(new KeyAdapter() {
            @Override
            public void keyPressed(KeyEvent e) {
                switch (e.getKeyCode()) {
                    case KeyEvent.VK_E -> toggleExclusion();
                    case KeyEvent.VK_A -> toggleAAMode();
                    case KeyEvent.VK_ESCAPE -> exitApp();
                }
            }
        });
    }

    private final Object pipelineLock = new Object();

    private void toggleAAMode() {
        synchronized (pipelineLock) {
            aaModeIndex = (aaModeIndex + 1) % AA_MODES.length;
        }
        updateTitleBar();
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
            double captureFps = screen != null ? screen.getStreamFPS() : 0.0;
            String exclStr = isExcluded ? "LENS: HIDDEN [E]" : "LENS: MIRROR [E]";
            String aaStr = AA_MODES[aaModeIndex];

            parentFrame.setTitle(String.format(
                "FastScreen 0.1.3 — Render: %.0f FPS | Filter: %.0f FPS (%.2f ms) | DXGI: %.0f FPS | %dx%d | %s | %s",
                renderFps, processFps, avgCaptureTimeMs, captureFps, curW, curH, aaStr, exclStr
            ));
        });
    }

    private void exitApp() {
        running = false;
        try { FastDWM.endTimerPeriod(1); } catch (Throwable ignored) {}
        if (screen != null) {
            screen.stopStream();
            screen.dispose();
        }
        parentFrame.dispose();
        System.exit(0);
    }

    public void start() {
        createBufferStrategy(2);
        try { FastDWM.beginTimerPeriod(1); } catch (Throwable ignored) {}

        // 1. Exclude window from desktop capture (prevents infinite mirror loop)
        try {
            hwnd = FastTheme.getWindowHandle(parentFrame);
            if (hwnd != 0) {
                FastScreen.excludeWindow(hwnd);
            }
        } catch (Throwable t) {
            System.err.println("[FastScreen Demo] HWND init: " + t.getMessage());
        }

        // 2. Start desktop streaming pipeline (native full-resolution)
        screen.startStream(0, 0, screenW, screenH);
        System.out.println("[FastScreen Demo] Native DXGI Streaming active: " + screenW + "x" + screenH + " (Processing via FastImage: " + targetW + "x" + targetH + ")");

        updateTitleBar();

        // -------------------------------------------------------------
        // -------------------------------------------------------------
        // DXGI CAPTURE WORKER: Dedicated high-speed DXGI acquisition
        // -------------------------------------------------------------
        final java.util.concurrent.atomic.AtomicLong latestFrameAddress = new java.util.concurrent.atomic.AtomicLong(0L);
        final java.util.concurrent.atomic.AtomicLong frameSequence = new java.util.concurrent.atomic.AtomicLong(0L);

        Thread[] processThreadHolder = new Thread[1];

        Thread captureThread = new Thread(() -> {
            while (running) {
                long addr = screen.getNextFrameAddress();
                if (addr != 0L) {
                    latestFrameAddress.set(addr);
                    frameSequence.incrementAndGet();
                    Thread p = processThreadHolder[0];
                    if (p != null) {
                        LockSupport.unpark(p);
                    }
                } else {
                    LockSupport.parkNanos(50_000L); // 0.05ms backoff when no desktop frame changed
                }
            }
        }, "FastScreen-DXGI-Poller");
        captureThread.setDaemon(true);
        captureThread.start();

        // -------------------------------------------------------------
        // IMAGE PROCESS WORKER: Multi-Threaded SIMD Resampling via FastImage
        // -------------------------------------------------------------
        Thread processThread = new Thread(() -> {
            int writeSlot = 0;
            slotStates.set(writeSlot, SLOT_WRITING);
            long lastProcessedSeq = 0L;
            long lastProcTime = System.nanoTime();
            int procCount = 0;

            while (running) {
                long seq = frameSequence.get();
                if (seq == 0L || seq == lastProcessedSeq) {
                    LockSupport.parkNanos(100_000L); // Wait for fresh frame trigger
                    continue;
                }

                lastProcessedSeq = seq;
                long addr = latestFrameAddress.get();
                if (addr == 0L) continue;

                long t0 = System.nanoTime();

                synchronized (pipelineLock) {
                    fastimage.FastImage img = fastimage.FastImage.wrap(addr, screenW, screenH);
                    switch (aaModeIndex) {
                        case 0 -> img.resizeNearest(targetW, targetH);
                        case 1 -> img.resize(targetW, targetH);
                        case 2 -> img.resizeBicubic(targetW, targetH);
                        case 3 -> img.resizeAreaAverage(targetW, targetH);
                    }
                    img.getPixels(captureBuffers[writeSlot]);
                    img.dispose();
                }

                long t1 = System.nanoTime();
                double procMs = (t1 - t0) / 1_000_000.0;
                avgCaptureTimeMs = avgCaptureTimeMs * 0.9 + procMs * 0.1;

                procCount++;
                if (t1 - lastProcTime >= 500_000_000L) {
                    processFps = (procCount * 1_000_000_000.0) / (t1 - lastProcTime);
                    procCount = 0;
                    lastProcTime = t1;
                }

                slotStates.set(writeSlot, SLOT_READY);

                int nextSlot = -1;
                for (int i = 0; i < 3; i++) {
                    if (slotStates.compareAndSet(i, SLOT_FREE, SLOT_WRITING)) {
                        nextSlot = i;
                        break;
                    }
                }
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
                    for (int i = 0; i < 3; i++) {
                        if (slotStates.compareAndSet(i, SLOT_FREE, SLOT_WRITING)) {
                            writeSlot = i;
                            break;
                        }
                    }
                }
            }
        }, "FastScreen-Image-Processor");
        processThreadHolder[0] = processThread;
        processThread.setDaemon(true);
        processThread.start();

        // -------------------------------------------------------------
        // RENDER THREAD: FastDWM Hardware-Locked VSync Render Loop
        // -------------------------------------------------------------
        Thread renderThread = new Thread(() -> {
            long lastFpsTime = System.nanoTime();
            int frameCount = 0;

            while (running) {
                // Synchronize loop precisely to physical monitor VBlank (120 Hz VSync)
                FastDWM.waitForVSync();

                int readySlot = -1;
                for (int i = 0; i < 3; i++) {
                    if (slotStates.compareAndSet(i, SLOT_READY, SLOT_READING)) {
                        readySlot = i;
                        break;
                    }
                }

                if (readySlot != -1) {
                    System.arraycopy(captureBuffers[readySlot], 0, capturePixels, 0, capturePixels.length);
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
                        // Compute edge-to-edge COVER bounds
                        proportion.width = cw;
                        proportion.height = ch;
                        proportion.compute(ProportionMode.COVER, renderBounds);

                        int drawX = Math.round(renderBounds[0]);
                        int drawY = Math.round(renderBounds[1]);
                        int drawW = Math.round(renderBounds[2]);
                        int drawH = Math.round(renderBounds[3]);

                        Graphics g = bs.getDrawGraphics();

                        if (drawW > 0 && drawH > 0) {
                            // Direct 0-CPU-cost Blit of scaled frame
                            g.drawImage(captureImage, drawX, drawY, drawW, drawH, null);
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
            }
        }, "FastScreen-Render-Loop");
        renderThread.setDaemon(true);
        renderThread.start();
    }

    private static BufferedImage createRoundIcon() {
        BufferedImage icon = new BufferedImage(64, 64, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g = icon.createGraphics();
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
        g.setColor(Color.WHITE);
        g.fillOval(4, 4, 56, 56);
        g.dispose();
        return icon;
    }

    public static void main(String[] args) {
        System.setProperty("sun.awt.noerasebackground", "true");

        SwingUtilities.invokeLater(() -> {
            JFrame frame = new JFrame("FastScreen 0.1.3");
            frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
            frame.setIgnoreRepaint(true);
            frame.setIconImage(createRoundIcon());
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
