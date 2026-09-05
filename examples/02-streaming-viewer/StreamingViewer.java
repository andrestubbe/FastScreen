import fastscreen.FastScreen;
import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.awt.image.BufferedImage;
import java.nio.ByteBuffer;
import java.nio.IntBuffer;

/**
 * FastScreen Live Streaming Viewer
 * 
 * Real-time screen capture displayed in a JFrame window.
 * Shows live FPS counter and capture statistics.
 */
public class StreamingViewer extends JFrame {
    
    private final FastScreen screen;
    private final CapturePanel capturePanel;
    private final JLabel statsLabel;
    
    private volatile boolean running = false;
    private Thread captureThread;
    
    // Stats
    private int frameCount = 0;
    private long lastFpsUpdate = 0;
    private double currentFps = 0;
    private double avgCaptureTime = 0;
    
    public StreamingViewer() {
        super("FastScreen Live Stream");
        
        // Initialize FastScreen
        screen = new FastScreen();
        
        // Setup UI
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setLayout(new BorderLayout());
        
        // Capture display panel
        capturePanel = new CapturePanel();
        add(capturePanel, BorderLayout.CENTER);
        
        // Stats panel at bottom
        JPanel statsPanel = new JPanel(new BorderLayout());
        statsLabel = new JLabel("FPS: 0 | Avg: 0ms | Press START to begin");
        statsPanel.add(statsLabel, BorderLayout.WEST);
        
        // Control buttons
        JPanel buttonPanel = new JPanel();
        JButton startBtn = new JButton("START");
        JButton stopBtn = new JButton("STOP");
        JButton exitBtn = new JButton("EXIT");
        
        startBtn.addActionListener(e -> startStream());
        stopBtn.addActionListener(e -> stopStream());
        exitBtn.addActionListener(e -> exit());
        
        buttonPanel.add(startBtn);
        buttonPanel.add(stopBtn);
        buttonPanel.add(exitBtn);
        statsPanel.add(buttonPanel, BorderLayout.EAST);
        
        add(statsPanel, BorderLayout.SOUTH);
        
        // Window setup
        setSize(960, 600);
        setLocationRelativeTo(null);
        
        // Handle window close
        addWindowListener(new WindowAdapter() {
            @Override
            public void windowClosing(WindowEvent e) {
                exit();
            }
        });
    }
    
    private void startStream() {
        if (running) return;
        
        running = true;
        frameCount = 0;
        lastFpsUpdate = System.currentTimeMillis();
        
        // Start streaming at full screen resolution (2880x1920 on Surface Pro)
        // Note: If region capture fails, it falls back to this resolution anyway
        boolean started = screen.startStream(0, 0, 2880, 1920);
        if (!started) {
            JOptionPane.showMessageDialog(this, 
                "Failed to start stream. Check console for details.",
                "Error", JOptionPane.ERROR_MESSAGE);
            running = false;
            return;
        }
        
        // Enable GPU hardware scaling (2880x1920 -> 640x480)
        // This is the KEY optimization: scaling happens on GPU, not CPU!
        boolean scaled = screen.enableHardwareScaling(640, 480, false); // false = Point filter (fast)
        if (scaled) {
            System.out.println("[StreamingViewer] Hardware scaling enabled: 2880x1920 -> 640x480");
        } else {
            System.out.println("[StreamingViewer] Hardware scaling failed, using CPU fallback");
        }
        
        // Update frame dimensions for display
        frameWidth = 640;
        frameHeight = 480;
        
        // Capture thread
        captureThread = new Thread(this::captureLoop);
        captureThread.setDaemon(true);
        captureThread.start();
        
        System.out.println("Stream started");
    }
    
    private final java.util.concurrent.atomic.AtomicReference<int[]> pendingFrame = new java.util.concurrent.atomic.AtomicReference<>();
    
    private void captureLoop() {
        long totalCaptureTime = 0;
        int captureCount = 0;
        
        // Double-buffer pool for decoupling worker and UI
        int[][] buffers = new int[2][frameWidth * frameHeight];
        int bufIdx = 0;
        
        while (running) {
            long startTime = System.nanoTime();
            
            // Capture directly into reusable array
            boolean gotFrame = screen.getNextFrame(buffers[bufIdx]);
            
            if (gotFrame) {
                long captureTime = System.nanoTime() - startTime;
                totalCaptureTime += captureTime;
                captureCount++;
                
                // Publish current buffer to UI and flip to next
                pendingFrame.set(buffers[bufIdx]);
                bufIdx = 1 - bufIdx;
                
                // Calculate stats every second
                frameCount++;
                long now = System.currentTimeMillis();
                if (now - lastFpsUpdate >= 1000) {
                    currentFps = frameCount * 1000.0 / (now - lastFpsUpdate);
                    avgCaptureTime = (totalCaptureTime / captureCount) / 1_000_000.0;
                    
                    frameCount = 0;
                    totalCaptureTime = 0;
                    captureCount = 0;
                    lastFpsUpdate = now;
                    
                    // Update stats on EDT
                    SwingUtilities.invokeLater(this::updateStats);
                }
                
                // Request repaint without flooding EDT queue
                capturePanel.repaint();
            } else {
                java.util.concurrent.locks.LockSupport.parkNanos(500_000L);
            }
        }
    }
    
    private void updateStats() {
        statsLabel.setText(String.format("FPS: %.1f | Avg: %.2fms | Resolution: %dx%d",
            currentFps, avgCaptureTime, frameWidth, frameHeight));
    }
    
    private void stopStream() {
        running = false;
        
        if (captureThread != null) {
            captureThread.interrupt();
            try {
                captureThread.join(1000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
        
        screen.stopStream();
        statsLabel.setText("Stream stopped | Press START to resume");
        System.out.println("Stream stopped");
    }
    
    private void exit() {
        stopStream();
        screen.dispose();
        dispose();
        System.exit(0);
    }
    
    // Custom panel for displaying captured frames
    private class CapturePanel extends JPanel {
        private final BufferedImage displayImage;
        private final int[] displayPixels;
        private final int panelFrameWidth = 640;
        private final int panelFrameHeight = 480;

        public CapturePanel() {
            displayImage = new BufferedImage(panelFrameWidth, panelFrameHeight, BufferedImage.TYPE_INT_ARGB);
            displayPixels = ((java.awt.image.DataBufferInt) displayImage.getRaster().getDataBuffer()).getData();
        }
        
        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            
            int[] latest = pendingFrame.getAndSet(null);
            if (latest != null) {
                System.arraycopy(latest, 0, displayPixels, 0, Math.min(latest.length, displayPixels.length));
            }

            // Scale to fit panel while maintaining aspect ratio
            int panelWidth = getWidth();
            int panelHeight = getHeight();
            
            double scaleX = (double) panelWidth / panelFrameWidth;
            double scaleY = (double) panelHeight / panelFrameHeight;
            double scale = Math.min(scaleX, scaleY);
            
            int newWidth = (int) (panelFrameWidth * scale);
            int newHeight = (int) (panelFrameHeight * scale);
            
            int x = (panelWidth - newWidth) / 2;
            int y = (panelHeight - newHeight) / 2;
            
            g.drawImage(displayImage, x, y, newWidth, newHeight, null);
        }
    }
    
    public static void main(String[] args) {
        // Enable native access for Java 17+
        System.setProperty("jdk.module.illegalAccess", "permit");
        
        SwingUtilities.invokeLater(() -> {
            StreamingViewer viewer = new StreamingViewer();
            viewer.setVisible(true);
            
            // Auto-start after short delay (optional - remove if you want manual start)
            // new Timer(500, e -> viewer.startStream()).start();
        });
    }
    
}
