package com.nativesurface.surface;

import android.content.res.Resources;
import android.graphics.Point;

final class DisplayInfo
{
    private static boolean sLoggedSource;
    private static boolean sLoggedWm;

    private DisplayInfo()
    {
    }

    static int[] size()
    {
        int[] size = fromDisplayInfo();
        if (size == null)
        {
            size = fromWindowManager("getBaseDisplaySize");
        }
        if (size == null)
        {
            size = fromWindowManager("getInitialDisplaySize");
        }
        if (size == null)
        {
            size = fromResources();
        }
        if (size == null)
        {
            LogX.w("cannot determine display size, fall back to 1080x1920");
            size = new int[] {1080, 1920};
        }
        if (!sLoggedSource)
        {
            sLoggedSource = true;
            LogX.i("display size source ok, current " + size[0] + "x" + size[1]);
        }
        return size;
    }

    static int rotation()
    {
        try
        {
            Object info = displayInfoObject();
            if (info != null)
            {
                return Reflect.intField(info, "rotation", 0);
            }
        }
        catch (Throwable ignored)
        {
        }
        try
        {
            Object binder = Reflect.callStatic(Reflect.clazz("android.os.ServiceManager"), "getService",
                new Class<?>[] {String.class}, new Object[] {"window"});
            if (binder != null)
            {
                Object wm = Reflect.callStatic(Reflect.clazz("android.view.IWindowManager$Stub"), "asInterface",
                    new Class<?>[] {android.os.IBinder.class}, new Object[] {binder});
                if (wm != null)
                {
                    Object rot = Reflect.call(wm, "getDefaultDisplayRotation", new Class<?>[ 0 ], new Object[0]);
                    if (rot instanceof Integer)
                    {
                        return (Integer) rot;
                    }
                }
            }
        }
        catch (Throwable ignored)
        {
        }
        return 0;
    }

    static float maxRefreshRate()
    {
        try
        {
            Object info = displayInfoObject();
            if (info == null)
            {
                return 60.0f;
            }
            Object[] modes = null;
            try
            {
                modes = (Object[]) Reflect.call(info, "getSupportedModes", new Class<?>[ 0 ], new Object[0]);
            }
            catch (Throwable ignored)
            {
            }
            if (modes == null)
            {
                modes = (Object[]) Reflect.getField(info, "supportedModes");
            }
            if (modes != null && modes.length > 0)
            {
                float maxFps = 60.0f;
                for (Object m : modes)
                {
                    if (m == null)
                    {
                        continue;
                    }
                    float fps = Reflect.floatField(m, "mRefreshRate", 0f);
                    if (fps <= 0f)
                    {
                        fps = Reflect.floatField(m, "refreshRate", 0f);
                    }
                    if (fps <= 0f)
                    {
                        java.lang.reflect.Method method =
                            Reflect.findMethod(m.getClass(), "getRefreshRate", new Class<?>[ 0 ]);
                        if (method != null)
                        {
                            method.setAccessible(true);
                            fps = ((Number) method.invoke(m)).floatValue();
                        }
                    }
                    if (fps > maxFps)
                    {
                        maxFps = fps;
                    }
                }
                if (maxFps > 0f)
                {
                    return maxFps;
                }
            }
        }
        catch (Throwable ignored)
        {
        }
        return 60.0f;
    }

    private static Object displayInfoObject() throws Throwable
    {
        Object dmg = Reflect.callStatic(Reflect.clazz("android.hardware.display.DisplayManagerGlobal"), "getInstance",
            new Class<?>[ 0 ], new Object[0]);
        if (dmg == null)
        {
            return null;
        }
        return Reflect.call(dmg, "getDisplayInfo", new Class<?>[] {int.class}, new Object[] {0});
    }

    private static int[] fromDisplayInfo()
    {
        try
        {
            Object info = displayInfoObject();
            if (info == null)
            {
                return null;
            }
            int w = Reflect.intField(info, "logicalWidth", 0);
            int h = Reflect.intField(info, "logicalHeight", 0);
            if (w <= 0 || h <= 0)
            {
                w = Reflect.intField(info, "appWidth", 0);
                h = Reflect.intField(info, "appHeight", 0);
            }
            if (w > 0 && h > 0)
            {
                return new int[] {w, h};
            }
        }
        catch (Throwable t)
        {
            LogX.w("DisplayManagerGlobal.getDisplayInfo failed: " + t);
        }
        return null;
    }

    private static int[] fromResources()
    {
        try
        {
            Resources res = Resources.getSystem();
            android.util.DisplayMetrics dm = res.getDisplayMetrics();
            if (dm != null && dm.widthPixels > 0 && dm.heightPixels > 0)
            {
                return new int[] {dm.widthPixels, dm.heightPixels};
            }
        }
        catch (Throwable t)
        {
            LogX.w("Resources.getSystem() failed: " + t);
        }
        return null;
    }

    private static int[] fromWindowManager(String method)
    {
        try
        {
            Object binder = Reflect.callStatic(Reflect.clazz("android.os.ServiceManager"), "getService",
                new Class<?>[] {String.class}, new Object[] {"window"});
            if (binder == null)
            {
                return null;
            }
            Object wm = Reflect.callStatic(Reflect.clazz("android.view.IWindowManager$Stub"), "asInterface",
                new Class<?>[] {android.os.IBinder.class}, new Object[] {binder});
            if (wm == null)
            {
                return null;
            }
            Point p = new Point();
            Reflect.call(wm, method, new Class<?>[] {int.class, Point.class}, new Object[] {0, p});
            if (p.x > 0 && p.y > 0)
            {
                if (!sLoggedWm)
                {
                    sLoggedWm = true;
                    LogX.i(method + ": " + p.x + "x" + p.y);
                }
                return new int[] {p.x, p.y};
            }
        }
        catch (Throwable t)
        {
            LogX.w(method + " failed: " + t);
        }
        return null;
    }
}
