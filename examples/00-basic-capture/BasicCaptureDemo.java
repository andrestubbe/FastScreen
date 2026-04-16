package fastscreen.examples;

import fastscreen.FastScreen;
import javax.imageio.ImageIO;
import java.awt.Rectangle;
import java.awt.image.BufferedImage;
import java.io.File;

/**
 * Basic FastScreen capture demo.
 * 
 * Demonstrates single screenshot capture and saves to PNG.
 */
public class BasicCaptureDemo {
    
    public static void main(String[] args) {
        System.out.println("FastScreen Basic Capture Demo");
        System.out.println("============================");
        
        try {
            // Create FastScreen instance
            FastScreen screen = new FastScreen();
            
            // Capture full HD screenshot
            System.out.println("\nCapturing 1920x1080 screenshot...");
            Rectangle captureArea = new Rectangle(0, 0, 1920, 1080);
            
            long startTime = System.nanoTime();
            BufferedImage screenshot = screen.captureScreen(captureArea);
            long endTime = System.nanoTime();
            
            double durationMs = (endTime - startTime) / 1_000_000.0;
            System.out.printf("Capture time: %.2f ms%n", durationMs);
            
            // Save to file
            String filename = "screenshot_" + System.currentTimeMillis() + ".png";
            ImageIO.write(screenshot, "PNG", new File(filename));
            System.out.println("Saved to: " + filename);
            
            // Test raw pixel capture
            System.out.println("\nTesting raw pixel capture (100x100)...");
            startTime = System.nanoTime();
            int[] pixels = screen.captureRaw(0, 0, 100, 100);
            endTime = System.nanoTime();
            
            durationMs = (endTime - startTime) / 1_000_000.0;
            System.out.printf("Raw capture time: %.2f ms%n", durationMs);
            System.out.println("Captured " + pixels.length + " pixels");
            
            // Cleanup
            screen.dispose();
            
            System.out.println("\nDemo complete!");
            
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
