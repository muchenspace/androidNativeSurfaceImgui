package com.nativesurface.surface;

import android.graphics.PixelFormat;
import android.view.Surface;
import java.lang.reflect.Method;

final class SurfaceLayer
{
    static final int LAYER_TOP = 0x7FFFFFFF;

    private static final int FRAME_RATE_COMPATIBILITY_FIXED_SOURCE = 1;

    private static final int CHANGE_FRAME_RATE_ALWAYS = 1;

    private final Class<?> mScClass;
    private final Class<?> mTxnClass;

    private final Object mSurfaceControl;
    private final Object mTransaction;
    private final Surface mSurface;

    private final String mName;
    private final boolean mTrusted;
    private final float mFrameRate;

    private volatile int mCurrentWidth;
    private volatile int mCurrentHeight;
    private volatile boolean mReleased;

    private SurfaceLayer(Class<?> scClass, Class<?> txnClass, Object sc, Object txn, Surface surface, String name,
        int width, int height, boolean trusted, float frameRate)
    {
        mScClass = scClass;
        mTxnClass = txnClass;
        mSurfaceControl = sc;
        mTransaction = txn;
        mSurface = surface;
        mName = name;
        mCurrentWidth = width;
        mCurrentHeight = height;
        mTrusted = trusted;
        mFrameRate = frameRate;
    }

    static SurfaceLayer create(String name, int width, int height, boolean trusted, float frameRate) throws Throwable
    {
        Class<?> scClass = Reflect.clazz("android.view.SurfaceControl");
        Class<?> builderClass = Reflect.clazz("android.view.SurfaceControl$Builder");
        Class<?> txnClass = Reflect.clazz("android.view.SurfaceControl$Transaction");

        Object builder = Reflect.newInstance(builderClass, new Class<?>[ 0 ], new Object[0]);
        Reflect.call(builder, "setName", new Class<?>[] {String.class}, new Object[] {name});
        Reflect.call(builder, "setBufferSize", new Class<?>[] {int.class, int.class}, new Object[] {width, height});
        Reflect.call(builder, "setFormat", new Class<?>[] {int.class}, new Object[] {PixelFormat.TRANSLUCENT});

        Reflect.callOptional(builder, "setHidden", new Class<?>[] {boolean.class}, new Object[] {Boolean.FALSE});
        Reflect.callOptional(builder, "setOpaque", new Class<?>[] {boolean.class}, new Object[] {Boolean.FALSE});

        Object sc = Reflect.call(builder, "build", new Class<?>[ 0 ], new Object[0]);
        LogX.i("SurfaceControl created: " + name + " " + width + "x" + height);

        Object txn = Reflect.newInstance(txnClass, new Class<?>[ 0 ], new Object[0]);

        Reflect.call(txn, "setLayer", new Class<?>[] {scClass, int.class}, new Object[] {sc, LAYER_TOP});
        Reflect.call(txn, "setPosition", new Class<?>[] {scClass, float.class, float.class}, new Object[] {sc, 0f, 0f});

        boolean trustedApplied = trusted && applyTrustedOverlay(txnClass, scClass, txn, sc);
        applyFrameRate(txnClass, scClass, txn, sc, frameRate);

        Reflect.call(txn, "show", new Class<?>[] {scClass}, new Object[] {sc});
        applyTransaction(txnClass, txn);

        Surface surface = (Surface) Reflect.newInstance(Surface.class, new Class<?>[] {scClass}, new Object[] {sc});
        applySurfaceFrameRate(surface, frameRate);

        LogX.i("overlay shown, layer=0x" + Integer.toHexString(LAYER_TOP) + " trusted=" + trustedApplied
            + " frameRate=" + frameRate);
        return new SurfaceLayer(scClass, txnClass, sc, txn, surface, name, width, height, trustedApplied, frameRate);
    }

    private static boolean applyTrustedOverlay(Class<?> txnClass, Class<?> scClass, Object txn, Object sc)
    {
        Class<?>[][] sigs = {
            {scClass, boolean.class},
            {Surface.class, boolean.class},
        };
        for (Class<?>[] sig : sigs)
        {
            Method m = Reflect.findMethod(txnClass, "setTrustedOverlay", sig);
            if (m == null)
            {
                continue;
            }
            try
            {
                m.setAccessible(true);
                m.invoke(txn, new Object[] {sc, Boolean.TRUE});
                return true;
            }
            catch (Throwable t)
            {
                LogX.w("setTrustedOverlay invoke failed: " + t);
            }
        }
        LogX.w("setTrustedOverlay not available on this ROM; overlay still works "
            + "but may be blocked by untrusted-overlay policies");
        return false;
    }

    private static void applyTransaction(Class<?> txnClass, Object txn) throws Throwable
    {
        Method m = Reflect.findMethod(txnClass, "apply", new Class<?>[ 0 ]);
        if (m == null)
        {
            m = Reflect.findMethod(txnClass, "apply", new Class<?>[] {boolean.class});
            if (m == null)
            {
                throw new NoSuchMethodException("SurfaceControl$Transaction.apply");
            }
            m.setAccessible(true);
            m.invoke(txn, new Object[] {Boolean.TRUE});
            return;
        }
        m.setAccessible(true);
        m.invoke(txn, new Object[0]);
    }

    private static void applyFrameRate(Class<?> txnClass, Class<?> scClass, Object txn, Object sc, float fps)
    {
        if (fps <= 0f)
        {
            return;
        }
        try
        {
            Object res =
                Reflect.callOptional(txn, "setFrameRate", new Class<?>[] {scClass, float.class, int.class, int.class},
                    new Object[] {sc, fps, FRAME_RATE_COMPATIBILITY_FIXED_SOURCE, CHANGE_FRAME_RATE_ALWAYS});
            if (res == null)
            {
                Reflect.callOptional(txn, "setFrameRate", new Class<?>[] {scClass, float.class, int.class},
                    new Object[] {sc, fps, FRAME_RATE_COMPATIBILITY_FIXED_SOURCE});
            }
        }
        catch (Throwable t)
        {
            LogX.w("applyFrameRate to SurfaceControl failed: " + t);
        }
    }

    private static void applySurfaceFrameRate(Surface surface, float fps)
    {
        if (surface == null || fps <= 0f)
        {
            return;
        }
        try
        {
            Object res =
                Reflect.callOptional(surface, "setFrameRate", new Class<?>[] {float.class, int.class, int.class},
                    new Object[] {fps, FRAME_RATE_COMPATIBILITY_FIXED_SOURCE, CHANGE_FRAME_RATE_ALWAYS});
            if (res == null)
            {
                Reflect.callOptional(surface, "setFrameRate", new Class<?>[] {float.class, int.class},
                    new Object[] {fps, FRAME_RATE_COMPATIBILITY_FIXED_SOURCE});
            }
        }
        catch (Throwable ignored)
        {
        }
    }

    String name()
    {
        return mName;
    }

    Surface surface()
    {
        return mSurface;
    }

    int width()
    {
        return mCurrentWidth;
    }

    int height()
    {
        return mCurrentHeight;
    }

    boolean trustedOverlayApplied()
    {
        return mTrusted;
    }

    boolean released()
    {
        return mReleased;
    }

    boolean resizable()
    {
        return Reflect.findMethod(mTxnClass, "setBufferSize", new Class<?>[] {mScClass, int.class, int.class}) != null;
    }

    boolean resize(int width, int height)
    {
        if (mReleased || width <= 0 || height <= 0)
        {
            return false;
        }
        if (width == mCurrentWidth && height == mCurrentHeight)
        {
            return true;
        }
        if (!resizable())
        {
            return false;
        }
        try
        {
            Object txn = Reflect.newInstance(mTxnClass, new Class<?>[ 0 ], new Object[0]);
            Reflect.call(txn, "setBufferSize", new Class<?>[] {mScClass, int.class, int.class},
                new Object[] {mSurfaceControl, width, height});
            Reflect.callOptional(txn, "setWindowCrop", new Class<?>[] {mScClass, int.class, int.class},
                new Object[] {mSurfaceControl, width, height});
            Reflect.call(
                txn, "setLayer", new Class<?>[] {mScClass, int.class}, new Object[] {mSurfaceControl, LAYER_TOP});
            Reflect.call(txn, "setPosition", new Class<?>[] {mScClass, float.class, float.class},
                new Object[] {mSurfaceControl, 0f, 0f});
            applyFrameRate(mTxnClass, mScClass, txn, mSurfaceControl, mFrameRate);
            applyTransaction(mTxnClass, txn);
            applySurfaceFrameRate(mSurface, mFrameRate);
            mCurrentWidth = width;
            mCurrentHeight = height;
            LogX.i("layer resized to " + width + "x" + height);
            return true;
        }
        catch (Throwable t)
        {
            LogX.w("resize to " + width + "x" + height + " failed: " + t);
            return false;
        }
    }

    void release()
    {
        if (mReleased)
        {
            return;
        }
        mReleased = true;
        try
        {
            Object txn = Reflect.newInstance(mTxnClass, new Class<?>[ 0 ], new Object[0]);
            Reflect.callOptional(
                txn, "reparent", new Class<?>[] {mScClass, mScClass}, new Object[] {mSurfaceControl, null});
            Reflect.callOptional(txn, "hide", new Class<?>[] {mScClass}, new Object[] {mSurfaceControl});
            Reflect.callOptional(txn, "remove", new Class<?>[] {mScClass}, new Object[] {mSurfaceControl});
            Reflect.callOptional(txn, "apply", new Class<?>[ 0 ], new Object[0]);
        }
        catch (Throwable t)
        {
            LogX.w("detach transaction failed: " + t);
        }
        try
        {
            if (mSurface != null)
            {
                mSurface.release();
            }
        }
        catch (Throwable t)
        {
            LogX.w("Surface.release failed: " + t);
        }
        try
        {
            Reflect.callOptional(mSurfaceControl, "release", new Class<?>[ 0 ], new Object[0]);
        }
        catch (Throwable t)
        {
            LogX.w("SurfaceControl.release failed: " + t);
        }
        LogX.i("overlay released");
    }
}
