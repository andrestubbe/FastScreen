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
 * FastScreen 0.1.1 — High-FPS Scalable Desktop Duplication Demo.
 *
 * Demonstrates:
 * - Ultra-high FPS Desktop Capture via DirectX 11 DXGI Duplication
 * - Native Window Capture Exclusion (WDA_EXCLUDEFROMCAPTURE)
 * - Automatic Desktop Aspect-Ratio matching (Width adapts to Screen Ratio)
 * - Freely Resizable Window with Aspect-Ratio Preserving Scaling
 * - Clean edge-to-edge canvas with all telemetry in Native Title Bar
 */
public class Demo extends Canvas {

    private static final int BASE_HEIGHT = 610;

    private final FastScreen screen;
    private final JFrame parentFrame;
    private long hwnd = 0;

    // Desktop Dimensions & Aspect Ratio
    private final int screenW;
    private final int screenH;
    private final double screenAspect;

    // Full-Resolution Desktop Frame Buffer
    private BufferedImage desktopImage;
    private int[] desktopPixels;

    // Interactive State
    private volatile boolean isExcluded = true;
    private volatile boolean isPaused = false;
    private volatile boolean running = true;

    // Telemetry
    private volatile double currentFps = 0.0;
    private volatile double avgFrameTimeMs = 0.8;

    public Demo(JFrame parentFrame) {
        this.parentFrame = parentFrame;

        // 1. Detect physical desktop resolution and aspect ratio
        Dimension screenDim = Toolkit.getDefaultToolkit().getScreenSize();
        this.screenW = screenDim.width;
        this.screenH = screenDim.height;
        this.screenAspect = (double) screenW / (double) screenH;

        // 2. Compute initial width strictly according to desktop aspect ratio
        int initialWidth = (int) Math.round(BASE_HEIGHT * screenAspect);
        setPreferredSize(new Dimension(initialWidth, BASE_HEIGHT));
        setMinimumSize(new Dimension(320, 180));
        setIgnoreRepaint(true);

        // 3. Initialize FastScreen
        this.screen = new FastScreen();

        // 4. Prepare full-resolution desktop frame buffer
        this.desktopImage = new BufferedImage(screenW, screenH, BufferedImage.TYPE_INT_RGB);
        this.desktopPixels = ((DataBufferInt) desktopImage.getRaster().getDataBuffer()).getData();

        // 5. Register Keyboard Controls
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
            int curW = getWidth();
            int curH = getHeight();
            if (isPaused) {
                parentFrame.setTitle(String.format(
                    "FastScreen 0.1.1 — PAUSED | %dx%d | Exclusion: %s [E] | [SPACE] Resume | [ESC] Exit",
                    curW, curH,
                    isExcluded ? "ON (Transparent Lens)" : "OFF (Droste Mirror)"
                ));
            } else {
                parentFrame.setTitle(String.format(
                    "FastScreen 0.1.1 — %.1f FPS | %.2f ms | %dx%d | Exclusion: %s [E] | [SPACE] Pause",
                    currentFps, avgFrameTimeMs, curW, curH,
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

        // Retrieve native HWND and apply initial window exclusion
        try {
            hwnd = FastTheme.getWindowHandle(parentFrame);
            if (hwnd != 0) {
                FastScreen.excludeWindow(hwnd);
            }
        } catch (Throwable t) {
            System.err.println("[FastScreen Demo] HWND note: " + t.getMessage());
        }

        // Start Desktop Streaming at native desktop resolution
        boolean streamStarted = screen.startStream(0, 0, screenW, screenH);
        if (!streamStarted) {
            System.err.println("[FastScreen Demo] Note: Streaming started in single-shot fallback mode.");
        }

        updateTitleBar();

        // High-Speed Render Loop
        new Thread(() -> {
            long lastFpsTime = System.nanoTime();
            int frameCount = 0;

            while (running) {
                long frameStart = System.nanoTime();
                boolean hasFrame = false;

                // 1. Ingest frame from FastScreen
                if (!isPaused) {
                    int[] newPixels = screen.getNextFrame();
                    if (newPixels != null && newPixels.length == desktopPixels.length) {
                        hasFrame = true;
                        System.arraycopy(newPixels, 0, desktopPixels, 0, newPixels.length);
                    } else if (!streamStarted) {
                        BufferedImage shot = screen.captureScreen();
                        if (shot != null) {
                            hasFrame = true;
                            int[] shotPx = ((DataBufferInt) shot.getRaster().getDataBuffer()).getData();
                            System.arraycopy(shotPx, 0, desktopPixels, 0, Math.min(shotPx.length, desktopPixels.length));
                        }
                    }
                }

                // 2. Acquire BufferStrategy
                BufferStrategy bs = getBufferStrategy();
                if (bs == null || bs.contentsLost()) {
                    createBufferStrategy(3);
                    bs = getBufferStrategy();
                }

                if (bs != null) {
                    int cw = getWidth();
                    int ch = getHeight();

                    if (cw > 0 && ch > 0) {
                        // Calculate Aspect-Ratio Preserving Destination Rectangle
                        double canvasAspect = (double) cw / (double) ch;
                        int drawX, drawY, drawW, drawH;

                        if (canvasAspect > screenAspect) {
                            // Canvas is wider -> Pillarbox (bars left & right)
                            drawH = ch;
                            drawW = (int) Math.round(drawH * screenAspect);
                            drawX = (cw - drawW) / 2;
                            drawY = 0;
                        } else {
                            // Canvas is taller -> Letterbox (bars top & bottom)
                            drawW = cw;
                            drawH = (int) Math.round(drawW / screenAspect);
                            drawX = 0;
                            drawY = (ch - drawH) / 2;
                        }

                        Graphics g = bs.getDrawGraphics();

                        // Fill dark background if pillarbox or letterbox exists
                        if (drawW < cw || drawH < ch) {
                            g.setColor(new Color(16, 20, 24));
                            g.fillRect(0, 0, cw, ch);
                        }

                        // Fast hardware-accelerated scaling blit
                        g.drawImage(desktopImage, drawX, drawY, drawW, drawH, null);

                        g.dispose();
                        if (!bs.contentsLost()) {
                            bs.show();
                        }
                    }
                }

                // 3. Telemetry Calculation
                long frameEnd = System.nanoTime();
                long durationNs = frameEnd - frameStart;
                avgFrameTimeMs = avgFrameTimeMs * 0.95 + (durationNs / 1_000_000.0) * 0.05;

                frameCount++;
                if (frameEnd - lastFpsTime >= 500_000_000L) {
                    currentFps = (frameCount * 1_000_000_000.0) / (frameEnd - lastFpsTime);
                    frameCount = 0;
                    lastFpsTime = frameEnd;
                    updateTitleBar();
                }

                // Slight yield when idle to avoid spin-locking
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
            JFrame frame = new JFrame("FastScreen 0.1.1 — Desktop Duplication");
            frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
            frame.setIgnoreRepaint(true);
            frame.setIconImage(createRoundIcon());

            // Allow user to resize/scale window freely
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