package com.nativesurface.surface;

import android.view.Surface;
import java.util.concurrent.ConcurrentHashMap;

public final class SurfaceService
{
    private static final ConcurrentHashMap<Long, Entry> LAYERS = new ConcurrentHashMap<>();

    private SurfaceService()
    {
    }

    private static final class Entry
    {
        final SurfaceLayer layer;
        final DisplayWatcher watcher;

        Entry(SurfaceLayer layer, DisplayWatcher watcher)
        {
            this.layer = layer;
            this.watcher = watcher;
        }
    }

    public static Surface createSurface(
        String name, int width, int height, boolean trusted, int watchIntervalMs, long handle) throws Throwable
    {
        HiddenApiExempt.exemptAll();

        int w = width;
        int h = height;
        if (w <= 0 || h <= 0)
        {
            int[] screen = DisplayInfo.size();
            w = w > 0 ? w : screen[0];
            h = h > 0 ? h : screen[1];
        }

        float frameRate = DisplayInfo.maxRefreshRate();
        LogX.i("creating surface '" + name + "' " + w + "x" + h + " frameRate=" + frameRate + " trusted=" + trusted);

        SurfaceLayer layer = SurfaceLayer.create(name, w, h, trusted, frameRate);
        DisplayWatcher watcher = null;
        if (watchIntervalMs > 0)
        {
            watcher = new DisplayWatcher(layer, handle, watchIntervalMs);
        }

        Entry entry = new Entry(layer, watcher);
        LAYERS.put(handle, entry);

        if (watcher != null)
        {
            watcher.start();
        }
        return layer.surface();
    }

    public static void destroySurface(long handle)
    {
        Entry entry = LAYERS.remove(handle);
        if (entry == null)
        {
            LogX.w("destroySurface: unknown handle " + handle);
            return;
        }
        if (entry.watcher != null)
        {
            entry.watcher.stop();
        }
        entry.layer.release();
    }

    static native void nativeOnGeometryChanged(long handle, int width, int height, int rotation);
}
