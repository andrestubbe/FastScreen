package fastscreen.examples;

import fastscreen.FastScreen;
import java.awt.AWTException;
import java.awt.Rectangle;
import java.awt.Robot;
import java.awt.image.BufferedImage;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * FastScreen Performance Benchmark
 * 
 * Measures and compares:
 * - java.awt.Robot screen capture
 * - FastScreen single capture
 * - FastScreen streaming performance
 * 
 * Run: mvn compile exec:java
 */
public class FastScreenBenchmark {
    
    private static final int WARMUP_ITERATIONS = 10;
    private static final int BENCHMARK_ITERATIONS = 100;
    private static final Rectangle TEST_RECT = new Rectangle(0, 0, 1920, 1080);
    
    public static void main(String[] args) {
        System.out.println("========================================");
        System.out.println("  FastScreen Performance Benchmark");
        System.out.println("========================================");
        System.out.println();
        
        // Detect screen resolution
        java.awt.GraphicsDevice gd = java.awt.GraphicsEnvironment
            .getLocalGraphicsEnvironment().getDefaultScreenDevice();
        int screenWidth = gd.getDisplayMode().getWidth();
        int screenHeight = gd.getDisplayMode().getHeight();
        System.out.println("Screen Resolution: " + screenWidth + "x" + screenHeight);
        System.out.println();
        
        try {
            // Run benchmarks
            benchmarkRobot();
            benchmarkFastScreenSingle();
            benchmarkFastScreenStreaming();
            
        } catch (Exception e) {
            System.err.println("Benchmark failed: " + e.getMessage());
            e.printStackTrace();
        }
        
        System.out.println();
        System.out.println("========================================");
        System.out.println("Benchmark complete!");
        System.out.println("========================================");
    }
    
    private static void benchmarkRobot() throws AWTException {
        System.out.println("[1/3] java.awt.Robot Benchmark");
        System.out.println("----------------------------------------");
        
        Robot robot = new Robot();
        List<Long> times = new ArrayList<>();
        
        // Warmup
        System.out.print("Warming up... ");
        for (int i = 0; i < WARMUP_ITERATIONS; i++) {
            robot.createScreenCapture(TEST_RECT);
        }
        System.out.println("Done");
        
        // Benchmark
        System.out.print("Benchmarking... ");
        for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
            long start = System.nanoTime();
            BufferedImage img = robot.createScreenCapture(TEST_RECT);
            long end = System.nanoTime();
            times.add(end - start);
            
            // Small delay to not overwhelm system
            if (i % 10 == 0) {
                System.out.print(".");
            }
        }
        System.out.println(" Done");
        
        // Calculate statistics
        printStats(times, "Robot");
        System.out.println();
    }
    
    private static void benchmarkFastScreenSingle() {
        System.out.println("[2/3] FastScreen Single Capture Benchmark");
        System.out.println("----------------------------------------");
        
        FastScreen screen = new FastScreen();
        List<Long> times = new ArrayList<>();
        
        // Warmup
        System.out.print("Warming up... ");
        for (int i = 0; i < WARMUP_ITERATIONS; i++) {
            screen.captureScreen(TEST_RECT);
        }
        System.out.println("Done");
        
        // Benchmark
        System.out.print("Benchmarking... ");
        for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
            long start = System.nanoTime();
            BufferedImage img = screen.captureScreen(TEST_RECT);
            long end = System.nanoTime();
            times.add(end - start);
            
            if (i % 10 == 0) {
                System.out.print(".");
            }
        }
        System.out.println(" Done");
        
        // Calculate statistics
        printStats(times, "FastScreen");
        
        // Compare
        double robotAvg = getAverageFromPrevious("Robot");
        double fastAvg = calculateAverage(times);
        double speedup = robotAvg / fastAvg;
        System.out.printf("Speedup vs Robot: %.1f×%n", speedup);
        
        screen.dispose();
        System.out.println();
    }
    
    private static void benchmarkFastScreenStreaming() {
        System.out.println("[3/3] FastScreen Streaming Benchmark");
        System.out.println("----------------------------------------");
        
        FastScreen screen = new FastScreen();
        int streamWidth = 1920;
        int streamHeight = 1080;
        int durationMs = 5000; // 5 seconds
        
        System.out.println("Streaming " + streamWidth + "x" + streamHeight + " for " + (durationMs/1000) + " seconds...");
        
        List<Long> frameTimes = new ArrayList<>();
        int framesCaptured = 0;
        int framesMissed = 0;
        
        screen.startStream(0, 0, streamWidth, streamHeight);
        
        long startTime = System.currentTimeMillis();
        long lastFrameTime = startTime;
        
        while (System.currentTimeMillis() - startTime < durationMs) {
            if (screen.hasNewFrame()) {
                long frameStart = System.nanoTime();
                int[] pixels = screen.getNextFrame();
                long frameEnd = System.nanoTime();
                
                if (pixels != null) {
                    framesCaptured++;
                    frameTimes.add(frameEnd - frameStart);
                } else {
                    framesMissed++;
                }
                
                lastFrameTime = System.currentTimeMillis();
            }
        }
        
        screen.stopStream();
        
        // Calculate streaming stats
        double elapsedSec = durationMs / 1000.0;
        double fps = framesCaptured / elapsedSec;
        
        System.out.println();
        System.out.println("Streaming Results:");
        System.out.printf("  Frames captured: %d%n", framesCaptured);
        System.out.printf("  Frames missed: %d%n", framesMissed);
        System.out.printf("  Average FPS: %.1f%n", fps);
        
        if (!frameTimes.isEmpty()) {
            printStats(frameTimes, "Frame capture");
        }
        
        screen.dispose();
        System.out.println();
    }
    
    private static void printStats(List<Long> timesNanos, String label) {
        List<Long> timesMs = new ArrayList<>();
        for (Long t : timesNanos) {
            timesMs.add(t / 1_000_000); // Convert to ms
        }
        Collections.sort(timesMs);
        
        double avg = timesMs.stream().mapToLong(Long::longValue).average().orElse(0);
        long min = timesMs.get(0);
        long max = timesMs.get(timesMs.size() - 1);
        long p50 = timesMs.get(timesMs.size() / 2);
        long p95 = timesMs.get((int)(timesMs.size() * 0.95));
        long p99 = timesMs.get((int)(timesMs.size() * 0.99));
        
        System.out.println(label + " Statistics:");
        System.out.printf("  Average: %.2f ms%n", avg);
        System.out.printf("  Min: %d ms | Max: %d ms%n", min, max);
        System.out.printf("  P50 (median): %d ms%n", p50);
        System.out.printf("  P95: %d ms | P99: %d ms%n", p95, p99);
    }
    
    private static double robotAvg = 0;
    
    private static double getAverageFromPrevious(String label) {
        // Store Robot average for comparison
        return robotAvg;
    }
    
    private static double calculateAverage(List<Long> times) {
        return times.stream().mapToLong(Long::longValue).average().orElse(0) / 1_000_000.0;
    }
}
