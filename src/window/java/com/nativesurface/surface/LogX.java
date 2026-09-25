package com.nativesurface.surface;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

final class LogX
{
    static final String TAG = "NativeSurface";

    private static final SimpleDateFormat FMT = new SimpleDateFormat("HH:mm:ss.SSS", Locale.US);

    private static volatile PrintWriter sFile;
    private static volatile boolean sVerbose = true;

    static
    {
        init(System.getenv("NS_LOG"));
        String quiet = System.getenv("NS_QUIET");
        if (quiet != null && !quiet.isEmpty() && !"0".equals(quiet))
        {
            sVerbose = false;
        }
    }

    private LogX()
    {
    }

    static void init(String logFile)
    {
        if (logFile == null || logFile.isEmpty())
        {
            return;
        }
        try
        {
            File f = new File(logFile);
            File parent = f.getParentFile();
            if (parent != null && !parent.exists())
            {
                parent.mkdirs();
            }
            sFile = new PrintWriter(new FileOutputStream(f, true), true);
        }
        catch (Throwable t)
        {
            System.err.println("[" + TAG + "] cannot open log file: " + t);
        }
    }

    static void i(String msg)
    {
        log("I", msg, null);
    }

    static void w(String msg)
    {
        log("W", msg, null);
    }

    static void w(String msg, Throwable t)
    {
        log("W", msg, t);
    }

    static void e(String msg)
    {
        log("E", msg, null);
    }

    static void e(String msg, Throwable t)
    {
        log("E", msg, t);
    }

    private static synchronized void log(String level, String msg, Throwable t)
    {
        String line = "[" + FMT.format(new Date()) + " " + level + "/" + TAG + "] " + msg;
        if (t != null)
        {
            StringWriter sw = new StringWriter();
            t.printStackTrace(new PrintWriter(sw));
            line += "\n" + sw;
        }
        if (sVerbose || !"I".equals(level))
        {
            System.out.println(line);
        }
        PrintWriter f = sFile;
        if (f != null)
        {
            try
            {
                f.println(line);
            }
            catch (Throwable ignored)
            {
            }
        }
    }
}
