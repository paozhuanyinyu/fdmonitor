package com.hook.fdmonitor;

import android.content.Context;
import android.util.Log;
import com.bytedance.android.bytehook.ByteHook;
import com.tencent.mmkv.MMKV;

import java.util.ArrayList;
import java.util.ListIterator;

public class FdMonitorManager {
    private static String TAG = "FdMonitorManager";
    private static final int DEFAULT_MAX_STACK_LAYER = 32;
    private static String sPackageName = "com.hook.fdmonitor.FdMonitorManager";
    public static void init(Context context, boolean enablePrintLog){
        ByteHook.init();
        String rootDir = MMKV.initialize(context);
        Log.i(TAG, "mmkv root: " + rootDir);
        System.loadLibrary("fdmonitor");
        initMonitor(enablePrintLog);
    }
    public static native void initMonitor(boolean enablePrintLog);

    public static void saveBackTrace(int fd, String log){
        Log.i(TAG, "saveBackTrace--->" + fd);
        MMKV.defaultMMKV().putString(String.valueOf(fd), log);
//        FdUtils.INSTANCE.printFd();
    }

    public static void removeBackTrace(int fd){
        Log.i(TAG, "removeBackTrace--->" + fd);
        MMKV.defaultMMKV().remove(String.valueOf(fd));
//        FdUtils.INSTANCE.printFd();
    }

    private static final class JavaContext {
        private final String stack;
        private String threadName;

        private JavaContext() {
            stack = stackTraceToString(new Throwable().getStackTrace());
            if (null != Thread.currentThread()) {
                threadName = Thread.currentThread().getName();
            }
        }
    }

    /**
     * 声明为private，给c++部分调用！！！不要干掉！！！
     * @return
     */
    private static JavaContext getJavaContext() {
        try {
            return new JavaContext();
        } catch (Throwable th) {
            Log.e(TAG, "get javacontext exception: " + th.getMessage());
        }

        return null;
    }

    public static String stackTraceToString(final StackTraceElement[] arr) {
        if (arr == null) {
            return "";
        }

        ArrayList<StackTraceElement> stacks = new ArrayList<>(arr.length);
        for (int i = 0; i < arr.length; i++) {
            String className = arr[i].getClassName();
            // remove unused stacks
//            if (className.contains("libcore.io")
//                    || className.contains("com.tencent.matrix.iocanary")
//                    || className.contains("java.io")
//                    || className.contains("dalvik.system")
//                    || className.contains("android.os")) {
//                continue;
//            }

            stacks.add(arr[i]);
        }
        // stack still too large
        if (stacks.size() > DEFAULT_MAX_STACK_LAYER && sPackageName != null) {
            ListIterator<StackTraceElement> iterator = stacks.listIterator(stacks.size());
            // from backward to forward
            while (iterator.hasPrevious()) {
                StackTraceElement stack = iterator.previous();
                String className = stack.getClassName();
                if (className.contains(sPackageName)) {
                    iterator.remove();
                }
                if (stacks.size() <= DEFAULT_MAX_STACK_LAYER) {
                    break;
                }
            }
        }
        StringBuffer sb = new StringBuffer(stacks.size());
        for (StackTraceElement stackTraceElement : stacks) {
            sb.append(stackTraceElement).append('\n');
        }
        return sb.toString();
    }

}
