import fastscreen.FastScreen;

/**
 * Smoke Test - Basic functionality verification
 * 
 * This test verifies that:
 * 1. Native library loads correctly
 * 2. FastScreen initializes
 * 3. Basic capture works
 * 4. Streaming mode works
 * 
 * Run: mvn compile exec:java
 */
public class SmokeTest {
    
    public static void main(String[] args) {
        System.out.println("========================================");
        System.out.println("  FastScreen Smoke Test");
        System.out.println("========================================");
        System.out.println();
        
        try {
            // Test 1: Library Loading
            System.out.println("[TEST 1] Loading native library...");
            FastScreen screen = new FastScreen();
            System.out.println("✓ Native library loaded successfully");
            System.out.println();
            
            // Test 2: Monitor Count
            System.out.println("[TEST 2] Detecting monitors...");
            int monitors = screen.getMonitorCount();
            System.out.println("✓ Found " + monitors + " monitor(s)");
            System.out.println();
            
            // Test 3: Single Pixel Capture
            System.out.println("[TEST 3] Testing single pixel capture...");
            int color = screen.getPixelColor(100, 100);
            System.out.println("✓ Pixel at (100,100): 0x" + Integer.toHexString(color));
            System.out.println();
            
            // Test 4: Screen Capture
            System.out.println("[TEST 4] Testing screen capture...");
            int[] pixels = screen.captureRaw(0, 0, 100, 100);
            if (pixels != null && pixels.length == 10000) {
                System.out.println("✓ Captured " + pixels.length + " pixels");
            } else {
                System.out.println("✗ Capture failed or returned wrong size");
            }
            System.out.println();
            
            // Test 5: Streaming
            System.out.println("[TEST 5] Testing streaming mode...");
            boolean started = screen.startStream(0, 0, 640, 480);
            if (started) {
                System.out.println("✓ Stream started");
                
                // Try to get a few frames
                int frames = 0;
                long startTime = System.currentTimeMillis();
                
                while (frames < 10 && System.currentTimeMillis() - startTime < 2000) {
                    if (screen.hasNewFrame()) {
                        int[] frame = screen.getNextFrame();
                        if (frame != null) {
                            frames++;
                            System.out.println("  Frame " + frames + ": " + frame.length + " pixels");
                        }
                    } else {
                        // Small sleep to not spin CPU
                        Thread.sleep(16);
                    }
                }
                
                screen.stopStream();
                System.out.println("✓ Captured " + frames + " frames in streaming mode");
            } else {
                System.out.println("✗ Failed to start stream");
            }
            System.out.println();
            
            // Cleanup
            screen.dispose();
            
            System.out.println("========================================");
            System.out.println("  All tests completed!");
            System.out.println("========================================");
            
        } catch (UnsatisfiedLinkError e) {
            System.err.println("✗ FAILED: Native library not found!");
            System.err.println("   Make sure fastscreen.dll is in native/ directory");
            System.err.println("   Run compile.bat to build the DLL");
        } catch (Exception e) {
            System.err.println("✗ FAILED: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
