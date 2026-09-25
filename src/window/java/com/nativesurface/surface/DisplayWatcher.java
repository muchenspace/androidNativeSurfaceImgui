package com.nativesurface.surface;

final class DisplayWatcher implements Runnable
{
    static final int DEFAULT_INTERVAL_MS = 250;

    private final SurfaceLayer mLayer;
    private final long mHandle;
    private final int mIntervalMs;

    private volatile boolean mRunning = true;
    private Thread mThread;

    DisplayWatcher(SurfaceLayer layer, long handle, int intervalMs)
    {
        mLayer = layer;
        mHandle = handle;
        mIntervalMs = intervalMs > 0 ? intervalMs : DEFAULT_INTERVAL_MS;
    }

    void start()
    {
        SurfaceService.nativeOnGeometryChanged(mHandle, mLayer.width(), mLayer.height(), DisplayInfo.rotation());
        mThread = new Thread(this, "ns-display-watch");
        mThread.setDaemon(true);
        mThread.start();
        LogX.i("display watcher started, interval=" + mIntervalMs + "ms");
    }

    void stop()
    {
        mRunning = false;
        Thread t = mThread;
        if (t != null)
        {
            t.interrupt();
            try
            {
                t.join(1000);
            }
            catch (InterruptedException e)
            {
                Thread.currentThread().interrupt();
            }
        }
        mThread = null;
    }

    @Override public void run()
    {
        int lastW = mLayer.width();
        int lastH = mLayer.height();
        int lastRot = DisplayInfo.rotation();
        int fails = 0;

        while (mRunning && !mLayer.released())
        {
            try
            {
                int[] size = DisplayInfo.size();
                int rot = DisplayInfo.rotation();
                int w = size[0];
                int h = size[1];
                if (w > 0 && h > 0 && (w != lastW || h != lastH || rot != lastRot))
                {
                    LogX.i("display changed: " + lastW + "x" + lastH + "@" + (lastRot * 90) + " -> " + w + "x" + h + "@"
                        + (rot * 90));
                    if (mLayer.resize(w, h))
                    {
                        lastW = w;
                        lastH = h;
                        lastRot = rot;
                        fails = 0;
                        SurfaceService.nativeOnGeometryChanged(mHandle, w, h, rot);
                    }
                    else
                    {
                        fails++;
                        if (fails % 20 == 1)
                        {
                            LogX.w("in-place resize unsupported on this ROM, layer stays at " + mLayer.width() + "x"
                                + mLayer.height() + "; image may be stretched after rotation");
                        }
                    }
                }
            }
            catch (Throwable t)
            {
                LogX.w("display watch tick failed: " + t);
            }

            try
            {
                Thread.sleep(mIntervalMs);
            }
            catch (InterruptedException e)
            {
                Thread.currentThread().interrupt();
                return;
            }
        }
        LogX.i("display watcher stopped");
    }
}
