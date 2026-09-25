package com.nativesurface.surface;

final class HiddenApiExempt
{
    private static volatile boolean sDone;

    private HiddenApiExempt()
    {
    }

    static synchronized void exemptAll()
    {
        if (sDone)
        {
            return;
        }
        sDone = true;
        try
        {
            Class<?> vmRuntime = Reflect.clazz("dalvik.system.VMRuntime");
            Object rt = Reflect.callStatic(vmRuntime, "getRuntime", new Class<?>[ 0 ], new Object[0]);
            Reflect.call(
                rt, "setHiddenApiExemptions", new Class<?>[] {String[].class}, new Object[] {new String[] {"L"}});
            LogX.i("hidden api exemptions: L");
        }
        catch (Throwable t)
        {
            LogX.w("setHiddenApiExemptions failed, fall back to direct reflection: " + t);
        }
    }
}
