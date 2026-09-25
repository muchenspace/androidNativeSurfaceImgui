package com.nativesurface.surface;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

final class Reflect
{
    private Reflect()
    {
    }

    static Class<?> clazz(String name) throws ClassNotFoundException
    {
        return Class.forName(name);
    }

    static Object newInstance(Class<?> c, Class<?>[] types, Object[] args) throws Throwable
    {
        Constructor<?> ctor = c.getDeclaredConstructor(types);
        ctor.setAccessible(true);
        try
        {
            return ctor.newInstance(args);
        }
        catch (InvocationTargetException e)
        {
            throw e.getCause() != null ? e.getCause() : e;
        }
    }

    static Object callStatic(Class<?> c, String name, Class<?>[] types, Object[] args) throws Throwable
    {
        Method m = c.getDeclaredMethod(name, types);
        m.setAccessible(true);
        try
        {
            return m.invoke(null, args);
        }
        catch (InvocationTargetException e)
        {
            throw e.getCause() != null ? e.getCause() : e;
        }
    }

    static Object call(Object target, String name, Class<?>[] types, Object[] args) throws Throwable
    {
        Method m = target.getClass().getMethod(name, types);
        m.setAccessible(true);
        try
        {
            return m.invoke(target, args);
        }
        catch (InvocationTargetException e)
        {
            throw e.getCause() != null ? e.getCause() : e;
        }
    }

    static Method findMethod(Class<?> c, String name, Class<?>... types)
    {
        try
        {
            return c.getDeclaredMethod(name, types);
        }
        catch (NoSuchMethodException e)
        {
            return null;
        }
    }

    static Object callOptional(Object target, String name, Class<?>[] types, Object[] args)
    {
        try
        {
            Method m = findMethod(target.getClass(), name, types);
            if (m == null)
            {
                return null;
            }
            m.setAccessible(true);
            return m.invoke(target, args);
        }
        catch (Throwable t)
        {
            LogX.w("optional call " + name + " failed: " + t);
            return null;
        }
    }

    static int intField(Object target, String name, int def)
    {
        try
        {
            Field f = target.getClass().getField(name);
            return f.getInt(target);
        }
        catch (Throwable t)
        {
            return def;
        }
    }

    static float floatField(Object target, String name, float def)
    {
        try
        {
            Field f = target.getClass().getField(name);
            return f.getFloat(target);
        }
        catch (Throwable t1)
        {
            try
            {
                Field f = target.getClass().getDeclaredField(name);
                f.setAccessible(true);
                return f.getFloat(target);
            }
            catch (Throwable t2)
            {
                return def;
            }
        }
    }

    static Object getField(Object target, String name)
    {
        try
        {
            Field f = target.getClass().getField(name);
            return f.get(target);
        }
        catch (Throwable t1)
        {
            try
            {
                Field f = target.getClass().getDeclaredField(name);
                f.setAccessible(true);
                return f.get(target);
            }
            catch (Throwable t2)
            {
                return null;
            }
        }
    }
}
