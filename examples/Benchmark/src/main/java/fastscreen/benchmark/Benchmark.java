package fastscreen.benchmark;

import fastscreen.FastScreen;
import org.openjdk.jmh.annotations.*;

import java.awt.Rectangle;
import java.awt.Robot;
import java.awt.image.BufferedImage;
import java.util.concurrent.TimeUnit;

@BenchmarkMode(Mode.Throughput)
@OutputTimeUnit(TimeUnit.MILLISECONDS)
@State(Scope.Benchmark)
@Warmup(iterations = 2, time = 1, timeUnit = TimeUnit.SECONDS)
@Measurement(iterations = 3, time = 1, timeUnit = TimeUnit.SECONDS)
@Fork(1)
public class Benchmark {

    private FastScreen fastScreen;
    private Robot awtRobot;
    private Rectangle captureRect;

    @Setup
    public void setup() {
        try {
            fastScreen = new FastScreen();
        } catch (Exception e) {
            fastScreen = null;
        }
        try {
            awtRobot = new Robot();
        } catch (Exception e) {
            awtRobot = null;
        }
        captureRect = new Rectangle(0, 0, 1920, 1080);
    }

    @TearDown
    public void tearDown() {
        if (fastScreen != null) {
            try {
                fastScreen.dispose();
            } catch (Exception ignored) {}
        }
    }

    @org.openjdk.jmh.annotations.Benchmark
    public int benchmarkFastScreenGetPixelColor() {
        if (fastScreen != null) {
            return fastScreen.getPixelColor(100, 100);
        }
        return 0;
    }

    @org.openjdk.jmh.annotations.Benchmark
    public int benchmarkAwtRobotGetPixelColor() {
        if (awtRobot != null) {
            return awtRobot.getPixelColor(100, 100).getRGB();
        }
        return 0;
    }
}
