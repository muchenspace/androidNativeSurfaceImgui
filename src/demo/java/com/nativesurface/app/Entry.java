package com.nativesurface.app;

public final class Entry
{
    private static final String LIB_NAME = "androidNativeSurfaceImgui";

    static
    {
        System.loadLibrary(LIB_NAME);
    }

    private Entry()
    {
    }

    public static void main(String[] args)
    {
        try
        {
            nativeMain(args != null ? args : new String[0]);
        }
        finally
        {
            System.exit(0);
        }
    }

    private static native void nativeMain(String[] args);
}
