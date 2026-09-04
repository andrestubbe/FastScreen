package fastscreen;

import fastscreen.FastScreen;
import fasttheme.FastTheme;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.awt.image.BufferStrategy;
import java.awt.image.BufferedImage;
import java.awt.image.DataBufferInt;

/**
 * FastScreen 0.1.1 — Clean Visual Showcase Demo.
 *
 * Demonstrates:
 * - Ultra-high FPS Desktop Duplication (GPU Hardware Scaled)
 * - Native Window Capture Exclusion (WDA_EXCLUDEFROMCAPTURE)
 * - Clean edge-to-edge video canvas (No overlays)
 * - Full telemetry & control status in the native Windows Title Bar
 */
public class Demo extends Canvas {

    // --- Window / Render Target (FastAnimation Standard) ---
    private static final int WIDTH = 1173;
    private static final int HEIGHT = 610;

    private final FastScreen screen;
    private final JFrame parentFrame;
    private long hwnd = 0;

    // Desktop Dimensions
    private final int screenW;
    private final int screenH;

    // 1:1 Scaled Canvas Buffer (Fastest VRAM Blit)
    private BufferedImage canvasImage;
    private int[] canvasPixels;

    // Fallback full-resolution buffer if hardware scaling is inactive
    private BufferedImage fallbackImage;
    private int[] fallbackPixels;
    private boolean hardwareScaled = false;

    // Interactive State
    private volatile boolean isExcluded = true;
    private volatile boolean isPaused = false;
    private volatile boolean running = true;

    // Telemetry
    private volatile double currentFps = 0.0;
    private volatile double avgFrameTimeMs = 0.8;

    public Demo(JFrame parentFrame) {
        this.parentFrame = parentFrame;
        setPreferredSize(new Dimension(WIDTH, HEIGHT));
        setIgnoreRepaint(true);

        // 1. Detect physical desktop resolution
        Dimension screenDim = Toolkit.getDefaultToolkit().getScreenSize();
        this.screenW = screenDim.width;
        this.screenH = screenDim.height;

        // 2. Initialize FastScreen
        this.screen = new FastScreen();

        // 3. Prepare 1:1 Canvas Buffer for Zero-Copy display
        this.canvasImage = new BufferedImage(WIDTH, HEIGHT, BufferedImage.TYPE_INT_RGB);
        this.canvasPixels = ((DataBufferInt) canvasImage.getRaster().getDataBuffer()).getData();

        // 4. Keyboard Controls
        addKeyListener(new KeyAdapter() {
            @Override
            public void keyPressed(KeyEvent e) {
                switch (e.getKeyCode()) {
                    case KeyEvent.VK_E -> toggleExclusion();
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
            if (isPaused) {
                parentFrame.setTitle(String.format(
                    "FastScreen 0.1.1 — PAUSED | Exclusion: %s [E] | [SPACE] Resume | [ESC] Exit",
                    isExcluded ? "ON (Transparent Lens)" : "OFF (Droste Mirror)"
                ));
            } else {
                parentFrame.setTitle(String.format(
                    "FastScreen 0.1.1 — %.1f FPS | %.2f ms | Exclusion: %s [E] | [SPACE] Pause",
                    currentFps, avgFrameTimeMs,
                    isExcluded ? "ON (Transparent Lens)" : "OFF (Droste Mirror)"
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
        createBufferStrategy(3);
        BufferStrategy bs = getBufferStrategy();

        // 1. Retrieve native HWND and apply initial exclusion
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
        if (streamStarted) {
            // Enable GPU Hardware Scaling directly to 1173x610 (Bypasses CPU scaling completely!)
            hardwareScaled = screen.enableHardwareScaling(WIDTH, HEIGHT, false);
            if (hardwareScaled) {
                System.out.println("[FastScreen Demo] GPU Hardware scaling enabled: " + screenW + "x" + screenH + " -> " + WIDTH + "x" + HEIGHT);
            } else {
                System.out.println("[FastScreen Demo] GPU scaling unavailable, using fast 1:1 blit.");
                fallbackImage = new BufferedImage(screenW, screenH, BufferedImage.TYPE_INT_RGB);
                fallbackPixels = ((DataBufferInt) fallbackImage.getRaster().getDataBuffer()).getData();
            }
        } else {
            fallbackImage = new BufferedImage(screenW, screenH, BufferedImage.TYPE_INT_RGB);
            fallbackPixels = ((DataBufferInt) fallbackImage.getRaster().getDataBuffer()).getData();
        }

        updateTitleBar();

        // 3. Ultra-Fast High-FPS Render Loop
        new Thread(() -> {
            long lastFpsTime = System.nanoTime();
            int frameCount = 0;

            while (running) {
                long frameStart = System.nanoTime();

                boolean hasFrame = false;

                if (!isPaused) {
                    int[] newPixels = screen.getNextFrame();
                    if (newPixels != null) {
                        hasFrame = true;
                        if (hardwareScaled && newPixels.length == canvasPixels.length) {
                            // FASTEST ZERO-COPY PATH: GPU already downscaled to 1173x610
                            System.arraycopy(newPixels, 0, canvasPixels, 0, canvasPixels.length);
                        } else if (fallbackPixels != null && newPixels.length == fallbackPixels.length) {
                            System.arraycopy(newPixels, 0, fallbackPixels, 0, fallbackPixels.length);
                        }
                    } else if (!streamStarted) {
                        BufferedImage shot = screen.captureScreen();
                        if (shot != null && fallbackPixels != null) {
                            hasFrame = true;
                            int[] shotPx = ((DataBufferInt) shot.getRaster().getDataBuffer()).getData();
                            System.arraycopy(shotPx, 0, fallbackPixels, 0, Math.min(shotPx.length, fallbackPixels.length));
                        }
                    }
                }

                // Render frame to canvas (pure edge-to-edge video, NO overlays!)
                Graphics g = bs.getDrawGraphics();
                if (hardwareScaled) {
                    g.drawImage(canvasImage, 0, 0, null);
                } else if (fallbackImage != null) {
                    g.drawImage(fallbackImage, 0, 0, WIDTH, HEIGHT, null);
                }
                g.dispose();
                bs.show();

                // Telemetry Calculation
                long frameEnd = System.nanoTime();
                long durationNs = frameEnd - frameStart;
                avgFrameTimeMs = avgFrameTimeMs * 0.95 + (durationNs / 1_000_000.0) * 0.05;

                frameCount++;
                if (frameEnd - lastFpsTime >= 500_000_000L) { // Update title every 0.5s
                    currentFps = (frameCount * 1_000_000_000.0) / (frameEnd - lastFpsTime);
                    frameCount = 0;
                    lastFpsTime = frameEnd;
                    updateTitleBar();
                }

                // Yield to prevent CPU lock if waiting for next frame
                if (!hasFrame) {
                    Thread.yield();
                }
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
        System.setProperty("sun.java2d.opengl", "true");
        System.setProperty("sun.awt.noerasebackground", "true");

        SwingUtilities.invokeLater(() -> {
            JFrame frame = new JFrame("FastScreen 0.1.1 — 240+ FPS Desktop Duplication");
            frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
            frame.setIgnoreRepaint(true);
            frame.setIconImage(createRoundIcon());

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